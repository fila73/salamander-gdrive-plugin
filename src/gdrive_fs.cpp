// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "gdrive_fs.h"
#include "gdrive_auth.h"
#include "gdrive_api.h"
#include "gdrive_cache.h"
#include "gdrive_http.h"
#include "dialogs.h"

CPluginInterfaceForFS InterfaceForFS;

static const char* kMyDriveDir = "My Drive";
static const char* kSharedDrivesDir = "Shared Drives";
static const char* kSharedWithMeDir = "Shared with me";
static const char* kStarredDir = "Starred";
static const char* kRecentDir = "Recent";
static const char* kTrashDir = "Trash";

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
    std::string oldPath = m_currentPath;

    if (pathWasCut) *pathWasCut = FALSE;
    if (cutFileName) *cutFileName = '\0';
    if (fsName) lstrcpyn(fsName, AssignedFSName, MAX_PATH);

    std::string newPath = userPart ? userPart : "";
    std::replace(newPath.begin(), newPath.end(), '\\', '/');

    while (newPath.size() > 1 && newPath.back() == '/')
        newPath.pop_back();

    if (newPath.empty() || newPath == "/")
        newPath = "/";
    else if (newPath[0] != '/')
        newPath = "/" + newPath;

    m_currentPath = newPath;
    m_lastErrorPath.clear();

    if (ResolveCurrentFolderId())
    {
        // If navigating up from a subpath, populate cutFileName so Salamander focuses the exited folder
        if (cutFileName && !oldPath.empty() && oldPath.length() > newPath.length())
        {
            if (oldPath.compare(0, newPath == "/" ? 0 : newPath.length(), newPath == "/" ? "" : newPath) == 0)
            {
                std::string rel = oldPath.substr(newPath == "/" ? 0 : newPath.length());
                if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);
                if (!rel.empty())
                {
                    std::string ansiRel = GDriveHttp::HttpClient::Utf8ToAnsi(rel);
                    lstrcpynA(cutFileName, ansiRel.c_str(), MAX_PATH);
                    if (pathWasCut) *pathWasCut = TRUE;
                }
            }
        }
        return TRUE;
    }

    // If direct resolution fails (e.g. Salamander stripped "/Archiv2021/22" after slash to "/Archiv2021" on navigating UP),
    // try trimming backwards to find the nearest valid ancestor path
    std::string fallbackPath = newPath;
    while (fallbackPath.length() > 1)
    {
        size_t slashPos = fallbackPath.rfind('/');
        if (slashPos == std::string::npos || slashPos == 0)
        {
            fallbackPath = "/";
        }
        else
        {
            fallbackPath = fallbackPath.substr(0, slashPos);
        }

        m_currentPath = fallbackPath;
        if (ResolveCurrentFolderId())
        {
            if (cutFileName && !oldPath.empty() && oldPath.length() > fallbackPath.length())
            {
                std::string rel = oldPath.substr(fallbackPath == "/" ? 0 : fallbackPath.length());
                if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);
                if (!rel.empty())
                {
                    std::string ansiRel = GDriveHttp::HttpClient::Utf8ToAnsi(rel);
                    lstrcpynA(cutFileName, ansiRel.c_str(), MAX_PATH);
                }
            }
            if (pathWasCut) *pathWasCut = TRUE;
            return TRUE;
        }
    }

    m_currentPath = newPath;
    return FALSE;
}

