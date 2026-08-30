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

    std::string up = userPart;
    if (up.empty() || up == "\\")
    {
        up = "\\My Drive";
    }

    std::string full = std::string(fsName && *fsName ? fsName : AssignedFSName) + ":" + up;
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

const GDriveApi::GDriveItem* CPluginFS::FindItemByPanelName(const char* panelName) const
{
    if (!panelName || !panelName[0] || strcmp(panelName, "..") == 0) return nullptr;

    std::string fileAnsiName = panelName;
    std::string fileUtf8Name = GDriveHttp::HttpClient::AnsiToUtf8(panelName);

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

        std::string ansiCheckName = GDriveHttp::HttpClient::Utf8ToAnsi(checkName);
        for (char& c : ansiCheckName) { if (c == '/' || c == '\\') c = '_'; }

        std::string ansiItemName = GDriveHttp::HttpClient::Utf8ToAnsi(item.name);
        for (char& c : ansiItemName) { if (c == '/' || c == '\\') c = '_'; }

        if (_stricmp(checkName.c_str(), fileAnsiName.c_str()) == 0 ||
            _stricmp(checkName.c_str(), fileUtf8Name.c_str()) == 0 ||
            _stricmp(ansiCheckName.c_str(), fileAnsiName.c_str()) == 0 ||
            _stricmp(item.name.c_str(), fileAnsiName.c_str()) == 0 ||
            _stricmp(item.name.c_str(), fileUtf8Name.c_str()) == 0 ||
            _stricmp(ansiItemName.c_str(), fileAnsiName.c_str()) == 0)
        {
            return &item;
        }
    }
    return nullptr;
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

bool CPluginFS::ResolveFolderIdForPath(const std::string& path, std::string& folderId, std::string& driveId, bool& isShared)
{
    std::string normPath = path;
    std::replace(normPath.begin(), normPath.end(), '\\', '/');
    while (normPath.size() > 1 && normPath.back() == '/') normPath.pop_back();
    if (normPath.empty() || normPath == "/")
    {
        folderId = "";
        driveId = "";
        isShared = false;
        return true;
    }

    if (_stricmp(normPath.c_str(), "/My Drive") == 0)
    {
        folderId = "root";
        driveId = "";
        isShared = false;
        return true;
    }
    if (_stricmp(normPath.c_str(), "/Shared Drives") == 0)
    {
        folderId = "shared_drives_root";
        driveId = "";
        isShared = true;
        return true;
    }
    if (_stricmp(normPath.c_str(), "/Shared with me") == 0)
    {
        folderId = "shared_with_me_root";
        driveId = "";
        isShared = false;
        return true;
    }

    auto it = m_pathToIdCache.find(normPath);
    if (it != m_pathToIdCache.end())
    {
        folderId = it->second;
        isShared = (_strnicmp(normPath.c_str(), "/Shared Drives", 14) == 0 && _stricmp(normPath.c_str(), "/Shared Drives") != 0);
        driveId = "";
        return true;
    }

    std::vector<std::string> segs;
    std::string seg;
    std::istringstream ss(normPath);
    while (std::getline(ss, seg, '/'))
    {
        if (!seg.empty()) segs.push_back(seg);
    }

    std::string accumulated = "";
    std::string parentId = "root";
    driveId = "";
    isShared = false;

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

        if (!found) return false;
    }

    folderId = parentId;
    return !folderId.empty();
}

