// SPDX-FileCopyrightText: 2026 Open Salamander Authors & Red Salamander Authors
// SPDX-FileContributor: Ported to Open Salamander framework by fila73
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "gdrive_fs.h"
#include "gdrive_auth.h"
#include "gdrive_api.h"
#include "gdrive_http.h"
#include "dialogs.h"

CPluginInterfaceForFS InterfaceForFS;

static const char* kMyDriveDir = "My Drive";
static const char* kSharedDrivesDir = "Shared Drives";

CPluginFS::CPluginFS(const char* fsName)
    : m_fsName(fsName ? fsName : "gdrive"),
      m_currentPath(""),
      m_currentFolderId("root"),
      m_currentDriveId(""),
      m_isSharedDrive(false)
{
    m_pathToIdCache["/"] = "";
    m_pathToIdCache["/My Drive"] = "root";
    m_pathToIdCache["/Shared Drives"] = "shared_drives_root";
}

CPluginFS::~CPluginFS()
{
}

BOOL WINAPI CPluginFS::GetCurrentPath(char* userPart)
{
    if (!userPart) return FALSE;

    if (m_currentPath.empty() || m_currentPath == "/")
    {
        strcpy(userPart, "\\");
    }
    else
    {
        std::string winPath = m_currentPath;
        std::replace(winPath.begin(), winPath.end(), '/', '\\');
        strncpy(userPart, winPath.c_str(), MAX_PATH - 1);
        userPart[MAX_PATH - 1] = '\0';
    }
    return TRUE;
}

BOOL WINAPI CPluginFS::GetFullName(CFileData& file, int isDir, char* buf, int bufSize)
{
    if (!buf || bufSize <= 0) return FALSE;

    std::string path;
    if (m_currentPath.empty() || m_currentPath == "/")
    {
        path = "\\" + std::string(file.Name ? file.Name : "");
    }
    else
    {
        std::string winPath = m_currentPath;
        std::replace(winPath.begin(), winPath.end(), '/', '\\');
        if (isDir == 2) // ".."
        {
            size_t pos = winPath.rfind('\\');
            if (pos != std::string::npos && pos > 0)
                path = winPath.substr(0, pos);
            else
                path = "\\";
        }
        else
        {
            path = winPath + "\\" + std::string(file.Name ? file.Name : "");
        }
    }

    strncpy(buf, path.c_str(), bufSize - 1);
    buf[bufSize - 1] = '\0';
    return TRUE;
}

BOOL WINAPI CPluginFS::GetFullFSPath(HWND parent, const char* fsName, char* path, int pathSize, BOOL& success)
{
    if (!path || pathSize <= 0)
    {
        success = FALSE;
        return FALSE;
    }

    char userPart[MAX_PATH] = {0};
    GetCurrentPath(userPart);

    std::string full = std::string(fsName ? fsName : "gdrive") + ":" + userPart;
    strncpy(path, full.c_str(), pathSize - 1);
    path[pathSize - 1] = '\0';

    success = TRUE;
    return TRUE;
}

BOOL WINAPI CPluginFS::GetRootPath(char* userPart)
{
    if (!userPart) return FALSE;
    strcpy(userPart, "\\");
    return TRUE;
}

BOOL WINAPI CPluginFS::IsCurrentPath(int currentFSNameIndex, int fsNameIndex, const char* userPart)
{
    if (currentFSNameIndex != fsNameIndex) return FALSE;
    char cur[MAX_PATH] = {0};
    GetCurrentPath(cur);
    return SalamanderGeneral->IsTheSamePath(cur, userPart ? userPart : "\\");
}

BOOL WINAPI CPluginFS::IsOurPath(int currentFSNameIndex, int fsNameIndex, const char* userPart)
{
    return currentFSNameIndex == fsNameIndex;
}

BOOL WINAPI CPluginFS::ChangePath(int currentFSNameIndex, char* fsName, int fsNameIndex,
                                  const char* userPart, char* cutFileName, BOOL* pathWasCut,
                                  BOOL forceRefresh, int mode)
{
    if (pathWasCut) *pathWasCut = FALSE;
    if (cutFileName) *cutFileName = '\0';
    if (fsName) lstrcpyn(fsName, AssignedFSName, MAX_PATH);

    std::string newPath = userPart ? userPart : "";
    std::replace(newPath.begin(), newPath.end(), '\\', '/');

    while (newPath.size() > 1 && newPath.back() == '/')
        newPath.pop_back();

    if (newPath.empty()) newPath = "/";

    m_currentPath = newPath;
    m_lastErrorPath.clear();
    return ResolveCurrentFolderId();
}