bool CPluginFS::ResolveCurrentFolderId()
{
    if (m_currentPath.empty() || m_currentPath == "/" || _stricmp(m_currentPath.c_str(), "/") == 0)
    {
        m_currentFolderId = "";
        m_currentDriveId = "";
        m_isSharedDrive = false;
        return true;
    }

    if (_stricmp(m_currentPath.c_str(), "/My Drive") == 0)
    {
        m_currentFolderId = "root";
        m_currentDriveId = "";
        m_isSharedDrive = false;
        m_pathToIdCache[m_currentPath] = "root";
        return true;
    }

    if (_stricmp(m_currentPath.c_str(), "/Shared Drives") == 0)
    {
        m_currentFolderId = "shared_drives_root";
        m_currentDriveId = "";
        m_isSharedDrive = true;
        m_pathToIdCache[m_currentPath] = "shared_drives_root";
        return true;
    }

    if (_stricmp(m_currentPath.c_str(), "/Shared with me") == 0)
    {
        m_currentFolderId = "shared_with_me_root";
        m_currentDriveId = "";
        m_isSharedDrive = false;
        m_pathToIdCache[m_currentPath] = "shared_with_me_root";
        return true;
    }

    if (_stricmp(m_currentPath.c_str(), "/Starred") == 0)
    {
        m_currentFolderId = "starred_root";
        m_currentDriveId = "";
        m_isSharedDrive = false;
        m_pathToIdCache[m_currentPath] = "starred_root";
        return true;
    }

    if (_stricmp(m_currentPath.c_str(), "/Recent") == 0)
    {
        m_currentFolderId = "recent_root";
        m_currentDriveId = "";
        m_isSharedDrive = false;
        m_pathToIdCache[m_currentPath] = "recent_root";
        return true;
    }

    if (_stricmp(m_currentPath.c_str(), "/Trash") == 0)
    {
        m_currentFolderId = "trash_root";
        m_currentDriveId = "";
        m_isSharedDrive = false;
        m_pathToIdCache[m_currentPath] = "trash_root";
        return true;
    }

    auto it = m_pathToIdCache.find(m_currentPath);
    if (it != m_pathToIdCache.end())
    {
        m_currentFolderId = it->second;
        m_isSharedDrive = (_strnicmp(m_currentPath.c_str(), "/Shared Drives", 14) == 0 && _stricmp(m_currentPath.c_str(), "/Shared Drives") != 0);
        return true;
    }

    std::vector<std::string> segs;
    std::string seg;
    std::istringstream ss(m_currentPath);
    while (std::getline(ss, seg, '/'))
    {
        if (!seg.empty()) segs.push_back(seg);
    }

    std::string accumulated = "";
    std::string parentId = "root";
    std::string driveId = "";
    bool isShared = false;

    for (size_t i = 0; i < segs.size(); ++i)
    {
        accumulated += "/" + segs[i];

        if (_stricmp(accumulated.c_str(), "/My Drive") == 0)
        {
            parentId = "root";
            isShared = false;
            m_pathToIdCache[accumulated] = "root";
            continue;
        }

        if (_stricmp(accumulated.c_str(), "/Shared Drives") == 0)
        {
            parentId = "shared_drives_root";
            isShared = true;
            m_pathToIdCache[accumulated] = "shared_drives_root";
            continue;
        }

        if (_stricmp(accumulated.c_str(), "/Shared with me") == 0)
        {
            parentId = "shared_with_me_root";
            isShared = false;
            m_pathToIdCache[accumulated] = "shared_with_me_root";
            continue;
        }

        if (_stricmp(accumulated.c_str(), "/Starred") == 0)
        {
            parentId = "starred_root";
            isShared = false;
            m_pathToIdCache[accumulated] = "starred_root";
            continue;
        }

        if (_stricmp(accumulated.c_str(), "/Recent") == 0)
        {
            parentId = "recent_root";
            isShared = false;
            m_pathToIdCache[accumulated] = "recent_root";
            continue;
        }

        if (_stricmp(accumulated.c_str(), "/Trash") == 0)
        {
            parentId = "trash_root";
            isShared = false;
            m_pathToIdCache[accumulated] = "trash_root";
            continue;
        }

        auto cached = m_pathToIdCache.find(accumulated);
        if (cached != m_pathToIdCache.end())
        {
            parentId = cached->second;
            if (isShared && driveId.empty()) driveId = parentId;
            continue;
        }

        std::vector<GDriveApi::GDriveItem> items;
        if (_stricmp(parentId.c_str(), "shared_drives_root") == 0)
        {
            if (!GDriveApi::ApiClient::GetInstance().ListSharedDrives(items)) return false;
            for (auto& item : items) item.driveId = item.id;
        }
        else if (_stricmp(parentId.c_str(), "shared_with_me_root") == 0)
        {
            if (!GDriveApi::ApiClient::GetInstance().ListSharedWithMe(items)) return false;
        }
        else
        {
            if (!GDriveApi::ApiClient::GetInstance().ListFolder(parentId, driveId, isShared, items)) return false;
        }

        bool found = false;
        for (const auto& item : items)
        {
            if (item.isFolder && (_stricmp(item.name.c_str(), segs[i].c_str()) == 0 ||
                                  _stricmp(GDriveHttp::HttpClient::Utf8ToAnsi(item.name).c_str(), segs[i].c_str()) == 0))
            {
                parentId = item.id;
                if (isShared && driveId.empty()) driveId = parentId;
                m_pathToIdCache[accumulated] = parentId;
                found = true;
                break;
            }
        }

        // Multi-segment matching for folder names containing slashes (e.g. "Archiv2021/22")
        if (!found && i + 1 < segs.size())
        {
            std::string combined = segs[i];
            size_t j = i + 1;
            while (j < segs.size())
            {
                combined += "/" + segs[j];
                for (const auto& item : items)
                {
                    if (item.isFolder && (_stricmp(item.name.c_str(), combined.c_str()) == 0 ||
                                          _stricmp(GDriveHttp::HttpClient::Utf8ToAnsi(item.name).c_str(), combined.c_str()) == 0))
                    {
                        parentId = item.id;
                        if (isShared && driveId.empty()) driveId = parentId;
                        accumulated += "/" + segs[j];
                        m_pathToIdCache[accumulated] = parentId;
                        found = true;
                        i = j;
                        break;
                    }
                }
                if (found) break;
                j++;
            }
        }

        if (!found) return false;
    }

    m_currentFolderId = parentId;
    m_currentDriveId = driveId;
    m_isSharedDrive = isShared;
    m_pathToIdCache[m_currentPath] = parentId;
    return true;
}

