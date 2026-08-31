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

    std::string inputPath = path;
    char curBuf[MAX_PATH] = {0};
    GetCurrentPath(curBuf);
    std::string current = curBuf;
    if (current.empty()) current = "\\";

    std::string resultPath;
    if (inputPath.rfind("gdrive:", 0) == 0)
    {
        resultPath = inputPath;
    }
    else if (!inputPath.empty() && (inputPath[0] == '\\' || inputPath[0] == '/'))
    {
        std::string p = inputPath;
        std::replace(p.begin(), p.end(), '/', '\\');
        resultPath = std::string(fsName && *fsName ? fsName : AssignedFSName) + ":" + p;
    }
    else
    {
        char combined[MAX_PATH] = {0};
        lstrcpynA(combined, current.c_str(), MAX_PATH);
        SalamanderGeneral->SalPathAppend(combined, inputPath.c_str(), MAX_PATH);
        resultPath = std::string(fsName && *fsName ? fsName : AssignedFSName) + ":" + combined;
    }

    lstrcpynA(path, resultPath.c_str(), pathSize);
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
    BOOL isSame = SalamanderGeneral->IsTheSamePath(cur, userPart ? userPart : "\\");
    GDriveLog::Log("[NAV] IsCurrentPath: cur='%s', userPart='%s' -> isSame=%d", cur, userPart ? userPart : "", isSame);
    return isSame;
}