bool CPluginFS::ResolveCurrentFolderId()
{
    if (m_currentPath.empty() || m_currentPath == "/")
    {
        m_currentFolderId = "";
        m_currentDriveId = "";
        m_isSharedDrive = false;
        return true;
    }

    auto it = m_pathToIdCache.find(m_currentPath);
    if (it != m_pathToIdCache.end())
    {
        m_currentFolderId = it->second;
        m_isSharedDrive = (m_currentPath.rfind("/Shared Drives", 0) == 0 && m_currentPath != "/Shared Drives");
        return true;
    }

    if (m_currentPath == "/My Drive")
    {
        m_currentFolderId = "root";
        m_currentDriveId = "";
        m_isSharedDrive = false;
        m_pathToIdCache[m_currentPath] = "root";
        return true;
    }

    if (m_currentPath == "/Shared Drives")
    {
        m_currentFolderId = "shared_drives_root";
        m_currentDriveId = "";
        m_isSharedDrive = true;
        m_pathToIdCache[m_currentPath] = "shared_drives_root";
        return true;
    }

    std::string seg;
    std::istringstream ss(m_currentPath);
    std::string accumulated = "";
    std::string parentId = "root";
    std::string driveId = "";
    bool isShared = false;

    while (std::getline(ss, seg, '/'))
    {
        if (seg.empty()) continue;
        accumulated += "/" + seg;

        if (accumulated == "/My Drive")
        {
            parentId = "root";
            isShared = false;
            m_pathToIdCache[accumulated] = "root";
            continue;
        }

        if (accumulated == "/Shared Drives")
        {
            parentId = "shared_drives_root";
            isShared = true;
            m_pathToIdCache[accumulated] = "shared_drives_root";
            continue;
        }

        auto cached = m_pathToIdCache.find(accumulated);
        if (cached != m_pathToIdCache.end())
        {
            parentId = cached->second;
            if (isShared && driveId.empty()) driveId = parentId;
            continue;
        }

        if (parentId == "shared_drives_root")
        {
            std::vector<GDriveApi::GDriveItem> drives;
            if (GDriveApi::ApiClient::GetInstance().ListSharedDrives(drives))
            {
                bool found = false;
                for (const auto& d : drives)
                {
                    if (d.name == seg)
                    {
                        parentId = d.id;
                        driveId = d.id;
                        m_pathToIdCache[accumulated] = parentId;
                        found = true;
                        break;
                    }
                }
                if (!found) return false;
            }
            else
            {
                return false;
            }
        }
        else
        {
            std::vector<GDriveApi::GDriveItem> items;
            if (GDriveApi::ApiClient::GetInstance().ListFolder(parentId, driveId, isShared, items))
            {
                bool found = false;
                for (const auto& item : items)
                {
                    if (item.isFolder && item.name == seg)
                    {
                        parentId = item.id;
                        m_pathToIdCache[accumulated] = parentId;
                        found = true;
                        break;
                    }
                }
                if (!found) return false;
            }
            else
            {
                return false;
            }
        }
    }

    m_currentFolderId = parentId;
    m_currentDriveId = driveId;
    m_isSharedDrive = isShared;
    return true;
}

static void AddItemToDir(CSalamanderDirectoryAbstract* dir,
                         const char* name,
                         bool isDir,
                         int64_t size,
                         const FILETIME* ft,
                         CPluginDataInterfaceAbstract* pluginData)
{
    CFileData file;
    memset(&file, 0, sizeof(file));
    file.Name = SalamanderGeneral->DupStr(name ? name : "");
    if (!file.Name) return;

    file.NameLen = (int)strlen(file.Name);

    int sortByExtDirsAsFiles = 0;
    SalamanderGeneral->GetConfigParameter(SALCFG_SORTBYEXTDIRSASFILES, &sortByExtDirsAsFiles,
                                          sizeof(sortByExtDirsAsFiles), NULL);

    if (!sortByExtDirsAsFiles && isDir)
        file.Ext = file.Name + file.NameLen;
    else
    {
        const char* pDot = strrchr(file.Name, '.');
        file.Ext = (pDot != NULL) ? const_cast<char*>(pDot + 1) : file.Name + file.NameLen;
    }

    file.Size.SetUI64((unsigned __int64)size);
    if (ft) file.LastWrite = *ft;
    file.Attr = isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    file.DosName = NULL;
    file.PluginData = 0;
    file.IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED;

    if (isDir)
    {
        dir->AddDir(NULL, file, pluginData);
    }
    else
    {
        dir->AddFile(NULL, file, pluginData);
    }
}