static void AddItemToDir(CSalamanderDirectoryAbstract* dir, const char* name,
                         bool isDir, int64_t size, const FILETIME* ft,
                         CPluginDataInterfaceAbstract* pluginData)
{
    if (!name) return;

    std::string ansiName;
    if (strcmp(name, "..") == 0 || strcmp(name, ".") == 0)
    {
        ansiName = name;
    }
    else
    {
        ansiName = GDriveHttp::HttpClient::Utf8ToAnsi(name);
    }

    CFileData file;
    memset(&file, 0, sizeof(file));
    file.Name = SalamanderGeneral->DupStr(ansiName.c_str());
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
    if (isDir && size > 0)
    {
        file.SizeValid = 1;
    }
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

static int64_t GetCachedOrComputedFolderSize(const std::string& folderId)
{
    if (folderId.empty()) return 0;
    int64_t sz = 0;
    int files = 0, dirs = 0;
    if (GDriveCache::CacheManager::GetInstance().GetFolderSize(folderId, sz))
    {
        return sz;
    }
    if (GDriveCache::CacheManager::GetInstance().ComputeFolderSizeFromCache(folderId, sz, files, dirs))
    {
        return sz;
    }
    return 0;
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
    if (!m_currentPath.empty() && m_currentPath != "/" && _stricmp(m_currentPath.c_str(), "/") != 0)
    {
        FILETIME ft = {0, 0};
        GetSystemTimeAsFileTime(&ft);
        AddItemToDir(dir, "..", true, 0, &ft, pluginData);
    }

    // 1. Root listing: show "My Drive" and "Shared Drives"
    if (m_currentPath.empty() || m_currentPath == "/" || _stricmp(m_currentPath.c_str(), "/") == 0)
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

        GDriveApi::GDriveItem itemSharedWithMe;
        itemSharedWithMe.id = "shared_with_me_root";
        itemSharedWithMe.name = kSharedWithMeDir;
        itemSharedWithMe.isFolder = true;
        m_cachedItems.push_back(itemSharedWithMe);

        AddItemToDir(dir, kSharedWithMeDir, true, 0, &ft, pluginData);

        GDriveApi::GDriveItem itemStarred;
        itemStarred.id = "starred_root";
        itemStarred.name = kStarredDir;
        itemStarred.isFolder = true;
        m_cachedItems.push_back(itemStarred);

        AddItemToDir(dir, kStarredDir, true, 0, &ft, pluginData);

        GDriveApi::GDriveItem itemRecent;
        itemRecent.id = "recent_root";
        itemRecent.name = kRecentDir;
        itemRecent.isFolder = true;
        m_cachedItems.push_back(itemRecent);

        AddItemToDir(dir, kRecentDir, true, 0, &ft, pluginData);

        GDriveApi::GDriveItem itemTrash;
        itemTrash.id = "trash_root";
        itemTrash.name = kTrashDir;
        itemTrash.isFolder = true;
        m_cachedItems.push_back(itemTrash);

        AddItemToDir(dir, kTrashDir, true, 0, &ft, pluginData);

        return TRUE;
    }

    // Check cache synchronization with Changes API
    if (forceRefresh)
    {
        if (!GDriveCache::CacheManager::GetInstance().IsSmartCtrlR())
        {
            GDriveCache::CacheManager::GetInstance().InvalidateFolder(m_currentFolderId);
        }
        GDriveCache::CacheManager::GetInstance().CheckForRemoteChanges(true);
    }
    else
    {
        GDriveCache::CacheManager::GetInstance().CheckForRemoteChanges(false);
    }

    // Check if we have a cache hit for the current folder
    std::vector<GDriveApi::GDriveItem> cachedList;
    if (!m_currentFolderId.empty() && GDriveCache::CacheManager::GetInstance().GetFolder(m_currentFolderId, cachedList))
    {
        for (const auto& item : cachedList)
        {
            m_cachedItems.push_back(item);
            std::string subPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + item.name;
            m_pathToIdCache[subPath] = item.id;

            std::string ansiName = GDriveHttp::HttpClient::Utf8ToAnsi(item.name);
            for (char& c : ansiName) { if (c == '/' || c == '\\') c = '_'; }
            std::string ansiSubPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + ansiName;
            m_pathToIdCache[ansiSubPath] = item.id;

            if (item.isFolder)
            {
                AddItemToDir(dir, item.name.c_str(), true, GetCachedOrComputedFolderSize(item.id), &item.modifiedTime, pluginData);
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

    // 2. Shared Drives root: show all Shared Drives
    if (_stricmp(m_currentPath.c_str(), "/Shared Drives") == 0)
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

        GDriveCache::CacheManager::GetInstance().PutFolder("shared_drives_root", drives);

        FILETIME ft = {0, 0};
        GetSystemTimeAsFileTime(&ft);

        for (const auto& d : drives)
        {
            m_cachedItems.push_back(d);
            std::string subPath = m_currentPath + "/" + d.name;
            m_pathToIdCache[subPath] = d.id;

            AddItemToDir(dir, d.name.c_str(), true, GetCachedOrComputedFolderSize(d.id), &ft, pluginData);
        }
        return TRUE;
    }

    // 2b. Shared with me root: show all files & folders shared with the user
    if (_stricmp(m_currentPath.c_str(), "/Shared with me") == 0)
    {
        std::vector<GDriveApi::GDriveItem> items;
        std::string err;
        if (!GDriveApi::ApiClient::GetInstance().ListSharedWithMe(items, &err))
        {
            if (m_lastErrorPath != m_currentPath)
            {
                m_lastErrorPath = m_currentPath;
                SalamanderGeneral->SalMessageBox(NULL, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            }
            return TRUE;
        }
        m_lastErrorPath.clear();

        GDriveCache::CacheManager::GetInstance().PutFolder("shared_with_me_root", items);

        for (auto& item : items)
        {
            m_cachedItems.push_back(item);
            std::string subPath = m_currentPath + "/" + item.name;
            m_pathToIdCache[subPath] = item.id;

            if (item.isFolder)
            {
                AddItemToDir(dir, item.name.c_str(), true, GetCachedOrComputedFolderSize(item.id), &item.modifiedTime, pluginData);
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

    // 2c. Starred root: show all starred files & folders
    if (_stricmp(m_currentPath.c_str(), "/Starred") == 0)
    {
        std::vector<GDriveApi::GDriveItem> items;
        std::string err;
        if (!GDriveApi::ApiClient::GetInstance().ListStarred(items, &err))
        {
            if (m_lastErrorPath != m_currentPath)
            {
                m_lastErrorPath = m_currentPath;
                SalamanderGeneral->SalMessageBox(NULL, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            }
            return TRUE;
        }
        m_lastErrorPath.clear();

        GDriveCache::CacheManager::GetInstance().PutFolder("starred_root", items);

        for (auto& item : items)
        {
            m_cachedItems.push_back(item);
            std::string subPath = m_currentPath + "/" + item.name;
            m_pathToIdCache[subPath] = item.id;

            if (item.isFolder)
            {
                AddItemToDir(dir, item.name.c_str(), true, GetCachedOrComputedFolderSize(item.id), &item.modifiedTime, pluginData);
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

    // 2d. Recent root: show recent files & folders
    if (_stricmp(m_currentPath.c_str(), "/Recent") == 0)
    {
        std::vector<GDriveApi::GDriveItem> items;
        std::string err;
        if (!GDriveApi::ApiClient::GetInstance().ListRecent(items, &err))
        {
            if (m_lastErrorPath != m_currentPath)
            {
                m_lastErrorPath = m_currentPath;
                SalamanderGeneral->SalMessageBox(NULL, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            }
            return TRUE;
        }
        m_lastErrorPath.clear();

        GDriveCache::CacheManager::GetInstance().PutFolder("recent_root", items);

        for (auto& item : items)
        {
            m_cachedItems.push_back(item);
            std::string subPath = m_currentPath + "/" + item.name;
            m_pathToIdCache[subPath] = item.id;

            if (item.isFolder)
            {
                AddItemToDir(dir, item.name.c_str(), true, GetCachedOrComputedFolderSize(item.id), &item.modifiedTime, pluginData);
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

    // 2e. Trash root: show trashed files & folders
    if (_stricmp(m_currentPath.c_str(), "/Trash") == 0)
    {
        std::vector<GDriveApi::GDriveItem> items;
        std::string err;
        if (!GDriveApi::ApiClient::GetInstance().ListTrash(items, &err))
        {
            if (m_lastErrorPath != m_currentPath)
            {
                m_lastErrorPath = m_currentPath;
                SalamanderGeneral->SalMessageBox(NULL, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            }
            return TRUE;
        }
        m_lastErrorPath.clear();

        GDriveCache::CacheManager::GetInstance().PutFolder("trash_root", items);

        for (auto& item : items)
        {
            m_cachedItems.push_back(item);
            std::string subPath = m_currentPath + "/" + item.name;
            m_pathToIdCache[subPath] = item.id;

            if (item.isFolder)
            {
                AddItemToDir(dir, item.name.c_str(), true, GetCachedOrComputedFolderSize(item.id), &item.modifiedTime, pluginData);
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

    // Guard against empty or invalid folder ID before calling ListFolder
    if (m_currentFolderId.empty() || m_currentFolderId == "shared_drives_root" ||
        m_currentFolderId == "shared_with_me_root" || m_currentFolderId == "starred_root" ||
        m_currentFolderId == "recent_root" || m_currentFolderId == "trash_root")
    {
        ResolveCurrentFolderId();
        if (m_currentFolderId.empty() || m_currentFolderId == "shared_drives_root" ||
            m_currentFolderId == "shared_with_me_root" || m_currentFolderId == "starred_root" ||
            m_currentFolderId == "recent_root" || m_currentFolderId == "trash_root")
        {
            if (m_lastErrorPath != m_currentPath)
            {
                m_lastErrorPath = m_currentPath;
                SalamanderGeneral->SalMessageBox(NULL, LoadStr(IDS_ERR_PATH_NOT_FOUND), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            }
            return TRUE;
        }
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

    GDriveCache::CacheManager::GetInstance().PutFolder(m_currentFolderId, m_cachedItems);

    for (auto& item : m_cachedItems)
    {
        std::string subPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + item.name;
        m_pathToIdCache[subPath] = item.id;

        std::string ansiName = GDriveHttp::HttpClient::Utf8ToAnsi(item.name);
        for (char& c : ansiName) { if (c == '/' || c == '\\') c = '_'; }
        std::string ansiSubPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + ansiName;
        m_pathToIdCache[ansiSubPath] = item.id;

        if (item.isFolder)
        {
            AddItemToDir(dir, item.name.c_str(), true, GetCachedOrComputedFolderSize(item.id), &item.modifiedTime, pluginData);
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
           FS_SERVICE_COPYFROMDISKTOFS |
           FS_SERVICE_MOVEFROMDISKTOFS |
           FS_SERVICE_CREATEDIR |
           FS_SERVICE_QUICKRENAME |
           FS_SERVICE_DELETE |
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
    cancel = FALSE;
    if (!newName || !*newName || !file.Name) return FALSE;

    if (m_currentPath.empty() || m_currentPath == "/" || _stricmp(m_currentPath.c_str(), "/") == 0)
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_CANNOT_RENAME_ROOT), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }

    std::string fileAnsiName = file.Name;
    std::string fileUtf8Name = GDriveHttp::HttpClient::AnsiToUtf8(file.Name);

    std::string fileId;
    for (const auto& it : m_cachedItems)
    {
        if (_stricmp(it.name.c_str(), fileAnsiName.c_str()) == 0 ||
            _stricmp(it.name.c_str(), fileUtf8Name.c_str()) == 0)
        {
            fileId = it.id;
            break;
        }
    }

    if (fileId.empty())
    {
        std::string fullPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + fileAnsiName;
        auto it = m_pathToIdCache.find(fullPath);
        if (it != m_pathToIdCache.end())
        {
            fileId = it->second;
        }
        else
        {
            fullPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + fileUtf8Name;
            it = m_pathToIdCache.find(fullPath);
            if (it != m_pathToIdCache.end()) fileId = it->second;
        }
    }

    if (fileId.empty())
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_PATH_NOT_FOUND), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
        return FALSE;
    }

    std::string utf8NewName = GDriveHttp::HttpClient::AnsiToUtf8(newName);
    std::string err;
    if (!GDriveApi::ApiClient::GetInstance().RenameItem(fileId, utf8NewName, &err))
    {
        SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
        return FALSE;
    }

    std::string oldSubPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + fileAnsiName;
    std::string newSubPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + newName;
    m_pathToIdCache.erase(oldSubPath);
    m_pathToIdCache[newSubPath] = fileId;

    std::string oldUtf8SubPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + fileUtf8Name;
    std::string newUtf8SubPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + utf8NewName;
    m_pathToIdCache.erase(oldUtf8SubPath);
    m_pathToIdCache[newUtf8SubPath] = fileId;

    for (auto& it : m_cachedItems)
    {
        if (it.id == fileId)
        {
            it.name = utf8NewName;
            break;
        }
    }

    GDriveCache::CacheManager::GetInstance().RenameItem(m_currentFolderId, fileId, utf8NewName);

    return TRUE;
}

void WINAPI CPluginFS::AcceptChangeOnPathNotification(const char* fsName, const char* path, BOOL includingSubdirs)
{
}

BOOL WINAPI CPluginFS::CreateDir(const char* fsName, int mode, HWND parent, char* newName, BOOL& cancel)
{
    cancel = FALSE;
    if (!newName || !*newName) return FALSE;

    if (m_currentPath.empty() || m_currentPath == "/" || _stricmp(m_currentPath.c_str(), "/") == 0)
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_CANNOT_CREATE_DIR_ROOT), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }

    if (_stricmp(m_currentPath.c_str(), "/Shared with me") == 0)
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_CANNOT_CREATE_DIR_SHARED_WITH_ME), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }

    if (_stricmp(m_currentPath.c_str(), "/Shared Drives") == 0)
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_CANNOT_CREATE_DIR_SHARED_DRIVES), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }

    if (!ResolveCurrentFolderId() || m_currentFolderId.empty())
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_PATH_NOT_FOUND), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
        return FALSE;
    }

    std::string utf8NewName = GDriveHttp::HttpClient::AnsiToUtf8(newName);
    GDriveApi::GDriveItem newItem;
    std::string err;
    if (!GDriveApi::ApiClient::GetInstance().CreateFolder(m_currentFolderId, utf8NewName, newItem, &err))
    {
        SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
        return FALSE;
    }

    std::string subPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + newName;
    m_pathToIdCache[subPath] = newItem.id;
    std::string utf8SubPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + utf8NewName;
    m_pathToIdCache[utf8SubPath] = newItem.id;

    m_cachedItems.push_back(newItem);
    GDriveCache::CacheManager::GetInstance().AddOrUpdateItem(m_currentFolderId, newItem);

    return TRUE;
}

BOOL WINAPI CPluginFS::Delete(const char* fsName, int mode, HWND parent, int panel,
                              int selectedFiles, int selectedDirs, BOOL& cancelOrError)
{
    cancelOrError = FALSE;

    if (m_currentPath.empty() || m_currentPath == "/" || _stricmp(m_currentPath.c_str(), "/") == 0 ||
        _stricmp(m_currentPath.c_str(), "/Shared Drives") == 0 || _stricmp(m_currentPath.c_str(), "/Shared with me") == 0)
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_CANNOT_DELETE_ROOT), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        cancelOrError = TRUE;
        return FALSE;
    }

    bool shiftPressed = (GetKeyState(VK_SHIFT) < 0);

    std::vector<std::pair<std::string, std::string>> itemsToDelete; // name, id

    if (selectedFiles == 0 && selectedDirs == 0)
    {
        BOOL isDir = FALSE;
        const CFileData* f = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
        if (f && strcmp(f->Name, "..") != 0)
        {
            std::string ansiName = f->Name;
            std::string utf8Name = GDriveHttp::HttpClient::AnsiToUtf8(f->Name);
            std::string id;
            for (const auto& it : m_cachedItems)
            {
                if (_stricmp(it.name.c_str(), ansiName.c_str()) == 0 ||
                    _stricmp(it.name.c_str(), utf8Name.c_str()) == 0)
                {
                    id = it.id;
                    break;
                }
            }
            if (id.empty())
            {
                std::string fullPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + ansiName;
                auto it = m_pathToIdCache.find(fullPath);
                if (it != m_pathToIdCache.end()) id = it->second;
                else
                {
                    fullPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + utf8Name;
                    it = m_pathToIdCache.find(fullPath);
                    if (it != m_pathToIdCache.end()) id = it->second;
                }
            }
            if (!id.empty()) itemsToDelete.emplace_back(f->Name, id);
        }
    }
    else
    {
        int index = 0;
        BOOL isDir = FALSE;
        const CFileData* f = NULL;
        while ((f = SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir)) != NULL)
        {
            if (strcmp(f->Name, "..") == 0) continue;
            std::string ansiName = f->Name;
            std::string utf8Name = GDriveHttp::HttpClient::AnsiToUtf8(f->Name);
            std::string id;
            for (const auto& it : m_cachedItems)
            {
                if (_stricmp(it.name.c_str(), ansiName.c_str()) == 0 ||
                    _stricmp(it.name.c_str(), utf8Name.c_str()) == 0)
                {
                    id = it.id;
                    break;
                }
            }
            if (id.empty())
            {
                std::string fullPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + ansiName;
                auto it = m_pathToIdCache.find(fullPath);
                if (it != m_pathToIdCache.end()) id = it->second;
                else
                {
                    fullPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + utf8Name;
                    it = m_pathToIdCache.find(fullPath);
                    if (it != m_pathToIdCache.end()) id = it->second;
                }
            }
            if (!id.empty()) itemsToDelete.emplace_back(f->Name, id);
        }
    }

    if (itemsToDelete.empty())
    {
        return TRUE;
    }

    char confirmMsg[512] = {0};
    if (itemsToDelete.size() == 1)
    {
        snprintf(confirmMsg, sizeof(confirmMsg),
                 shiftPressed ? LoadStr(IDS_CONFIRM_DELETE_PERM_SINGLE) : LoadStr(IDS_CONFIRM_DELETE_TRASH_SINGLE),
                 itemsToDelete[0].first.c_str());
    }
    else
    {
        snprintf(confirmMsg, sizeof(confirmMsg),
                 shiftPressed ? LoadStr(IDS_CONFIRM_DELETE_PERM_MULTI) : LoadStr(IDS_CONFIRM_DELETE_TRASH_MULTI),
                 (int)itemsToDelete.size());
    }

    if (SalamanderGeneral->SalMessageBox(parent, confirmMsg, LoadStr(IDS_PLUGINNAME),
                                         MB_YESNO | (shiftPressed ? MB_ICONWARNING : MB_ICONQUESTION)) != IDYES)
    {
        cancelOrError = TRUE;
        return FALSE;
    }

    for (const auto& [name, id] : itemsToDelete)
    {
        std::string err;
        bool ok = shiftPressed ? GDriveApi::ApiClient::GetInstance().DeleteItem(id, &err)
                               : GDriveApi::ApiClient::GetInstance().TrashItem(id, &err);
        if (!ok)
        {
            SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            cancelOrError = TRUE;
            return FALSE;
        }

        std::string subPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + name;
        m_pathToIdCache.erase(subPath);

        for (auto it = m_cachedItems.begin(); it != m_cachedItems.end(); ++it)
        {
            if (it->id == id)
            {
                m_cachedItems.erase(it);
                break;
            }
        }

        GDriveCache::CacheManager::GetInstance().RemoveItem(m_currentFolderId, id);
    }

    return TRUE;
}

BOOL WINAPI CPluginFS::CopyOrMoveFromDiskToFS(BOOL copy, int mode, const char* fsName, HWND parent,
                                              const char* sourcePath, SalEnumSelection2 next,
                                              void* nextParam, int selectedFiles, int selectedDirs,
                                              char* targetPath, BOOL* cancelOrHandlePath)
{
    if (cancelOrHandlePath) *cancelOrHandlePath = FALSE;

    if (mode == 1)
    {
        // Pre-fill target path for Salamander Copy dialog
        if (targetPath)
        {
            std::string dest = "gdrive:\\";
            if (!m_currentPath.empty() && m_currentPath != "/")
            {
                std::string rel = m_currentPath;
                if (rel[0] == '/') rel = rel.substr(1);
                for (char& c : rel) { if (c == '/') c = '\\'; }
                dest += rel;
            }
            std::string ansiDest = GDriveHttp::HttpClient::Utf8ToAnsi(dest);
            strncpy(targetPath, ansiDest.c_str(), MAX_PATH - 1);
        }
        return FALSE; // Return FALSE so Salamander shows its standard Copy/Move dialog
    }
    if (mode == 4)
    {
        return FALSE;
    }

    if (m_currentPath.empty() || m_currentPath == "/" || _stricmp(m_currentPath.c_str(), "/") == 0)
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_CANNOT_UPLOAD_ROOT), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        if (cancelOrHandlePath) *cancelOrHandlePath = TRUE;
        return FALSE;
    }

    if (_stricmp(m_currentPath.c_str(), "/Shared with me") == 0)
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_CANNOT_CREATE_DIR_SHARED_WITH_ME), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        if (cancelOrHandlePath) *cancelOrHandlePath = TRUE;
        return FALSE;
    }

    if (_stricmp(m_currentPath.c_str(), "/Shared Drives") == 0)
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_CANNOT_CREATE_DIR_SHARED_DRIVES), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        if (cancelOrHandlePath) *cancelOrHandlePath = TRUE;
        return FALSE;
    }

    if (!ResolveCurrentFolderId() || m_currentFolderId.empty())
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_PATH_NOT_FOUND), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
        if (cancelOrHandlePath) *cancelOrHandlePath = TRUE;
        return FALSE;
    }

    std::wstring wSourcePath = GDriveHttp::HttpClient::AnsiToWide(sourcePath ? sourcePath : "");
    if (!wSourcePath.empty() && wSourcePath.back() != L'\\')
        wSourcePath += L"\\";

    const char* itemName = NULL;
    int isDir = 0;
    CQuadWord fileSize;
    DWORD attr = 0;
    FILETIME lastWriteTime = {0, 0};
    int enumError = 0;

    BOOL overallSuccess = TRUE;

    CTransferProgressDialog progressDlg(parent, true, "", 0);
    bool progressStarted = false;

    while (next(parent, 0, &itemName, &isDir, &fileSize, &attr, &lastWriteTime, nextParam, &enumError) != NULL)
    {
        if (!itemName || !*itemName) continue;
        if (strcmp(itemName, ".") == 0 || strcmp(itemName, "..") == 0)
            continue;

        if (progressDlg.IsCancelled())
        {
            overallSuccess = FALSE;
            break;
        }

        std::wstring itemLocalPath = wSourcePath + GDriveHttp::HttpClient::AnsiToWide(itemName);
        
        // Resolve long filename from disk in case itemName is DOS 8.3 short name
        std::string fileName;
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(itemLocalPath.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            fileName = GDriveHttp::HttpClient::WideToUtf8(fd.cFileName);
            FindClose(hFind);
        }
        else
        {
            fileName = GDriveHttp::HttpClient::AnsiToUtf8(itemName);
        }

        if (!progressStarted)
        {
            progressDlg.Start();
            progressStarted = true;
        }

        if (isDir)
        {
            if (!UploadFolderRecursive(itemLocalPath, fileName, m_currentFolderId, parent, &progressDlg))
            {
                overallSuccess = FALSE;
                break;
            }
        }
        else
        {
            if (!UploadSingleItem(itemLocalPath, fileName, m_currentFolderId, parent, &progressDlg))
            {
                overallSuccess = FALSE;
                break;
            }
        }

        if (!copy && overallSuccess)
        {
            // If Move operation, delete source file or directory after successful upload
            if (isDir)
            {
                SHFILEOPSTRUCTW fileOp = {0};
                fileOp.wFunc = FO_DELETE;
                std::wstring doubleNullTerm = itemLocalPath;
                doubleNullTerm.push_back(L'\0');
                fileOp.pFrom = doubleNullTerm.c_str();
                fileOp.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
                SHFileOperationW(&fileOp);
            }
            else
            {
                DeleteFileW(itemLocalPath.c_str());
            }
        }
    }

    if (progressStarted)
    {
        progressDlg.Stop();
    }

    return overallSuccess;
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

    BOOL isDir = FALSE;
    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
    int index = 0;
    const CFileData* f = focused ? SalamanderGeneral->GetPanelFocusedItem(panel, &isDir)
                                 : SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);

    const GDriveApi::GDriveItem* targetItem = nullptr;
    if (f && strcmp(f->Name, "..") != 0)
    {
        for (const auto& it : m_cachedItems)
        {
            if (_stricmp(it.name.c_str(), f->Name) == 0)
            {
                targetItem = &it;
                break;
            }
        }
    }

    bool isInsideTrash = (_stricmp(m_currentPath.c_str(), "/Trash") == 0);

    if (isInsideTrash)
    {
        if (targetItem)
        {
            AppendMenuA(hMenu, MF_STRING, CM_RESTORE_TRASH, LoadStr(IDS_MENU_RESTORE_TRASH));
            AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
        }
        AppendMenuA(hMenu, MF_STRING, CM_EMPTY_TRASH, LoadStr(IDS_MENU_EMPTY_TRASH));
    }
    else
    {
        if (targetItem)
        {
            if (!targetItem->webViewLink.empty())
            {
                AppendMenuA(hMenu, MF_STRING, CM_OPEN_IN_BROWSER, LoadStr(IDS_MENU_OPEN_IN_BROWSER));
                AppendMenuA(hMenu, MF_STRING, CM_COPY_LINK, LoadStr(IDS_MENU_COPY_LINK));
                AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
            }

            if (targetItem->isStarred)
            {
                AppendMenuA(hMenu, MF_STRING, CM_REMOVE_STAR, LoadStr(IDS_MENU_REMOVE_STAR));
            }
            else
            {
                AppendMenuA(hMenu, MF_STRING, CM_ADD_STAR, LoadStr(IDS_MENU_ADD_STAR));
            }
            AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
        }

        AppendMenuA(hMenu, MF_STRING, CM_CALC_SIZE, LoadStr(IDS_MENU_CALC_SIZE));
    }

    int cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
                             menuX, menuY, 0, parent, NULL);
    DestroyMenu(hMenu);

    if (cmd == CM_CALC_SIZE)
    {
        CalculateFolderSize(parent, panel);
    }
    else if (cmd == CM_OPEN_IN_BROWSER && targetItem && !targetItem->webViewLink.empty())
    {
        ShellExecuteA(NULL, "open", targetItem->webViewLink.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    else if (cmd == CM_COPY_LINK && targetItem && !targetItem->webViewLink.empty())
    {
        if (OpenClipboard(parent))
        {
            EmptyClipboard();
            HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, targetItem->webViewLink.length() + 1);
            if (hGlob)
            {
                char* p = (char*)GlobalLock(hGlob);
                if (p)
                {
                    strcpy(p, targetItem->webViewLink.c_str());
                    GlobalUnlock(hGlob);
                    SetClipboardData(CF_TEXT, hGlob);
                }
            }
            CloseClipboard();
            SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_LINK_COPIED), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONINFORMATION);
        }
    }
    else if ((cmd == CM_ADD_STAR || cmd == CM_REMOVE_STAR) && targetItem)
    {
        bool makeStarred = (cmd == CM_ADD_STAR);
        std::string err;
        if (GDriveApi::ApiClient::GetInstance().SetStarred(targetItem->id, makeStarred, &err))
        {
            GDriveCache::CacheManager::GetInstance().SetStarStatus(targetItem->id, makeStarred);
            SalamanderGeneral->RefreshPanelPath(panel);
        }
        else
        {
            SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
        }
    }
    else if (cmd == CM_RESTORE_TRASH && targetItem)
    {
        std::string err;
        if (GDriveApi::ApiClient::GetInstance().RestoreFromTrash(targetItem->id, &err))
        {
            GDriveCache::CacheManager::GetInstance().RemoveItem("trash_root", targetItem->id);
            SalamanderGeneral->RefreshPanelPath(panel);
        }
        else
        {
            SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
        }
    }
    else if (cmd == CM_EMPTY_TRASH)
    {
        if (SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_CONFIRM_EMPTY_TRASH), LoadStr(IDS_PLUGINNAME),
                                             MB_YESNO | MB_ICONWARNING) == IDYES)
        {
            std::string err;
            if (GDriveApi::ApiClient::GetInstance().EmptyTrash(&err))
            {
                GDriveCache::CacheManager::GetInstance().InvalidateFolder("trash_root");
                SalamanderGeneral->RefreshPanelPath(panel);
            }
            else
            {
                SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            }
        }
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