static void AddItemToDir(CSalamanderDirectoryAbstract* dir, const char* name,
                         bool isDir, int64_t size, bool sizeValid, const FILETIME* ft,
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

    file.Size.SetUI64((unsigned __int64)(size < 0 ? 0 : size));
    if (sizeValid)
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

static bool GetCachedOrComputedFolderSize(const std::string& folderId, int64_t& sizeOut)
{
    if (folderId.empty()) return false;
    int files = 0, dirs = 0;
    if (GDriveCache::CacheManager::GetInstance().GetFolderSize(folderId, sizeOut))
    {
        return true;
    }
    if (GDriveCache::CacheManager::GetInstance().ComputeFolderSizeFromCache(folderId, sizeOut, files, dirs))
    {
        return true;
    }
    return false;
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
        AddItemToDir(dir, "..", true, 0, false, &ft, pluginData);
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

        AddItemToDir(dir, kMyDriveDir, true, 0, false, &ft, pluginData);

        if (CfgIncludeSharedDrives)
        {
            GDriveApi::GDriveItem itemShared;
            itemShared.id = "shared_drives_root";
            itemShared.name = kSharedDrivesDir;
            itemShared.isFolder = true;
            m_cachedItems.push_back(itemShared);

            AddItemToDir(dir, kSharedDrivesDir, true, 0, false, &ft, pluginData);
        }

        GDriveApi::GDriveItem itemSharedWithMe;
        itemSharedWithMe.id = "shared_with_me_root";
        itemSharedWithMe.name = kSharedWithMeDir;
        itemSharedWithMe.isFolder = true;
        m_cachedItems.push_back(itemSharedWithMe);

        AddItemToDir(dir, kSharedWithMeDir, true, 0, false, &ft, pluginData);

        GDriveApi::GDriveItem itemStarred;
        itemStarred.id = "starred_root";
        itemStarred.name = kStarredDir;
        itemStarred.isFolder = true;
        m_cachedItems.push_back(itemStarred);

        AddItemToDir(dir, kStarredDir, true, 0, false, &ft, pluginData);

        GDriveApi::GDriveItem itemRecent;
        itemRecent.id = "recent_root";
        itemRecent.name = kRecentDir;
        itemRecent.isFolder = true;
        m_cachedItems.push_back(itemRecent);

        AddItemToDir(dir, kRecentDir, true, 0, false, &ft, pluginData);

        GDriveApi::GDriveItem itemTrash;
        itemTrash.id = "trash_root";
        itemTrash.name = kTrashDir;
        itemTrash.isFolder = true;
        m_cachedItems.push_back(itemTrash);

        AddItemToDir(dir, kTrashDir, true, 0, false, &ft, pluginData);

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
        GDriveLog::Log("[PANEL] ListCurrentPath '%s' (ID: %s) -> Cache hit (%u items)",
                       m_currentPath.c_str(), m_currentFolderId.c_str(), (uint32_t)cachedList.size());
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
                int64_t folderSize = 0;
                bool hasSize = GetCachedOrComputedFolderSize(item.id, folderSize);
                AddItemToDir(dir, item.name.c_str(), true, folderSize, hasSize, &item.modifiedTime, pluginData);
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

                AddItemToDir(dir, displayName.c_str(), false, item.size, true, &item.modifiedTime, pluginData);
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

            int64_t folderSize = 0;
            bool hasSize = GetCachedOrComputedFolderSize(d.id, folderSize);
            AddItemToDir(dir, d.name.c_str(), true, folderSize, hasSize, &ft, pluginData);
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
                int64_t folderSize = 0;
                bool hasSize = GetCachedOrComputedFolderSize(item.id, folderSize);
                AddItemToDir(dir, item.name.c_str(), true, folderSize, hasSize, &item.modifiedTime, pluginData);
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

                AddItemToDir(dir, displayName.c_str(), false, item.size, true, &item.modifiedTime, pluginData);
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
                int64_t folderSize = 0;
                bool hasSize = GetCachedOrComputedFolderSize(item.id, folderSize);
                AddItemToDir(dir, item.name.c_str(), true, folderSize, hasSize, &item.modifiedTime, pluginData);
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

                AddItemToDir(dir, displayName.c_str(), false, item.size, true, &item.modifiedTime, pluginData);
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
                int64_t folderSize = 0;
                bool hasSize = GetCachedOrComputedFolderSize(item.id, folderSize);
                AddItemToDir(dir, item.name.c_str(), true, folderSize, hasSize, &item.modifiedTime, pluginData);
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

                AddItemToDir(dir, displayName.c_str(), false, item.size, true, &item.modifiedTime, pluginData);
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
                int64_t folderSize = 0;
                bool hasSize = GetCachedOrComputedFolderSize(item.id, folderSize);
                AddItemToDir(dir, item.name.c_str(), true, folderSize, hasSize, &item.modifiedTime, pluginData);
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

                AddItemToDir(dir, displayName.c_str(), false, item.size, true, &item.modifiedTime, pluginData);
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
    GDriveLog::Log("[PANEL] ListCurrentPath '%s' (ID: %s) -> API fetched %u items",
                   m_currentPath.c_str(), m_currentFolderId.c_str(), (uint32_t)m_cachedItems.size());

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
            int64_t folderSize = 0;
            bool hasSize = GetCachedOrComputedFolderSize(item.id, folderSize);
            AddItemToDir(dir, item.name.c_str(), true, folderSize, hasSize, &item.modifiedTime, pluginData);
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

            AddItemToDir(dir, displayName.c_str(), false, item.size, true, &item.modifiedTime, pluginData);
        }
    }

    return TRUE;
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
    if (!buf || bufSize <= 0) return FALSE;
    char userPart[MAX_PATH] = {0};
    GetCurrentPath(userPart);
    snprintf(buf, bufSize, "%s:%s", fsName && *fsName ? fsName : AssignedFSName, userPart);
    return TRUE;
}