BOOL WINAPI CPluginFS::ListCurrentPath(CSalamanderDirectoryAbstract* dir,
                                      CPluginDataInterfaceAbstract*& pluginData,
                                      int& iconsType, BOOL forceRefresh)
{
    if (!dir) return FALSE;

    iconsType = pitFromRegistry;
    pluginData = NULL;
    m_cachedItems.clear();

    dir->SetFlags(SALDIRFLAG_IGNOREDUPDIRS);
    dir->SetValidData(VALID_DATA_EXTENSION | VALID_DATA_SIZE | VALID_DATA_TYPE |
                      VALID_DATA_DATE | VALID_DATA_TIME | VALID_DATA_ATTRIBUTES);

    if (!GDriveAuth::AuthManager::GetInstance().IsAuthenticated())
    {
        std::string err;
        if (!GDriveAuth::AuthManager::GetInstance().LaunchInteractiveAuth(NULL, &err))
        {
            SalamanderGeneral->SalMessageBox(NULL, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            return FALSE;
        }
    }

    // Up-dir ".." if not at root
    if (!m_currentPath.empty() && m_currentPath != "/")
    {
        FILETIME ft = {0, 0};
        GetSystemTimeAsFileTime(&ft);
        AddItemToDir(dir, "..", true, 0, &ft, pluginData);
    }

    // 1. Root listing: show "My Drive" and "Shared Drives"
    if (m_currentPath.empty() || m_currentPath == "/")
    {
        FILETIME ft = {0, 0};
        GetSystemTimeAsFileTime(&ft);

        GDriveApi::GDriveItem itemMy;
        itemMy.id = "root";
        itemMy.name = kMyDriveDir;
        itemMy.isFolder = true;
        m_cachedItems.push_back(itemMy);

        AddItemToDir(dir, kMyDriveDir, true, 0, &ft, pluginData);

        if (CfgIncludeSharedDrives)
        {
            GDriveApi::GDriveItem itemShared;
            itemShared.id = "shared_drives_root";
            itemShared.name = kSharedDrivesDir;
            itemShared.isFolder = true;
            m_cachedItems.push_back(itemShared);

            AddItemToDir(dir, kSharedDrivesDir, true, 0, &ft, pluginData);
        }
        return TRUE;
    }

    // 2. Shared Drives root: show all Shared Drives
    if (m_currentPath == "/Shared Drives")
    {
        std::vector<GDriveApi::GDriveItem> drives;
        std::string err;
        if (!GDriveApi::ApiClient::GetInstance().ListSharedDrives(drives, &err))
        {
            if (m_lastErrorPath != m_currentPath)
            {
                m_lastErrorPath = m_currentPath;
                SalamanderGeneral->SalMessageBox(NULL, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            }
            return TRUE;
        }
        m_lastErrorPath.clear();

        FILETIME ft = {0, 0};
        GetSystemTimeAsFileTime(&ft);

        for (const auto& d : drives)
        {
            m_cachedItems.push_back(d);
            std::string subPath = m_currentPath + "/" + d.name;
            m_pathToIdCache[subPath] = d.id;

            AddItemToDir(dir, d.name.c_str(), true, 0, &ft, pluginData);
        }
        return TRUE;
    }

    // 3. Regular folder listing (My Drive, subfolders, or Shared Drive contents)
    std::string err;
    if (!GDriveApi::ApiClient::GetInstance().ListFolder(m_currentFolderId, m_currentDriveId, m_isSharedDrive, m_cachedItems, &err))
    {
        if (m_lastErrorPath != m_currentPath)
        {
            m_lastErrorPath = m_currentPath;
            SalamanderGeneral->SalMessageBox(NULL, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
        }
        return TRUE;
    }
    m_lastErrorPath.clear();

    for (auto& item : m_cachedItems)
    {
        std::string subPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + item.name;
        m_pathToIdCache[subPath] = item.id;

        if (item.isFolder)
        {
            AddItemToDir(dir, item.name.c_str(), true, 0, &item.modifiedTime, pluginData);
        }
        else
        {
            std::string displayName = item.name;
            if (item.isGoogleDoc && !item.exportExtension.empty())
            {
                if (displayName.length() < item.exportExtension.length() ||
                    displayName.compare(displayName.length() - item.exportExtension.length(), item.exportExtension.length(), item.exportExtension) != 0)
                {
                    displayName += item.exportExtension;
                }
            }

            AddItemToDir(dir, displayName.c_str(), false, item.size, &item.modifiedTime, pluginData);
        }
    }

    return TRUE;
}

BOOL WINAPI CPluginFS::TryCloseOrDetach(BOOL forceClose, BOOL canDetach, BOOL& detach, int reason)
{
    detach = FALSE;
    return TRUE;
}

void CPluginFS::Event(int event, DWORD param)
{
}

void CPluginFS::ReleaseObject(HWND parent)
{
}

DWORD WINAPI CPluginFS::GetSupportedServices()
{
    return FS_SERVICE_COPYFROMFS |
           FS_SERVICE_VIEWFILE |
           FS_SERVICE_SHOWINFO |
           FS_SERVICE_GETCHANGEDRIVEORDISCONNECTITEM |
           FS_SERVICE_GETFSICON |
           FS_SERVICE_GETFREESPACE |
           FS_SERVICE_CONTEXTMENU |
           FS_SERVICE_ACCEPTSCHANGENOTIF;
}

BOOL WINAPI CPluginFS::GetChangeDriveOrDisconnectItem(const char* fsName, char*& title, HICON& icon, BOOL& destroyIcon)
{
    destroyIcon = FALSE;
    icon = (HICON)LoadImage(DLLInstance, MAKEINTRESOURCE(IDI_GDRIVE), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    title = (char*)SalamanderGeneral->Alloc(64);
    if (title)
    {
        snprintf(title, 64, "%s\tGoogle Drive\t%s", fsName, GDriveAuth::AuthManager::GetInstance().GetAccountDisplay().c_str());
        return TRUE;
    }
    return FALSE;
}

HICON WINAPI CPluginFS::GetFSIcon(BOOL& destroyIcon)
{
    destroyIcon = FALSE;
    return (HICON)LoadImage(DLLInstance, MAKEINTRESOURCE(IDI_GDRIVE), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
}

void WINAPI CPluginFS::GetDropEffect(const char* srcFSPath, const char* tgtFSPath,
                                     DWORD allowedEffects, DWORD keyState, DWORD* dropEffect)
{
    if (dropEffect) *dropEffect = DROPEFFECT_COPY;
}

void WINAPI CPluginFS::GetFSFreeSpace(CQuadWord* retValue)
{
    if (retValue) *retValue = CQuadWord(-1, -1);
}

BOOL WINAPI CPluginFS::GetNextDirectoryLineHotPath(const char* text, int pathLen, int& offset)
{
    return FALSE;
}

void WINAPI CPluginFS::CompleteDirectoryLineHotPath(char* path, int pathBufSize)
{
}

BOOL WINAPI CPluginFS::GetPathForMainWindowTitle(const char* fsName, int mode, char* buf, int bufSize)
{
    return FALSE;
}

void WINAPI CPluginFS::ShowInfoDialog(const char* fsName, HWND parent)
{
    GDriveApi::AboutInfo info;
    std::string err;
    if (!GDriveApi::ApiClient::GetInstance().GetAbout(info, &err))
    {
        SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
        return;
    }

    char msg[1024] = {0};
    double usedGB = (double)info.quota.usage / (1024.0 * 1024.0 * 1024.0);
    double totalGB = (double)info.quota.limit / (1024.0 * 1024.0 * 1024.0);

    if (info.quota.limit > 0)
    {
        snprintf(msg, sizeof(msg),
                 "Account: %s (%s)\n\n"
                 "Google Drive Storage:\n"
                 "Used: %.2f GB of %.2f GB (%.1f %%)\n"
                 "Trash: %.2f MB",
                 info.userName.c_str(), info.userEmail.c_str(),
                 usedGB, totalGB, (usedGB / totalGB) * 100.0,
                 (double)info.quota.usageInTrash / (1024.0 * 1024.0));
    }
    else
    {
        snprintf(msg, sizeof(msg),
                 "Account: %s (%s)\n\n"
                 "Google Drive Storage:\n"
                 "Used: %.2f GB (Unlimited quota)",
                 info.userName.c_str(), info.userEmail.c_str(),
                 usedGB);
    }

    SalamanderGeneral->SalMessageBox(parent, msg, LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONINFORMATION);
}

BOOL WINAPI CPluginFS::ExecuteCommandLine(HWND parent, char* command, int& selFrom, int& selTo)
{
    return FALSE;
}

BOOL WINAPI CPluginFS::QuickRename(const char* fsName, int mode, HWND parent, CFileData& file,
                                   BOOL isDir, char* newName, BOOL& cancel)
{
    return FALSE;
}

void WINAPI CPluginFS::AcceptChangeOnPathNotification(const char* fsName, const char* path, BOOL includingSubdirs)
{
}

BOOL WINAPI CPluginFS::CreateDir(const char* fsName, int mode, HWND parent, char* newName, BOOL& cancel)
{
    return FALSE;
}

BOOL WINAPI CPluginFS::Delete(const char* fsName, int mode, HWND parent, int panel,
                              int selectedFiles, int selectedDirs, BOOL& cancelOrError)
{
    return FALSE;
}

BOOL WINAPI CPluginFS::CopyOrMoveFromDiskToFS(BOOL copy, int mode, const char* fsName, HWND parent,
                                              const char* sourcePath, SalEnumSelection2 next,
                                              void* nextParam, int selectedFiles, int selectedDirs,
                                              char* targetPath, BOOL* cancelOrHandlePath)
{
    return FALSE;
}

BOOL WINAPI CPluginFS::ChangeAttributes(const char* fsName, HWND parent, int panel,
                                        int selectedFiles, int selectedDirs)
{
    return FALSE;
}

void WINAPI CPluginFS::ShowProperties(const char* fsName, HWND parent, int panel,
                                      int selectedFiles, int selectedDirs)
{
    BOOL isDir = FALSE;
    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
    int index = 0;
    const CFileData* f = focused ? SalamanderGeneral->GetPanelFocusedItem(panel, &isDir)
                                 : SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);
    if (!f) return;

    char info[1024] = {0};
    uint64_t size = f->Size.Value;
    snprintf(info, sizeof(info), "Name: %s\nType: %s\nSize: %llu B\nPath: %s",
             f->Name, isDir ? "Folder" : "File", (unsigned long long)size, m_currentPath.c_str());

    SalamanderGeneral->SalMessageBox(parent, info, "Properties", MB_OK | MB_ICONINFORMATION);
}

void WINAPI CPluginFS::ContextMenu(const char* fsName, HWND parent, int menuX, int menuY, int type,
                                  int panel, int selectedFiles, int selectedDirs)
{
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuA(hMenu, MF_STRING, CM_CALC_SIZE, LoadStr(IDS_MENU_CALC_SIZE));

    int cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
                             menuX, menuY, 0, parent, NULL);
    DestroyMenu(hMenu);

    if (cmd == CM_CALC_SIZE)
    {
        CalculateFolderSize(parent, panel);
    }
}

BOOL WINAPI CPluginFS::HandleMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* plResult)
{
    return FALSE;
}

BOOL WINAPI CPluginFS::OpenFindDialog(const char* fsName, int panel)
{
    return FALSE;
}

void WINAPI CPluginFS::OpenActiveFolder(const char* fsName, HWND parent)
{
}

void WINAPI CPluginFS::GetAllowedDropEffects(int mode, const char* tgtFSPath, DWORD* allowedEffects)
{
    if (allowedEffects) *allowedEffects = 0;
}

BOOL WINAPI CPluginFS::GetNoItemsInPanelText(char* textBuf, int textBufSize)
{
    return FALSE;
}

void WINAPI CPluginFS::ShowSecurityInfo(HWND parent)
{
}

bool CPluginFS::DownloadSingleItem(const GDriveApi::GDriveItem& item, const std::wstring& targetDir, HWND parent)
{
    std::string fileName = item.name;
    if (item.isGoogleDoc && !item.exportExtension.empty())
    {
        if (fileName.length() < item.exportExtension.length() ||
            fileName.compare(fileName.length() - item.exportExtension.length(), item.exportExtension.length(), item.exportExtension) != 0)
        {
            fileName += item.exportExtension;
        }
    }

    std::wstring localPath = targetDir;
    if (!localPath.empty() && localPath.back() != L'\\')
        localPath += L"\\";
    localPath += GDriveHttp::HttpClient::Utf8ToWide(fileName);

    std::string err;
    bool success = GDriveApi::ApiClient::GetInstance().DownloadFile(item, localPath, nullptr, nullptr, &err);
    if (!success && !err.empty())
    {
        SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
    }
    return success;
}

bool CPluginFS::DownloadFolderRecursive(const GDriveApi::GDriveItem& folder, const std::wstring& targetDir, HWND parent)
{
    std::wstring subDir = targetDir;
    if (!subDir.empty() && subDir.back() != L'\\')
        subDir += L"\\";
    subDir += GDriveHttp::HttpClient::Utf8ToWide(folder.name);

    CreateDirectoryW(subDir.c_str(), NULL);

    std::vector<GDriveApi::GDriveItem> children;
    std::string err;
    if (!GDriveApi::ApiClient::GetInstance().ListFolder(folder.id, folder.driveId, folder.isSharedDrive, children, &err))
    {
        return false;
    }

    for (const auto& child : children)
    {
        if (child.isFolder)
        {
            if (!DownloadFolderRecursive(child, subDir, parent))
                return false;
        }
        else
        {
            if (!DownloadSingleItem(child, subDir, parent))
                return false;
        }
    }
    return true;
}

BOOL WINAPI CPluginFS::CopyOrMoveFromFS(BOOL copy, int mode, const char* fsName, HWND parent,
                                       int panel, int selectedFiles, int selectedDirs,
                                       char* targetPath, BOOL& operationMask,
                                       BOOL& cancelOrHandlePath, HWND dropTarget)
{
    operationMask = FALSE;
    cancelOrHandlePath = FALSE;

    if (mode == 1)
    {
        // Ask Salamander to show standard destination dialog
        return FALSE;
    }
    if (mode == 4)
    {
        return FALSE;
    }

    // Clean target directory path
    char target[2 * MAX_PATH] = {0};
    lstrcpyn(target, targetPath, 2 * MAX_PATH);
    char* lastBs = strrchr(target, '\\');
    char* comp = (lastBs != NULL) ? lastBs + 1 : target;
    if (strchr(comp, '*') != NULL || strchr(comp, '?') != NULL)
    {
        if (lastBs != NULL)
            *lastBs = 0;
        else
            target[0] = 0;
    }

    std::wstring wTargetDir = GDriveHttp::HttpClient::Utf8ToWide(target);

    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
    int index = 0;
    BOOL isDir = FALSE;
    BOOL success = TRUE;

    while (true)
    {
        const CFileData* f = focused ? SalamanderGeneral->GetPanelFocusedItem(panel, &isDir)
                                     : SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);
        if (f == NULL)
            break;

        // Find item in cache
        const GDriveApi::GDriveItem* targetItem = nullptr;
        for (const auto& item : m_cachedItems)
        {
            if (_stricmp(item.name.c_str(), f->Name) == 0)
            {
                targetItem = &item;
                break;
            }
        }

        if (targetItem)
        {
            if (targetItem->isFolder)
            {
                if (!DownloadFolderRecursive(*targetItem, wTargetDir, parent))
                {
                    success = FALSE;
                    break;
                }
            }
            else
            {
                if (!DownloadSingleItem(*targetItem, wTargetDir, parent))
                {
                    success = FALSE;
                    break;
                }
            }
        }

        if (focused)
            break;
    }

    if (success)
        targetPath[0] = 0;
    else
        cancelOrHandlePath = TRUE;

    return TRUE;
}

void WINAPI CPluginFS::ViewFile(const char* fsName, HWND parent,
                                CSalamanderForViewFileOnFSAbstract* salamander,
                                CFileData& file)
{
    if (!file.Name || !salamander) return;

    const GDriveApi::GDriveItem* targetItem = nullptr;
    for (const auto& item : m_cachedItems)
    {
        std::string checkName = item.name;
        if (item.isGoogleDoc && !item.exportExtension.empty())
        {
            if (checkName.length() < item.exportExtension.length() ||
                checkName.compare(checkName.length() - item.exportExtension.length(), item.exportExtension.length(), item.exportExtension) != 0)
            {
                checkName += item.exportExtension;
            }
        }

        if (_stricmp(checkName.c_str(), file.Name) == 0 || _stricmp(item.name.c_str(), file.Name) == 0)
        {
            targetItem = &item;
            break;
        }
    }

    if (!targetItem) return;

    std::string uniqueFileName = std::string(fsName ? fsName : "gdrive") + ":" + m_currentPath + "/" + targetItem->id + "_" + targetItem->name;
    if (targetItem->isGoogleDoc && !targetItem->exportExtension.empty())
        uniqueFileName += targetItem->exportExtension;

    BOOL fileExists = FALSE;
    const char* tmpFileName = salamander->AllocFileNameInCache(parent, uniqueFileName.c_str(), file.Name, NULL, fileExists);
    if (!tmpFileName) return;

    BOOL newFileOK = FALSE;
    CQuadWord newFileSize(0, 0);

    if (!fileExists)
    {
        std::wstring wTempPath = GDriveHttp::HttpClient::Utf8ToWide(tmpFileName);
        std::string err;
        newFileOK = GDriveApi::ApiClient::GetInstance().DownloadFile(*targetItem, wTempPath, nullptr, nullptr, &err);
        if (!newFileOK)
        {
            salamander->FreeFileNameInCache(uniqueFileName.c_str(), FALSE, FALSE, newFileSize, NULL, FALSE, TRUE);
            SalamanderGeneral->SalMessageBox(parent, err.empty() ? "Failed to download file" : err.c_str(),
                                             LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            return;
        }

        HANDLE hF = CreateFileW(wTempPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (hF != INVALID_HANDLE_VALUE)
        {
            DWORD dwHigh = 0;
            DWORD dwLow = GetFileSize(hF, &dwHigh);
            newFileSize = CQuadWord(dwLow, dwHigh);
            CloseHandle(hF);
        }
    }

    HANDLE fileLock = NULL;
    BOOL fileLockOwner = FALSE;
    salamander->OpenViewer(parent, tmpFileName, &fileLock, &fileLockOwner);
    salamander->FreeFileNameInCache(uniqueFileName.c_str(), fileExists, newFileOK, newFileSize, fileLock, fileLockOwner, FALSE);
}

void CPluginFS::CalculateFolderSize(HWND parent, int panel)
{
    BOOL isDir = FALSE;
    int index = 0;
    const CFileData* f = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
    if (!f) return;

    if (!isDir)
    {
        char msg[512];
        std::string szStr = CCalcSizeProgressDialog::FormatSize(f->Size.Value);
        std::string numStr = CCalcSizeProgressDialog::FormatNumber(f->Size.Value);
        _snprintf_s(msg, _TRUNCATE, LoadStr(IDS_CALC_RESULT_FMT), f->Name, 0, 1, szStr.c_str(), numStr.c_str());
        SalamanderGeneral->SalMessageBox(parent, msg, LoadStr(IDS_CALC_TITLE), MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (strcmp(f->Name, "..") == 0)
    {
        std::string curName = m_currentPath;
        if (curName.empty() || curName == "/")
            curName = "Google Drive (Root)";
        else
        {
            size_t p = curName.rfind('/');
            if (p != std::string::npos) curName = curName.substr(p + 1);
        }

        std::string folderId = m_currentFolderId.empty() ? "root" : m_currentFolderId;
        CCalcSizeProgressDialog dlg(parent, curName.c_str(), folderId, m_currentDriveId, m_isSharedDrive);
        dlg.Run();
        return;
    }

    // Find item ID in cache or path
    std::string folderId;
    std::string folderName = f->Name;
    for (const auto& it : m_cachedItems)
    {
        if (_stricmp(it.name.c_str(), f->Name) == 0)
        {
            folderId = it.id;
            break;
        }
    }

    if (folderId.empty())
    {
        if (_stricmp(folderName.c_str(), "My Drive") == 0 || _stricmp(folderName.c_str(), LoadStr(IDS_MY_DRIVE)) == 0)
        {
            folderId = "root";
        }
    }

    if (folderId.empty())
    {
        SalamanderGeneral->SalMessageBox(parent, "Nelze zjistit identifikátor této položky.", LoadStr(IDS_CALC_TITLE), MB_OK | MB_ICONEXCLAMATION);
        return;
    }

    CCalcSizeProgressDialog dlg(parent, folderName.c_str(), folderId, m_currentDriveId, m_isSharedDrive);
    dlg.Run();

    // Update size in panel to replace DIR/ADR with calculated size
    BOOL isStillDir = FALSE;
    const CFileData* curF = SalamanderGeneral->GetPanelFocusedItem(panel, &isStillDir);
    if (curF && isStillDir && _stricmp(curF->Name, folderName.c_str()) == 0)
    {
        CFileData* nonConstF = const_cast<CFileData*>(curF);
        nonConstF->Size.Value = dlg.GetTotalBytes();
        nonConstF->SizeValid = 1;
        nonConstF->Dirty = 1;
        SalamanderGeneral->RepaintChangedItems(panel);
    }
    else
    {
        int itIdx = 0;
        BOOL itemIsDir = FALSE;
        const CFileData* item = NULL;
        while ((item = SalamanderGeneral->GetPanelItem(panel, &itIdx, &itemIsDir)) != NULL)
        {
            if (itemIsDir && _stricmp(item->Name, folderName.c_str()) == 0)
            {
                CFileData* nonConst = const_cast<CFileData*>(item);
                nonConst->Size.Value = dlg.GetTotalBytes();
                nonConst->SizeValid = 1;
                nonConst->Dirty = 1;
                SalamanderGeneral->RepaintChangedItems(panel);
                break;
            }
        }
    }
}

//
// CPluginInterfaceForFS
//

CPluginFSInterfaceAbstract* WINAPI CPluginInterfaceForFS::OpenFS(const char* fsName, int fsNameIndex)
{
    if (!GDriveAuth::AuthManager::GetInstance().IsAuthenticated())
    {
        std::string err;
        if (!GDriveAuth::AuthManager::GetInstance().LaunchInteractiveAuth(NULL, &err))
        {
            SalamanderGeneral->SalMessageBox(NULL, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            return NULL;
        }
    }

    return new (std::nothrow) CPluginFS(fsName);
}

void WINAPI CPluginInterfaceForFS::CloseFS(CPluginFSInterfaceAbstract* fs)
{
    if (fs)
    {
        delete (CPluginFS*)fs;
    }
}

void WINAPI CPluginInterfaceForFS::ExecuteChangeDriveMenuItem(int panel)
{
    int failReason = 0;
    SalamanderGeneral->ChangePanelPathToPluginFS(panel, AssignedFSName, "\\", &failReason);
}

BOOL WINAPI CPluginInterfaceForFS::ChangeDriveMenuItemContextMenu(HWND parent, int panel, int x, int y,
                                                                 CPluginFSInterfaceAbstract* pluginFS,
                                                                 const char* pluginFSName, int pluginFSNameIndex,
                                                                 BOOL isDetachedFS, BOOL& refreshMenu,
                                                                 BOOL& closeMenu, int& postCmd, void*& postCmdParam)
{
    return FALSE;
}

void WINAPI CPluginInterfaceForFS::ExecuteChangeDrivePostCommand(int panel, int postCmd, void* postCmdParam)
{
}

void WINAPI CPluginInterfaceForFS::ExecuteOnFS(int panel, CPluginFSInterfaceAbstract* pluginFS,
                                              const char* pluginFSName, int pluginFSNameIndex,
                                              CFileData& file, int isDir)
{
    CPluginFS* fs = (CPluginFS*)pluginFS;
    if (isDir)
    {
        char newPath[MAX_PATH];
        char curPath[MAX_PATH];
        fs->GetCurrentPath(curPath);
        lstrcpyn(newPath, curPath, MAX_PATH);

        if (isDir == 2) // ".."
        {
            char* cutDir = NULL;
            if (SalamanderGeneral->CutDirectory(newPath, &cutDir))
            {
                char focusName[MAX_PATH] = {0};
                if (cutDir) lstrcpyn(focusName, cutDir, MAX_PATH);

                fs = NULL; // pointer might become invalid after ChangePanelPath
                SalamanderGeneral->ChangePanelPathToPluginFS(panel, pluginFSName, newPath, NULL,
                                                             -1, focusName[0] ? focusName : NULL);
            }
            else
            {
                SalamanderGeneral->ChangePanelPathToPluginFS(panel, pluginFSName, "\\");
            }
        }
        else // subdirectory
        {
            if (SalamanderGeneral->SalPathAppend(newPath, file.Name, MAX_PATH))
            {
                fs = NULL;
                SalamanderGeneral->ChangePanelPathToPluginFS(panel, pluginFSName, newPath);
            }
        }
    }
}

BOOL WINAPI CPluginInterfaceForFS::DisconnectFS(HWND parent, BOOL isInPanel, int panel,
                                               CPluginFSInterfaceAbstract* pluginFS,
                                               const char* pluginFSName, int pluginFSNameIndex)
{
    if (isInPanel)
    {
        SalamanderGeneral->DisconnectFSFromPanel(parent, panel);
        return SalamanderGeneral->GetPanelPluginFS(panel) != pluginFS;
    }
    else
    {
        return SalamanderGeneral->CloseDetachedFS(parent, pluginFS);
    }
}