bool CPluginFS::DownloadSingleItem(const GDriveApi::GDriveItem& item, const std::wstring& targetDir, HWND parent, CTransferProgressDialog* pProgressDlg)
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

    std::wstring wFileName = GDriveHttp::HttpClient::Utf8ToWide(fileName);
    if (CfgSanitizeInvalidChars)
    {
        wFileName = GDriveHttp::HttpClient::SanitizeFileNameForLocalFsW(wFileName, (wchar_t)CfgSanitizeChar);
    }

    std::wstring localPath = targetDir;
    if (!localPath.empty() && localPath.back() != L'\\')
        localPath += L"\\";
    localPath += wFileName;

    std::unique_ptr<CTransferProgressDialog> localProgress;
    CTransferProgressDialog* activeDlg = pProgressDlg;
    if (!activeDlg)
    {
        localProgress = std::make_unique<CTransferProgressDialog>(parent, false, fileName, item.size);
        localProgress->Start();
        activeDlg = localProgress.get();
    }
    else
    {
        activeDlg->SetCurrentFile(fileName, item.size);
    }

    GDriveHttp::ProgressCallback progressCb = [activeDlg](int64_t bytesTransferred, int64_t totalBytes) -> bool {
        if (activeDlg)
        {
            return activeDlg->OnProgress(bytesTransferred, totalBytes);
        }
        return true;
    };

    bool cancelFlag = false;
    std::string err;
    bool success = GDriveApi::ApiClient::GetInstance().DownloadFile(item, localPath, progressCb, &cancelFlag, &err);

    if (localProgress)
    {
        localProgress->Stop();
    }

    if (!success && !err.empty() && (!activeDlg || !activeDlg->IsCancelled()))
    {
        SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
    }
    return success;
}

