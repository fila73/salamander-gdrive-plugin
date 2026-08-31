// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "gdrive_fs.h"
#include "gdrive_auth.h"
#include "gdrive_api.h"
#include "gdrive_cache.h"
#include "gdrive_http.h"
#include "gdrive_log.h"
#include "dialogs.h"
#include "dialog_find.h"

CPluginInterfaceForFS InterfaceForFS;

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
    m_pathToIdCache["/Shared with me"] = "shared_with_me_root";
    m_pathToIdCache["/Starred"] = "starred_root";
    m_pathToIdCache["/Recent"] = "recent_root";
    m_pathToIdCache["/Trash"] = "trash_root";
}

CPluginFS::~CPluginFS()
{
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
           FS_SERVICE_MOVEFROMFS |
           FS_SERVICE_COPYFROMDISKTOFS |
           FS_SERVICE_MOVEFROMDISKTOFS |
           FS_SERVICE_CREATEDIR |
           FS_SERVICE_QUICKRENAME |
           FS_SERVICE_DELETE |
           FS_SERVICE_VIEWFILE |
           FS_SERVICE_SHOWINFO |
           FS_SERVICE_GETPATHFORMAINWNDTITLE |
           FS_SERVICE_GETCHANGEDRIVEORDISCONNECTITEM |
           FS_SERVICE_GETFSICON |
           FS_SERVICE_GETFREESPACE |
           FS_SERVICE_CONTEXTMENU |
           FS_SERVICE_OPENFINDDLG |
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
    if (!retValue) return;

    static GDriveApi::AboutInfo cachedInfo;
    static DWORD lastFetchTime = 0;
    DWORD now = GetTickCount();

    if (now - lastFetchTime > 60000 || lastFetchTime == 0)
    {
        if (GDriveApi::ApiClient::GetInstance().GetAbout(cachedInfo))
        {
            lastFetchTime = now;
        }
    }

    if (cachedInfo.quota.limit > 0)
    {
        int64_t freeBytes = cachedInfo.quota.limit - cachedInfo.quota.usage;
        if (freeBytes < 0) freeBytes = 0;
        retValue->SetUI64((unsigned __int64)freeBytes);
    }
    else
    {
        *retValue = CQuadWord(-1, -1);
    }
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
                 LoadStr(IDS_STORAGE_INFO_FMT),
                 info.userName.c_str(), info.userEmail.c_str(),
                 usedGB, totalGB, (usedGB / totalGB) * 100.0,
                 (double)info.quota.usageInTrash / (1024.0 * 1024.0));
    }
    else
    {
        snprintf(msg, sizeof(msg),
                 LoadStr(IDS_STORAGE_UNLIMITED_FMT),
                 info.userName.c_str(), info.userEmail.c_str(),
                 usedGB);
    }

    SalamanderGeneral->SalMessageBox(parent, msg, LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONINFORMATION);
}

BOOL WINAPI CPluginFS::ExecuteCommandLine(HWND parent, char* command, int& selFrom, int& selTo)
{
    return FALSE;
}

BOOL WINAPI CPluginFS::OpenFindDialog(const char* fsName, int panel)
{
    CGDriveFindDialog::Launch(SalamanderGeneral->GetMainWindowHWND(), panel, m_currentPath, m_currentFolderId);
    return TRUE;
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

static std::set<CPluginFSInterfaceAbstract*> s_activeFSInstances;
static std::mutex s_fsInstancesMutex;

bool CPluginFS::IsOurFS(CPluginFSInterfaceAbstract* fs)
{
    if (!fs) return false;
    std::lock_guard<std::mutex> lock(s_fsInstancesMutex);
    return s_activeFSInstances.find(fs) != s_activeFSInstances.end();
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

    CPluginFS* fs = new (std::nothrow) CPluginFS(fsName);
    if (fs)
    {
        std::lock_guard<std::mutex> lock(s_fsInstancesMutex);
        s_activeFSInstances.insert(fs);
    }
    return fs;
}

void WINAPI CPluginInterfaceForFS::CloseFS(CPluginFSInterfaceAbstract* fs)
{
    if (fs)
    {
        {
            std::lock_guard<std::mutex> lock(s_fsInstancesMutex);
            s_activeFSInstances.erase(fs);
        }
        delete static_cast<CPluginFS*>(fs);
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
    CPluginFS* fs = static_cast<CPluginFS*>(pluginFS);
    if (!fs) return;

    if (isDir)
    {
        if (isDir == 2) // ".."
        {
            std::string curPath = fs->GetCurrentPathStr();
            std::string parentPath = CPluginFS::GetValidParentPath(curPath);
            std::string focusName;

            if (curPath.length() > parentPath.length())
            {
                focusName = curPath.substr(parentPath == "/" ? 0 : parentPath.length());
                if (!focusName.empty() && focusName[0] == '/') focusName = focusName.substr(1);
            }

            std::string winParent = parentPath;
            std::replace(winParent.begin(), winParent.end(), '/', '\\');
            if (winParent.empty()) winParent = "\\";

            std::string ansiFocus = GDriveHttp::HttpClient::Utf8ToAnsi(focusName);

            fs = NULL; // pointer might become invalid after ChangePanelPath
            SalamanderGeneral->ChangePanelPathToPluginFS(panel, pluginFSName, winParent.c_str(), NULL,
                                                         -1, ansiFocus.empty() ? NULL : ansiFocus.c_str());
        }
        else // subdirectory
        {
            std::string curPath = fs->GetCurrentPathStr();
            std::string nextPath = (curPath == "/" ? "" : curPath) + "/" + (file.Name ? file.Name : "");
            std::string winNext = nextPath;
            std::replace(winNext.begin(), winNext.end(), '/', '\\');
            if (winNext.empty()) winNext = "\\";

            fs = NULL;
            SalamanderGeneral->ChangePanelPathToPluginFS(panel, pluginFSName, winNext.c_str());
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

void WINAPI CPluginInterfaceForFS::ConvertPathToInternal(const char* fsName, int fsNameIndex, char* fsUserPart)
{
    if (!fsUserPart) return;
    for (char* p = fsUserPart; *p; ++p)
    {
        if (*p == '/') *p = '\\';
    }
}

void WINAPI CPluginInterfaceForFS::ConvertPathToExternal(const char* fsName, int fsNameIndex, char* fsUserPart)
{
    if (!fsUserPart) return;
    for (char* p = fsUserPart; *p; ++p)
    {
        if (*p == '/') *p = '\\';
    }
}