BOOL WINAPI CPluginFS::IsOurPath(int currentFSNameIndex, int fsNameIndex, const char* userPart)
{
    GDriveLog::Log("[NAV] IsOurPath: currentIdx=%d, fsIdx=%d, userPart='%s'", currentFSNameIndex, fsNameIndex, userPart ? userPart : "");
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

    while (!newPath.empty() && newPath.size() > 1 && newPath[0] == '/' && newPath[1] == '/')
        newPath.erase(0, 1);

    while (newPath.size() > 1 && newPath.back() == '/')
        newPath.pop_back();

    if (newPath.empty() || newPath == "/")
        newPath = "/";
    else if (newPath[0] != '/')
        newPath = "/" + newPath;

    GDriveLog::Log("[NAV] ChangePath: userPart='%s' -> oldPath='%s', newPath='%s', mode=%d",
                   userPart ? userPart : "", oldPath.c_str(), newPath.c_str(), mode);

    m_currentPath = newPath;
    m_lastErrorPath.clear();

    if (ResolveCurrentFolderId())
    {
        GDriveLog::Log("[NAV] ChangePath resolved '%s' -> folderId='%s'", m_currentPath.c_str(), m_currentFolderId.c_str());
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

std::string CPluginFS::ExtractIdSuffix(const std::string& id)
{
    if (id.length() <= 6) return id;
    return id.substr(id.length() - 6);
}

std::string CPluginFS::ExtractSuffixFromDisambiguatedName(const std::string& name)
{
    size_t openBracket = name.rfind('[');
    size_t closeBracket = name.rfind(']');
    if (openBracket != std::string::npos && closeBracket != std::string::npos && closeBracket > openBracket + 1)
    {
        return name.substr(openBracket + 1, closeBracket - openBracket - 1);
    }
    return "";
}

std::string CPluginFS::GetBaseDisplayName(const GDriveApi::GDriveItem& item)
{
    std::string baseName = item.name;
    if (item.isGoogleDoc && !item.exportExtension.empty())
    {
        if (baseName.length() < item.exportExtension.length() ||
            baseName.compare(baseName.length() - item.exportExtension.length(), item.exportExtension.length(), item.exportExtension) != 0)
        {
            baseName += item.exportExtension;
        }
    }
    return baseName;
}

std::map<std::string, std::string> CPluginFS::ComputeDisplayNames(const std::vector<GDriveApi::GDriveItem>& items)
{
    std::map<std::string, std::string> idToDisplayName;
    std::map<std::string, int> baseNameCounts;

    for (const auto& item : items)
    {
        std::string baseName = GetBaseDisplayName(item);
        std::string lowerBaseName = baseName;
        std::transform(lowerBaseName.begin(), lowerBaseName.end(), lowerBaseName.begin(), ::tolower);
        baseNameCounts[lowerBaseName]++;
    }

    for (const auto& item : items)
    {
        std::string baseName = GetBaseDisplayName(item);
        std::string lowerBaseName = baseName;
        std::transform(lowerBaseName.begin(), lowerBaseName.end(), lowerBaseName.begin(), ::tolower);

        if (baseNameCounts[lowerBaseName] > 1 && !item.id.empty())
        {
            std::string suffix = ExtractIdSuffix(item.id);
            if (item.isFolder)
            {
                idToDisplayName[item.id] = baseName + " [" + suffix + "]";
            }
            else
            {
                size_t dotPos = baseName.rfind('.');
                if (dotPos != std::string::npos && dotPos > 0)
                {
                    idToDisplayName[item.id] = baseName.substr(0, dotPos) + " [" + suffix + "]" + baseName.substr(dotPos);
                }
                else
                {
                    idToDisplayName[item.id] = baseName + " [" + suffix + "]";
                }
            }
        }
        else
        {
            idToDisplayName[item.id] = baseName;
        }
    }

    return idToDisplayName;
}

const GDriveApi::GDriveItem* CPluginFS::FindItemByPanelName(const char* panelName) const
{
    if (!panelName || !panelName[0] || strcmp(panelName, "..") == 0) return nullptr;

    std::string fileAnsiName = panelName;
    std::string fileUtf8Name = GDriveHttp::HttpClient::AnsiToUtf8(panelName);

    // 1. If panelName contains [suffix], match item whose ID ends with suffix
    std::string suffix = ExtractSuffixFromDisambiguatedName(fileUtf8Name);
    if (!suffix.empty())
    {
        for (const auto& item : m_cachedItems)
        {
            if (item.id.length() >= suffix.length() &&
                item.id.compare(item.id.length() - suffix.length(), suffix.length(), suffix) == 0)
            {
                return &item;
            }
        }
    }

    // 2. Compute display names for cached items and match against panel name
    auto displayNames = ComputeDisplayNames(m_cachedItems);
    for (const auto& item : m_cachedItems)
    {
        auto itName = displayNames.find(item.id);
        std::string dispName = (itName != displayNames.end()) ? itName->second : GetBaseDisplayName(item);
        std::string ansiDispName = GDriveHttp::HttpClient::Utf8ToAnsi(dispName);
        for (char& c : ansiDispName) { if (c == '/' || c == '\\') c = '_'; }

        if (_stricmp(dispName.c_str(), fileAnsiName.c_str()) == 0 ||
            _stricmp(dispName.c_str(), fileUtf8Name.c_str()) == 0 ||
            _stricmp(ansiDispName.c_str(), fileAnsiName.c_str()) == 0)
        {
            return &item;
        }
    }

    // 3. Fallback: match raw/sanitized names
    for (const auto& item : m_cachedItems)
    {
        std::string checkName = GetBaseDisplayName(item);
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

static std::map<std::string, std::string, CPluginFS::CaseInsensitiveCompare> s_pathToIdCache;
static std::mutex s_pathToIdMutex;

void CPluginFS::CachePathToId(const std::string& path, const std::string& folderId)
{
    if (path.empty() || folderId.empty()) return;
    std::lock_guard<std::mutex> lock(s_pathToIdMutex);
    std::string normPath = path;
    std::replace(normPath.begin(), normPath.end(), '\\', '/');
    while (!normPath.empty() && normPath.size() > 1 && normPath[0] == '/' && normPath[1] == '/')
        normPath.erase(0, 1);
    while (normPath.size() > 1 && normPath.back() == '/') normPath.pop_back();
    if (!normPath.empty() && normPath[0] != '/') normPath = "/" + normPath;

    s_pathToIdCache[normPath] = folderId;
    std::string ansiNorm = GDriveHttp::HttpClient::Utf8ToAnsi(normPath);
    if (ansiNorm != normPath)
    {
        s_pathToIdCache[ansiNorm] = folderId;
    }
    GDriveLog::Log("[CACHE] CachePathToId: '%s' -> '%s'", normPath.c_str(), folderId.c_str());
}

bool CPluginFS::ResolveCurrentFolderId()
{
    return ResolveFolderIdForPath(m_currentPath, m_currentFolderId, m_currentDriveId, m_isSharedDrive);
}

bool CPluginFS::ResolveFolderIdForPath(const std::string& path, std::string& folderId, std::string& driveId, bool& isShared)
{
    std::lock_guard<std::mutex> lock(s_pathToIdMutex);
    std::string normPath = path;
    std::replace(normPath.begin(), normPath.end(), '\\', '/');
    while (!normPath.empty() && normPath.size() > 1 && normPath[0] == '/' && normPath[1] == '/')
        normPath.erase(0, 1);
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
        s_pathToIdCache[normPath] = "root";
        return true;
    }
    if (_stricmp(normPath.c_str(), "/Shared Drives") == 0)
    {
        folderId = "shared_drives_root";
        driveId = "";
        isShared = true;
        s_pathToIdCache[normPath] = "shared_drives_root";
        return true;
    }
    if (_stricmp(normPath.c_str(), "/Shared with me") == 0)
    {
        folderId = "shared_with_me_root";
        driveId = "";
        isShared = false;
        s_pathToIdCache[normPath] = "shared_with_me_root";
        return true;
    }
    if (_stricmp(normPath.c_str(), "/Starred") == 0)
    {
        folderId = "starred_root";
        driveId = "";
        isShared = false;
        s_pathToIdCache[normPath] = "starred_root";
        return true;
    }
    if (_stricmp(normPath.c_str(), "/Recent") == 0)
    {
        folderId = "recent_root";
        driveId = "";
        isShared = false;
        s_pathToIdCache[normPath] = "recent_root";
        return true;
    }
    if (_stricmp(normPath.c_str(), "/Trash") == 0)
    {
        folderId = "trash_root";
        driveId = "";
        isShared = false;
        s_pathToIdCache[normPath] = "trash_root";
        return true;
    }

    auto it = s_pathToIdCache.find(normPath);
    if (it != s_pathToIdCache.end())
    {
        folderId = it->second;
        isShared = (_strnicmp(normPath.c_str(), "/Shared Drives", 14) == 0 && _stricmp(normPath.c_str(), "/Shared Drives") != 0);
        driveId = "";
        GDriveLog::Log("[NAV] ResolveFolderIdForPath cached '%s' -> folderId='%s'", normPath.c_str(), folderId.c_str());
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
            s_pathToIdCache[accumulated] = "root";
            continue;
        }

        if (_stricmp(accumulated.c_str(), "/Shared Drives") == 0)
        {
            parentId = "shared_drives_root";
            isShared = true;
            s_pathToIdCache[accumulated] = "shared_drives_root";
            continue;
        }

        if (_stricmp(accumulated.c_str(), "/Shared with me") == 0)
        {
            parentId = "shared_with_me_root";
            isShared = false;
            s_pathToIdCache[accumulated] = "shared_with_me_root";
            continue;
        }

        if (_stricmp(accumulated.c_str(), "/Starred") == 0)
        {
            parentId = "starred_root";
            isShared = false;
            s_pathToIdCache[accumulated] = "starred_root";
            continue;
        }

        if (_stricmp(accumulated.c_str(), "/Recent") == 0)
        {
            parentId = "recent_root";
            isShared = false;
            s_pathToIdCache[accumulated] = "recent_root";
            continue;
        }

        if (_stricmp(accumulated.c_str(), "/Trash") == 0)
        {
            parentId = "trash_root";
            isShared = false;
            s_pathToIdCache[accumulated] = "trash_root";
            continue;
        }

        auto cached = s_pathToIdCache.find(accumulated);
        if (cached != s_pathToIdCache.end())
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

        // A. If segment has [suffix], match by ID suffix first
        std::string segSuffix = ExtractSuffixFromDisambiguatedName(segs[i]);
        if (!segSuffix.empty())
        {
            for (const auto& item : items)
            {
                if (item.isFolder && item.id.length() >= segSuffix.length() &&
                    item.id.compare(item.id.length() - segSuffix.length(), segSuffix.length(), segSuffix) == 0)
                {
                    parentId = item.id;
                    if (isShared && driveId.empty()) driveId = parentId;
                    s_pathToIdCache[accumulated] = parentId;
                    found = true;
                    break;
                }
            }
        }

        // B. Match by display name or raw name
        if (!found)
        {
            auto displayNames = ComputeDisplayNames(items);
            for (const auto& item : items)
            {
                if (!item.isFolder) continue;
                auto itDisp = displayNames.find(item.id);
                std::string disp = (itDisp != displayNames.end()) ? itDisp->second : item.name;
                std::string ansiDisp = GDriveHttp::HttpClient::Utf8ToAnsi(disp);

                if (_stricmp(disp.c_str(), segs[i].c_str()) == 0 ||
                    _stricmp(ansiDisp.c_str(), segs[i].c_str()) == 0 ||
                    _stricmp(item.name.c_str(), segs[i].c_str()) == 0 ||
                    _stricmp(GDriveHttp::HttpClient::Utf8ToAnsi(item.name).c_str(), segs[i].c_str()) == 0)
                {
                    parentId = item.id;
                    if (isShared && driveId.empty()) driveId = parentId;
                    s_pathToIdCache[accumulated] = parentId;
                    found = true;
                    break;
                }
            }
        }

        // C. Multi-segment matching for folder names containing slashes (e.g. "Archiv2021/22")
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
                        s_pathToIdCache[accumulated] = parentId;
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

    folderId = parentId;
    return !folderId.empty();
}

// Column IDs for CustomData
#define GDRIVE_COL_OWNER_ID 1
#define GDRIVE_COL_SHARED_ID 2
#define GDRIVE_COL_STARRED_ID 3
#define GDRIVE_COL_MODIFIED_BY_ID 4

// Column width state
static DWORD s_ownerWidth = MAKELONG(130, 130);
static DWORD s_ownerFixedWidth = MAKELONG(0, 0);

static DWORD s_sharedWidth = MAKELONG(60, 60);
static DWORD s_sharedFixedWidth = MAKELONG(0, 0);

static DWORD s_starredWidth = MAKELONG(40, 40);
static DWORD s_starredFixedWidth = MAKELONG(0, 0);

static DWORD s_modifiedByWidth = MAKELONG(130, 130);
static DWORD s_modifiedByFixedWidth = MAKELONG(0, 0);

// Global transfer variables provided by Salamander
static const CFileData** s_transferFileData = NULL;
static int* s_transferIsDir = NULL;
static char* s_transferBuffer = NULL;
static int* s_transferLen = NULL;
static DWORD* s_transferRowData = NULL;
static CPluginDataInterfaceAbstract** s_transferPluginDataIface = NULL;
static DWORD* s_transferActCustomData = NULL;

struct GDrivePluginFileData
{
    std::string owner;
    std::string modifiedBy;
    bool isShared = false;
    bool isStarred = false;
};

void WINAPI CGDrivePluginDataInterface::ReleasePluginData(CFileData& file, BOOL isDir)
{
    if (file.PluginData != 0)
    {
        delete reinterpret_cast<GDrivePluginFileData*>(file.PluginData);
        file.PluginData = 0;
    }
}

int WINAPI CGDrivePluginDataInterface::CompareFilesFromFS(const CFileData* file1, const CFileData* file2)
{
    if (!file1 || !file2) return 0;
    return _stricmp(file1->Name ? file1->Name : "", file2->Name ? file2->Name : "");
}

static void WINAPI GetOwnerColumnText()
{
    if (s_transferFileData && *s_transferFileData && (*s_transferFileData)->PluginData)
    {
        auto* pData = reinterpret_cast<const GDrivePluginFileData*>((*s_transferFileData)->PluginData);
        std::string text = pData->owner;
        if (text.empty() && CfgOwnerFallbackToModifier)
        {
            text = pData->modifiedBy;
        }

        if (!text.empty())
        {
            std::string ansi = GDriveHttp::HttpClient::Utf8ToAnsi(text);
            int len = (int)ansi.length();
            if (len > TRANSFER_BUFFER_MAX - 1) len = TRANSFER_BUFFER_MAX - 1;
            memcpy(s_transferBuffer, ansi.data(), len);
            *s_transferLen = len;
            return;
        }
    }
    *s_transferLen = 0;
}

static void WINAPI GetModifiedByColumnText()
{
    if (s_transferFileData && *s_transferFileData && (*s_transferFileData)->PluginData)
    {
        auto* pData = reinterpret_cast<const GDrivePluginFileData*>((*s_transferFileData)->PluginData);
        if (!pData->modifiedBy.empty())
        {
            std::string ansi = GDriveHttp::HttpClient::Utf8ToAnsi(pData->modifiedBy);
            int len = (int)ansi.length();
            if (len > TRANSFER_BUFFER_MAX - 1) len = TRANSFER_BUFFER_MAX - 1;
            memcpy(s_transferBuffer, ansi.data(), len);
            *s_transferLen = len;
            return;
        }
    }
    *s_transferLen = 0;
}

static void WINAPI GetSharedColumnText()
{
    if (s_transferFileData && *s_transferFileData && (*s_transferFileData)->PluginData)
    {
        auto* pData = reinterpret_cast<const GDrivePluginFileData*>((*s_transferFileData)->PluginData);
        if (pData->isShared)
        {
            const char* str = LoadStr(IDS_YES);
            int len = (int)strlen(str);
            if (len > TRANSFER_BUFFER_MAX - 1) len = TRANSFER_BUFFER_MAX - 1;
            memcpy(s_transferBuffer, str, len);
            *s_transferLen = len;
            return;
        }
        else
        {
            const char* str = LoadStr(IDS_NO);
            int len = (int)strlen(str);
            if (len > TRANSFER_BUFFER_MAX - 1) len = TRANSFER_BUFFER_MAX - 1;
            memcpy(s_transferBuffer, str, len);
            *s_transferLen = len;
            return;
        }
    }
    *s_transferLen = 0;
}

static void WINAPI GetStarredColumnText()
{
    if (s_transferFileData && *s_transferFileData && (*s_transferFileData)->PluginData)
    {
        auto* pData = reinterpret_cast<const GDrivePluginFileData*>((*s_transferFileData)->PluginData);
        if (pData->isStarred)
        {
            const char* starStr = "*";
            int len = (int)strlen(starStr);
            memcpy(s_transferBuffer, starStr, len);
            *s_transferLen = len;
            return;
        }
    }
    *s_transferLen = 0;
}

void WINAPI CGDrivePluginDataInterface::SetupView(BOOL leftPanel, CSalamanderViewAbstract* view,
                                                  const char* archivePath, const CFileData* upperDir)
{
    view->GetTransferVariables(s_transferFileData, s_transferIsDir, s_transferBuffer, s_transferLen,
                               s_transferRowData, s_transferPluginDataIface, s_transferActCustomData);

    if (view->GetViewMode() == VIEW_MODE_DETAILED)
    {
        int count = view->GetColumnsCount();

        // Add Starred column
        CColumn colStarred;
        memset(&colStarred, 0, sizeof(colStarred));
        lstrcpynA(colStarred.Name, LoadStr(IDS_COL_STARRED), sizeof(colStarred.Name));
        lstrcpynA(colStarred.Description, LoadStr(IDS_COL_STARRED_DESC), sizeof(colStarred.Description));
        colStarred.GetText = GetStarredColumnText;
        colStarred.CustomData = GDRIVE_COL_STARRED_ID;
        colStarred.SupportSorting = 1;
        colStarred.LeftAlignment = 0;
        colStarred.ID = COLUMN_ID_CUSTOM;
        colStarred.Width = leftPanel ? LOWORD(s_starredWidth) : HIWORD(s_starredWidth);
        colStarred.FixedWidth = leftPanel ? LOWORD(s_starredFixedWidth) : HIWORD(s_starredFixedWidth);
        view->InsertColumn(count++, &colStarred);

        // Add Shared column
        CColumn colShared;
        memset(&colShared, 0, sizeof(colShared));
        lstrcpynA(colShared.Name, LoadStr(IDS_COL_SHARED), sizeof(colShared.Name));
        lstrcpynA(colShared.Description, LoadStr(IDS_COL_SHARED_DESC), sizeof(colShared.Description));
        colShared.GetText = GetSharedColumnText;
        colShared.CustomData = GDRIVE_COL_SHARED_ID;
        colShared.SupportSorting = 1;
        colShared.LeftAlignment = 1;
        colShared.ID = COLUMN_ID_CUSTOM;
        colShared.Width = leftPanel ? LOWORD(s_sharedWidth) : HIWORD(s_sharedWidth);
        colShared.FixedWidth = leftPanel ? LOWORD(s_sharedFixedWidth) : HIWORD(s_sharedFixedWidth);
        view->InsertColumn(count++, &colShared);

        // Add Owner column
        CColumn colOwner;
        memset(&colOwner, 0, sizeof(colOwner));
        lstrcpynA(colOwner.Name, LoadStr(IDS_COL_OWNER), sizeof(colOwner.Name));
        lstrcpynA(colOwner.Description, LoadStr(IDS_COL_OWNER_DESC), sizeof(colOwner.Description));
        colOwner.GetText = GetOwnerColumnText;
        colOwner.CustomData = GDRIVE_COL_OWNER_ID;
        colOwner.SupportSorting = 1;
        colOwner.LeftAlignment = 1;
        colOwner.ID = COLUMN_ID_CUSTOM;
        colOwner.Width = leftPanel ? LOWORD(s_ownerWidth) : HIWORD(s_ownerWidth);
        colOwner.FixedWidth = leftPanel ? LOWORD(s_ownerFixedWidth) : HIWORD(s_ownerFixedWidth);
        view->InsertColumn(count++, &colOwner);

        // Add Modified By column
        CColumn colModifiedBy;
        memset(&colModifiedBy, 0, sizeof(colModifiedBy));
        lstrcpynA(colModifiedBy.Name, LoadStr(IDS_COL_MODIFIED_BY), sizeof(colModifiedBy.Name));
        lstrcpynA(colModifiedBy.Description, LoadStr(IDS_COL_MODIFIED_BY_DESC), sizeof(colModifiedBy.Description));
        colModifiedBy.GetText = GetModifiedByColumnText;
        colModifiedBy.CustomData = GDRIVE_COL_MODIFIED_BY_ID;
        colModifiedBy.SupportSorting = 1;
        colModifiedBy.LeftAlignment = 1;
        colModifiedBy.ID = COLUMN_ID_CUSTOM;
        colModifiedBy.Width = leftPanel ? LOWORD(s_modifiedByWidth) : HIWORD(s_modifiedByWidth);
        colModifiedBy.FixedWidth = leftPanel ? LOWORD(s_modifiedByFixedWidth) : HIWORD(s_modifiedByFixedWidth);
        view->InsertColumn(count++, &colModifiedBy);
    }
}

void WINAPI CGDrivePluginDataInterface::ColumnFixedWidthShouldChange(BOOL leftPanel, const CColumn* column, int newFixedWidth)
{
    if (!column) return;
    if (leftPanel)
    {
        switch (column->CustomData)
        {
        case GDRIVE_COL_OWNER_ID:
            s_ownerFixedWidth = MAKELONG(newFixedWidth, HIWORD(s_ownerFixedWidth));
            break;
        case GDRIVE_COL_SHARED_ID:
            s_sharedFixedWidth = MAKELONG(newFixedWidth, HIWORD(s_sharedFixedWidth));
            break;
        case GDRIVE_COL_STARRED_ID:
            s_starredFixedWidth = MAKELONG(newFixedWidth, HIWORD(s_starredFixedWidth));
            break;
        case GDRIVE_COL_MODIFIED_BY_ID:
            s_modifiedByFixedWidth = MAKELONG(newFixedWidth, HIWORD(s_modifiedByFixedWidth));
            break;
        }
    }
    else
    {
        switch (column->CustomData)
        {
        case GDRIVE_COL_OWNER_ID:
            s_ownerFixedWidth = MAKELONG(LOWORD(s_ownerFixedWidth), newFixedWidth);
            break;
        case GDRIVE_COL_SHARED_ID:
            s_sharedFixedWidth = MAKELONG(LOWORD(s_sharedFixedWidth), newFixedWidth);
            break;
        case GDRIVE_COL_STARRED_ID:
            s_starredFixedWidth = MAKELONG(LOWORD(s_starredFixedWidth), newFixedWidth);
            break;
        case GDRIVE_COL_MODIFIED_BY_ID:
            s_modifiedByFixedWidth = MAKELONG(LOWORD(s_modifiedByFixedWidth), newFixedWidth);
            break;
        }
    }
    if (newFixedWidth)
    {
        ColumnWidthWasChanged(leftPanel, column, column->Width);
    }
}

void WINAPI CGDrivePluginDataInterface::ColumnWidthWasChanged(BOOL leftPanel, const CColumn* column, int newWidth)
{
    if (!column) return;
    if (leftPanel)
    {
        switch (column->CustomData)
        {
        case GDRIVE_COL_OWNER_ID:
            s_ownerWidth = MAKELONG(newWidth, HIWORD(s_ownerWidth));
            break;
        case GDRIVE_COL_SHARED_ID:
            s_sharedWidth = MAKELONG(newWidth, HIWORD(s_sharedWidth));
            break;
        case GDRIVE_COL_STARRED_ID:
            s_starredWidth = MAKELONG(newWidth, HIWORD(s_starredWidth));
            break;
        case GDRIVE_COL_MODIFIED_BY_ID:
            s_modifiedByWidth = MAKELONG(newWidth, HIWORD(s_modifiedByWidth));
            break;
        }
    }
    else
    {
        switch (column->CustomData)
        {
        case GDRIVE_COL_OWNER_ID:
            s_ownerWidth = MAKELONG(LOWORD(s_ownerWidth), newWidth);
            break;
        case GDRIVE_COL_SHARED_ID:
            s_sharedWidth = MAKELONG(LOWORD(s_sharedWidth), newWidth);
            break;
        case GDRIVE_COL_STARRED_ID:
            s_starredWidth = MAKELONG(LOWORD(s_starredWidth), newWidth);
            break;
        case GDRIVE_COL_MODIFIED_BY_ID:
            s_modifiedByWidth = MAKELONG(LOWORD(s_modifiedByWidth), newWidth);
            break;
        }
    }
}

static void AddItemToDir(CSalamanderDirectoryAbstract* dir, const char* name,
                         bool isDir, int64_t size, bool sizeValid, const FILETIME* ft,
                         CPluginDataInterfaceAbstract* pluginData,
                         const GDriveApi::GDriveItem* item = nullptr)
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

    if (item)
    {
        auto* pData = new GDrivePluginFileData();
        pData->owner = item->ownerName.empty() ? item->ownerEmail : item->ownerName;
        pData->modifiedBy = item->lastModifyingUserName.empty() ? item->lastModifyingUserEmail : item->lastModifyingUserName;
        pData->isShared = item->isShared;
        pData->isStarred = item->isStarred;
        file.PluginData = reinterpret_cast<DWORD_PTR>(pData);
    }

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

static void PopulateDirFromItems(CSalamanderDirectoryAbstract* dir,
                                 const std::string& currentPath,
                                 const std::vector<GDriveApi::GDriveItem>& items,
                                 std::vector<GDriveApi::GDriveItem>& cachedItemsOut,
                                 std::map<std::string, std::string, CPluginFS::CaseInsensitiveCompare>& pathToIdCacheOut,
                                 CPluginDataInterfaceAbstract* pluginData)
{
    auto displayNames = CPluginFS::ComputeDisplayNames(items);

    for (const auto& item : items)
    {
        cachedItemsOut.push_back(item);
        auto itDisp = displayNames.find(item.id);
        std::string displayName = (itDisp != displayNames.end()) ? itDisp->second : CPluginFS::GetBaseDisplayName(item);

        std::string subPath = (currentPath == "/" ? "" : currentPath) + "/" + displayName;
        pathToIdCacheOut[subPath] = item.id;

        std::string ansiName = GDriveHttp::HttpClient::Utf8ToAnsi(displayName);
        for (char& c : ansiName) { if (c == '/' || c == '\\') c = '_'; }
        std::string ansiSubPath = (currentPath == "/" ? "" : currentPath) + "/" + ansiName;
        pathToIdCacheOut[ansiSubPath] = item.id;

        // Also map raw item name as fallback
        std::string rawSubPath = (currentPath == "/" ? "" : currentPath) + "/" + item.name;
        pathToIdCacheOut[rawSubPath] = item.id;

        if (item.isFolder)
        {
            int64_t folderSize = 0;
            bool hasSize = GetCachedOrComputedFolderSize(item.id, folderSize);
            AddItemToDir(dir, displayName.c_str(), true, folderSize, hasSize, &item.modifiedTime, pluginData, &item);
        }
        else
        {
            AddItemToDir(dir, displayName.c_str(), false, item.size, true, &item.modifiedTime, pluginData, &item);
        }
    }
}

BOOL WINAPI CPluginFS::ListCurrentPath(CSalamanderDirectoryAbstract* dir,
                                      CPluginDataInterfaceAbstract*& pluginData,
                                      int& iconsType, BOOL forceRefresh)
{
    if (!dir) return FALSE;

    iconsType = pitFromRegistry;
    pluginData = &m_pluginDataInterface;
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
        PopulateDirFromItems(dir, m_currentPath, cachedList, m_cachedItems, m_pathToIdCache, pluginData);
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
        PopulateDirFromItems(dir, m_currentPath, drives, m_cachedItems, m_pathToIdCache, pluginData);
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
        PopulateDirFromItems(dir, m_currentPath, items, m_cachedItems, m_pathToIdCache, pluginData);
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
        PopulateDirFromItems(dir, m_currentPath, items, m_cachedItems, m_pathToIdCache, pluginData);
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
        PopulateDirFromItems(dir, m_currentPath, items, m_cachedItems, m_pathToIdCache, pluginData);
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
        PopulateDirFromItems(dir, m_currentPath, items, m_cachedItems, m_pathToIdCache, pluginData);
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

    std::vector<GDriveApi::GDriveItem> fetchedItems = m_cachedItems;
    m_cachedItems.clear();
    PopulateDirFromItems(dir, m_currentPath, fetchedItems, m_cachedItems, m_pathToIdCache, pluginData);

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