bool CPluginFS::DownloadFolderRecursive(const GDriveApi::GDriveItem& folder, const std::wstring& targetDir, HWND parent, CTransferProgressDialog* pProgressDlg)
{
    std::wstring wFolderName = GDriveHttp::HttpClient::Utf8ToWide(folder.name);
    if (CfgSanitizeInvalidChars)
    {
        wFolderName = GDriveHttp::HttpClient::SanitizeFileNameForLocalFsW(wFolderName, (wchar_t)CfgSanitizeChar);
    }

    std::wstring subDir = targetDir;
    if (!subDir.empty() && subDir.back() != L'\\')
        subDir += L"\\";
    subDir += wFolderName;

    CreateDirectoryW(subDir.c_str(), NULL);

    std::vector<GDriveApi::GDriveItem> children;
    std::string err;
    if (!GDriveApi::ApiClient::GetInstance().ListFolder(folder.id, folder.driveId, folder.isSharedDrive, children, &err))
    {
        return false;
    }

    for (const auto& child : children)
    {
        if (pProgressDlg && pProgressDlg->IsCancelled())
            return false;

        if (child.isFolder)
        {
            if (!DownloadFolderRecursive(child, subDir, parent, pProgressDlg))
                return false;
        }
        else
        {
            if (!DownloadSingleItem(child, subDir, parent, pProgressDlg))
                return false;
        }
    }
    return true;
}

bool CPluginFS::UploadSingleItem(const std::wstring& localPath, const std::string& fileName, const std::string& parentFolderId, HWND parent, CTransferProgressDialog* pProgressDlg)
{
    LARGE_INTEGER localFileSize = {0};
    HANDLE hF = CreateFileW(localPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hF != INVALID_HANDLE_VALUE)
    {
        GetFileSizeEx(hF, &localFileSize);
        CloseHandle(hF);
    }

    std::unique_ptr<CTransferProgressDialog> localProgress;
    CTransferProgressDialog* activeDlg = pProgressDlg;
    if (!activeDlg)
    {
        localProgress = std::make_unique<CTransferProgressDialog>(parent, true, fileName, localFileSize.QuadPart);
        localProgress->Start();
        activeDlg = localProgress.get();
    }
    else
    {
        activeDlg->SetCurrentFile(fileName, localFileSize.QuadPart);
    }

    GDriveHttp::ProgressCallback progressCb = [activeDlg](int64_t bytesTransferred, int64_t totalBytes) -> bool {
        if (activeDlg)
        {
            return activeDlg->OnProgress(bytesTransferred, totalBytes);
        }
        return true;
    };

    bool cancelFlag = false;
    GDriveApi::GDriveItem newItem;
    std::string err;
    bool success = GDriveApi::ApiClient::GetInstance().UploadFile(parentFolderId, localPath, fileName, "", progressCb, &cancelFlag, newItem, &err);

    if (localProgress)
    {
        localProgress->Stop();
    }

    if (!success)
    {
        if (!err.empty() && (!activeDlg || !activeDlg->IsCancelled()))
        {
            SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
        }
        return false;
    }

    std::string subPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + fileName;
    m_pathToIdCache[subPath] = newItem.id;

    std::string ansiName = GDriveHttp::HttpClient::Utf8ToAnsi(fileName);
    std::string ansiSubPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + ansiName;
    m_pathToIdCache[ansiSubPath] = newItem.id;

    m_cachedItems.push_back(newItem);

    GDriveCache::CacheManager::GetInstance().AddOrUpdateItem(parentFolderId, newItem);

    return true;
}

bool CPluginFS::UploadFolderRecursive(const std::wstring& localDirPath, const std::string& dirName, const std::string& parentFolderId, HWND parent, CTransferProgressDialog* pProgressDlg)
{
    GDriveApi::GDriveItem newFolder;
    std::string err;
    if (!GDriveApi::ApiClient::GetInstance().CreateFolder(parentFolderId, dirName, newFolder, &err))
    {
        if (!err.empty() && (!pProgressDlg || !pProgressDlg->IsCancelled()))
        {
            SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
        }
        return false;
    }

    std::string folderSubPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + dirName;
    m_pathToIdCache[folderSubPath] = newFolder.id;

    std::string ansiDirName = GDriveHttp::HttpClient::Utf8ToAnsi(dirName);
    std::string ansiFolderSubPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + ansiDirName;
    m_pathToIdCache[ansiFolderSubPath] = newFolder.id;

    GDriveCache::CacheManager::GetInstance().AddOrUpdateItem(parentFolderId, newFolder);

    std::wstring searchPattern = localDirPath;
    if (!searchPattern.empty() && searchPattern.back() != L'\\')
        searchPattern += L"\\";
    searchPattern += L"*";

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (pProgressDlg && pProgressDlg->IsCancelled())
            {
                FindClose(hFind);
                return false;
            }

            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;

            std::wstring itemLocalPath = localDirPath;
            if (!itemLocalPath.empty() && itemLocalPath.back() != L'\\')
                itemLocalPath += L"\\";
            itemLocalPath += fd.cFileName;

            std::string itemUtf8Name = GDriveHttp::HttpClient::WideToUtf8(fd.cFileName);

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (!UploadFolderRecursive(itemLocalPath, itemUtf8Name, newFolder.id, parent, pProgressDlg))
                {
                    FindClose(hFind);
                    return false;
                }
            }
            else
            {
                if (!UploadSingleItem(itemLocalPath, itemUtf8Name, newFolder.id, parent, pProgressDlg))
                {
                    FindClose(hFind);
                    return false;
                }
            }
        } while (FindNextFileW(hFind, &fd));

        FindClose(hFind);
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

    std::wstring wTargetDir = GDriveHttp::HttpClient::AnsiToWide(target);

    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
    int index = 0;
    BOOL isDir = FALSE;
    BOOL success = TRUE;

    CTransferProgressDialog progressDlg(parent, false, "", 0);
    bool progressStarted = false;

    while (true)
    {
        const CFileData* f = focused ? SalamanderGeneral->GetPanelFocusedItem(panel, &isDir)
                                     : SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);
        if (f == NULL)
            break;

        if (progressDlg.IsCancelled())
        {
            success = FALSE;
            break;
        }

        std::string fileAnsiName = f->Name;
        std::string fileUtf8Name = GDriveHttp::HttpClient::AnsiToUtf8(f->Name);

        // Find item in cache
        const GDriveApi::GDriveItem* targetItem = nullptr;
        for (const auto& item : m_cachedItems)
        {
            if (_stricmp(item.name.c_str(), fileAnsiName.c_str()) == 0 ||
                _stricmp(item.name.c_str(), fileUtf8Name.c_str()) == 0)
            {
                targetItem = &item;
                break;
            }
        }

        if (targetItem)
        {
            if (!progressStarted)
            {
                progressDlg.Start();
                progressStarted = true;
            }

            if (targetItem->isFolder)
            {
                if (!DownloadFolderRecursive(*targetItem, wTargetDir, parent, &progressDlg))
                {
                    success = FALSE;
                    break;
                }
            }
            else
            {
                if (!DownloadSingleItem(*targetItem, wTargetDir, parent, &progressDlg))
                {
                    success = FALSE;
                    break;
                }
            }
        }

        if (focused)
            break;
    }

    if (progressStarted)
    {
        progressDlg.Stop();
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

    std::string fileAnsiName = file.Name;
    std::string fileUtf8Name = GDriveHttp::HttpClient::AnsiToUtf8(file.Name);

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

        if (_stricmp(checkName.c_str(), fileAnsiName.c_str()) == 0 ||
            _stricmp(checkName.c_str(), fileUtf8Name.c_str()) == 0 ||
            _stricmp(item.name.c_str(), fileAnsiName.c_str()) == 0 ||
            _stricmp(item.name.c_str(), fileUtf8Name.c_str()) == 0)
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
        std::wstring wTempPath = GDriveHttp::HttpClient::AnsiToWide(tmpFileName);
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
        else if (_stricmp(folderName.c_str(), "Shared with me") == 0 || _stricmp(folderName.c_str(), LoadStr(IDS_SHARED_WITH_ME)) == 0)
        {
            folderId = "shared_with_me_root";
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
