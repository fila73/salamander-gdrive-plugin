// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Open Salamander Google Drive Plugin Authors

#include "gdrive_cache.h"
#include "gdrive_http.h"
#include "gdrive_auth.h"
#include "gdrive_log.h"
#include <algorithm>
#include <fstream>
#include <shlobj.h>

namespace GDriveCache
{

static const uint32_t kCacheMagic = 0x43444C53; // "SLDC"
static const uint32_t kCacheVersion = 1;

static void WriteString(std::ostream& os, const std::string& s)
{
    uint32_t len = (uint32_t)s.length();
    os.write((const char*)&len, sizeof(len));
    if (len > 0)
    {
        os.write(s.data(), len);
    }
}

static std::string ReadString(std::istream& is)
{
    uint32_t len = 0;
    is.read((char*)&len, sizeof(len));
    if (len == 0 || is.fail()) return "";
    std::string s(len, '\0');
    is.read(&s[0], len);
    return s;
}

CacheManager& CacheManager::GetInstance()
{
    static CacheManager s_instance;
    return s_instance;
}

CacheManager::CacheManager()
{
}

CacheManager::~CacheManager()
{
    SaveToDisk();
}

std::wstring CacheManager::GetCacheFilePath(const std::string& accountEmail) const
{
    wchar_t appData[MAX_PATH] = {0};
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData)))
    {
        return L"";
    }

    std::wstring dir = std::wstring(appData) + L"\\Open Salamander\\plugins\\gdrive";
    CreateDirectoryW((std::wstring(appData) + L"\\Open Salamander").c_str(), NULL);
    CreateDirectoryW((std::wstring(appData) + L"\\Open Salamander\\plugins").c_str(), NULL);
    CreateDirectoryW(dir.c_str(), NULL);

    std::string safeEmail = accountEmail.empty() ? "default" : accountEmail;
    std::replace(safeEmail.begin(), safeEmail.end(), '@', '_');
    std::replace(safeEmail.begin(), safeEmail.end(), '.', '_');
    std::replace(safeEmail.begin(), safeEmail.end(), ':', '_');
    std::replace(safeEmail.begin(), safeEmail.end(), '/', '_');
    std::replace(safeEmail.begin(), safeEmail.end(), '\\', '_');

    return dir + L"\\cache_" + GDriveHttp::HttpClient::Utf8ToWide(safeEmail) + L".bin";
}

void CacheManager::SetCurrentAccount(const std::string& email)
{
    if (m_currentAccountEmail != email)
    {
        SwitchAccount(email);
    }
}

void CacheManager::SwitchAccount(const std::string& newAccountEmail)
{
    if (m_currentAccountEmail == newAccountEmail) return;

    if (!m_currentAccountEmail.empty())
    {
        SaveToDisk();
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_folders.clear();
        m_folderSizes.clear();
        m_startPageToken.clear();
        m_lastChangeCheckTick = 0;
        m_currentAccountEmail = newAccountEmail;
        m_dirty = false;
    }

    if (!newAccountEmail.empty())
    {
        LoadFromDisk();
    }
}

bool CacheManager::SaveToDisk()
{
    if (!m_enabled) return false;

    std::string email = m_currentAccountEmail;
    if (email.empty())
    {
        email = GDriveAuth::AuthManager::GetInstance().GetTokens().accountEmail;
        if (!email.empty())
        {
            m_currentAccountEmail = email;
        }
    }
    if (email.empty()) email = "default";

    std::lock_guard<std::mutex> lock(m_mutex);
    std::wstring path = GetCacheFilePath(email);
    if (path.empty()) return false;

    std::string utf8Path = GDriveHttp::HttpClient::WideToUtf8(path);
    std::ofstream os(utf8Path, std::ios::binary);
    if (!os.is_open())
    {
        GDriveLog::Log("[CACHE] Failed to open cache file for writing: %s", utf8Path.c_str());
        return false;
    }

    uint32_t magic = kCacheMagic;
    uint32_t version = kCacheVersion;
    os.write((const char*)&magic, sizeof(magic));
    os.write((const char*)&version, sizeof(version));
    WriteString(os, email);
    WriteString(os, m_startPageToken);
    os.write((const char*)&m_lastChangeCheckTick, sizeof(m_lastChangeCheckTick));

    uint32_t folderCount = (uint32_t)m_folders.size();
    os.write((const char*)&folderCount, sizeof(folderCount));

    for (const auto& [key, folder] : m_folders)
    {
        WriteString(os, folder.folderKey);
        os.write((const char*)&folder.lastFetchedTick, sizeof(folder.lastFetchedTick));
        uint8_t validByte = folder.isValid ? 1 : 0;
        os.write((const char*)&validByte, sizeof(validByte));

        uint32_t itemCount = (uint32_t)folder.items.size();
        os.write((const char*)&itemCount, sizeof(itemCount));

        for (const auto& item : folder.items)
        {
            WriteString(os, item.id);
            WriteString(os, item.name);
            WriteString(os, item.mimeType);
            os.write((const char*)&item.size, sizeof(item.size));
            os.write((const char*)&item.modifiedTime, sizeof(item.modifiedTime));
            uint8_t flags = (item.isFolder ? 1 : 0) |
                            (item.isGoogleDoc ? 2 : 0) |
                            (item.isSharedDrive ? 4 : 0) |
                            (item.isStarred ? 8 : 0) |
                            (item.isTrashed ? 16 : 0);
            os.write((const char*)&flags, sizeof(flags));
            WriteString(os, item.webViewLink);
            WriteString(os, item.webContentLink);
            WriteString(os, item.driveId);
            WriteString(os, item.exportMimeType);
            WriteString(os, item.exportExtension);
        }
    }

    uint32_t sizesCount = (uint32_t)m_folderSizes.size();
    os.write((const char*)&sizesCount, sizeof(sizesCount));
    for (const auto& [id, sz] : m_folderSizes)
    {
        WriteString(os, id);
        os.write((const char*)&sz, sizeof(sz));
    }

    m_dirty = false;
    GDriveLog::Log("[CACHE] Saved to disk for '%s': %u folders, %u sizes (path: %s)",
                   email.c_str(), folderCount, sizesCount, utf8Path.c_str());
    return true;
}

bool CacheManager::LoadFromDisk()
{
    if (!m_enabled) return false;

    std::string email = m_currentAccountEmail;
    if (email.empty())
    {
        email = GDriveAuth::AuthManager::GetInstance().GetTokens().accountEmail;
        if (!email.empty())
        {
            m_currentAccountEmail = email;
        }
    }
    if (email.empty()) email = "default";

    std::lock_guard<std::mutex> lock(m_mutex);
    std::wstring path = GetCacheFilePath(email);
    if (path.empty()) return false;

    std::string utf8Path = GDriveHttp::HttpClient::WideToUtf8(path);
    std::ifstream is(utf8Path, std::ios::binary);
    if (!is.is_open())
    {
        GDriveLog::Log("[CACHE] Cache file not found on disk: %s", utf8Path.c_str());
        return false;
    }

    uint32_t magic = 0, version = 0;
    is.read((char*)&magic, sizeof(magic));
    is.read((char*)&version, sizeof(version));

    if (magic != kCacheMagic || version != kCacheVersion)
    {
        GDriveLog::Log("[CACHE] Invalid cache file header (magic: %08X, ver: %u)", magic, version);
        return false;
    }

    std::string account = ReadString(is);
    m_startPageToken = ReadString(is);
    is.read((char*)&m_lastChangeCheckTick, sizeof(m_lastChangeCheckTick));

    uint32_t folderCount = 0;
    is.read((char*)&folderCount, sizeof(folderCount));

    m_folders.clear();

    for (uint32_t f = 0; f < folderCount && !is.eof(); ++f)
    {
        CachedFolder folder;
        folder.folderKey = ReadString(is);
        is.read((char*)&folder.lastFetchedTick, sizeof(folder.lastFetchedTick));
        uint8_t validByte = 0;
        is.read((char*)&validByte, sizeof(validByte));
        folder.isValid = (validByte != 0);

        uint32_t itemCount = 0;
        is.read((char*)&itemCount, sizeof(itemCount));

        for (uint32_t i = 0; i < itemCount && !is.eof(); ++i)
        {
            GDriveApi::GDriveItem item;
            item.id = ReadString(is);
            item.name = ReadString(is);
            item.mimeType = ReadString(is);
            is.read((char*)&item.size, sizeof(item.size));
            is.read((char*)&item.modifiedTime, sizeof(item.modifiedTime));
            uint8_t flags = 0;
            is.read((char*)&flags, sizeof(flags));
            item.isFolder = (flags & 1) != 0;
            item.isGoogleDoc = (flags & 2) != 0;
            item.isSharedDrive = (flags & 4) != 0;
            item.isStarred = (flags & 8) != 0;
            item.isTrashed = (flags & 16) != 0;
            item.webViewLink = ReadString(is);
            item.webContentLink = ReadString(is);
            item.driveId = ReadString(is);
            item.exportMimeType = ReadString(is);
            item.exportExtension = ReadString(is);

            folder.items.push_back(item);
        }

        m_folders[folder.folderKey] = folder;
    }

    uint32_t sizesCount = 0;
    if (is.read((char*)&sizesCount, sizeof(sizesCount)))
    {
        for (uint32_t s = 0; s < sizesCount && !is.eof(); ++s)
        {
            std::string sId = ReadString(is);
            int64_t sSz = 0;
            if (is.read((char*)&sSz, sizeof(sSz)))
            {
                m_folderSizes[sId] = sSz;
            }
        }
    }

    m_dirty = false;
    GDriveLog::Log("[CACHE] Loaded from disk for '%s': %u folders, %u sizes (startToken='%s')",
                   m_currentAccountEmail.c_str(), (uint32_t)m_folders.size(), (uint32_t)m_folderSizes.size(),
                   m_startPageToken.c_str());
    return true;
}

void CacheManager::ClearDiskCache(const std::string& accountEmail)
{
    std::string email = accountEmail.empty() ? m_currentAccountEmail : accountEmail;
    std::wstring path = GetCacheFilePath(email);
    if (!path.empty())
    {
        DeleteFileW(path.c_str());
    }

    if (email == m_currentAccountEmail)
    {
        InvalidateAll();
    }
}

void CacheManager::SetFolderSize(const std::string& folderId, int64_t size)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_folderSizes[folderId] = size;
    m_dirty = true;
    GDriveLog::Log("[CACHE] SetFolderSize: id='%s' -> %lld B", folderId.c_str(), (long long)size);
}

bool CacheManager::GetFolderSize(const std::string& folderId, int64_t& sizeOut)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_folderSizes.find(folderId);
    if (it != m_folderSizes.end())
    {
        sizeOut = it->second;
        return true;
    }
    return false;
}

bool CacheManager::ComputeFolderSizeFromCache(const std::string& folderId, int64_t& sizeOut, int& filesCountOut, int& dirsCountOut)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    sizeOut = 0;
    filesCountOut = 0;
    dirsCountOut = 0;

    auto itSize = m_folderSizes.find(folderId);
    if (itSize != m_folderSizes.end())
    {
        sizeOut = itSize->second;
        return true;
    }

    auto itRoot = m_folders.find(folderId);
    if (itRoot == m_folders.end() || !itRoot->second.isValid)
    {
        return false;
    }

    std::vector<std::string> queue;
    std::set<std::string> visited;
    queue.push_back(folderId);
    visited.insert(folderId);

    int64_t totalBytes = 0;
    int totalFiles = 0;
    int totalDirs = 0;
    bool allValid = true;

    size_t qIdx = 0;
    while (qIdx < queue.size())
    {
        std::string curId = queue[qIdx++];

        auto it = m_folders.find(curId);
        if (it == m_folders.end() || !it->second.isValid)
        {
            auto itKnownSize = m_folderSizes.find(curId);
            if (itKnownSize != m_folderSizes.end())
            {
                totalBytes += itKnownSize->second;
                continue;
            }
            allValid = false;
            break;
        }

        for (const auto& item : it->second.items)
        {
            if (item.isFolder)
            {
                totalDirs++;
                if (visited.find(item.id) == visited.end())
                {
                    visited.insert(item.id);
                    queue.push_back(item.id);
                }
            }
            else
            {
                totalFiles++;
                totalBytes += item.size;
            }
        }
    }

    if (allValid)
    {
        sizeOut = totalBytes;
        filesCountOut = totalFiles;
        dirsCountOut = totalDirs;
        m_folderSizes[folderId] = totalBytes;
        m_dirty = true;
        return true;
    }

    return false;
}

bool CacheManager::GetFolder(const std::string& folderKey, std::vector<GDriveApi::GDriveItem>& itemsOut)
{
    if (!m_enabled) return false;

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_folders.find(folderKey);
    if (it != m_folders.end() && it->second.isValid)
    {
        itemsOut = it->second.items;
        return true;
    }
    return false;
}

void CacheManager::PutFolder(const std::string& folderKey, const std::vector<GDriveApi::GDriveItem>& items)
{
    if (!m_enabled) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    CachedFolder& entry = m_folders[folderKey];
    entry.folderKey = folderKey;
    entry.items = items;
    entry.lastFetchedTick = GetTickCount64();
    entry.isValid = true;
    m_dirty = true;

    // Ensure we have a start page token for change tracking
    if (m_startPageToken.empty())
    {
        GDriveApi::ApiClient::GetInstance().GetStartPageToken(m_startPageToken);
        m_lastChangeCheckTick = GetTickCount64();
    }
}

void CacheManager::InvalidateFolder(const std::string& folderKey)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_folderSizes.erase(folderKey);
    auto it = m_folders.find(folderKey);
    if (it != m_folders.end())
    {
        it->second.isValid = false;
        m_dirty = true;
    }
}

void CacheManager::InvalidateFolderIds(const std::vector<std::string>& folderIds)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& fId : folderIds)
    {
        m_folderSizes.erase(fId);
        auto it = m_folders.find(fId);
        if (it != m_folders.end())
        {
            it->second.isValid = false;
            m_dirty = true;
        }
    }
    InvalidateVirtualFolders();
}

void CacheManager::InvalidateAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& pair : m_folders)
    {
        pair.second.isValid = false;
    }
    m_folderSizes.clear();
    m_startPageToken.clear();
    m_lastChangeCheckTick = 0;
    m_dirty = true;
}

void CacheManager::InvalidateVirtualFolders()
{
    auto itStarred = m_folders.find("starred_root");
    if (itStarred != m_folders.end()) { itStarred->second.isValid = false; m_dirty = true; }

    auto itRecent = m_folders.find("recent_root");
    if (itRecent != m_folders.end()) { itRecent->second.isValid = false; m_dirty = true; }

    auto itTrash = m_folders.find("trash_root");
    if (itTrash != m_folders.end()) { itTrash->second.isValid = false; m_dirty = true; }
}

bool CacheManager::CheckForRemoteChanges(bool forceCheck)
{
    if (!m_enabled) return true;

    uint64_t now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!forceCheck && (now - m_lastChangeCheckTick < m_checkIntervalMs))
        {
            // Interval has not elapsed yet, cache is fresh
            return true;
        }
    }

    if (m_startPageToken.empty())
    {
        std::string token;
        if (GDriveApi::ApiClient::GetInstance().GetStartPageToken(token))
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_startPageToken = token;
            m_lastChangeCheckTick = now;
            m_dirty = true;
        }
        return true;
    }

    std::vector<std::string> changedFolders;
    std::string newStartToken;
    std::string err;

    if (GDriveApi::ApiClient::GetInstance().GetChanges(m_startPageToken, changedFolders, newStartToken, &err))
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastChangeCheckTick = now;

        if (!newStartToken.empty())
        {
            m_startPageToken = newStartToken;
            m_dirty = true;
        }

        if (!changedFolders.empty())
        {
            GDriveLog::Log("[CHANGES] Remote changes detected: %u changed folders reported by Google Drive API", (uint32_t)changedFolders.size());
            for (const auto& fId : changedFolders)
            {
                auto it = m_folders.find(fId);
                if (it != m_folders.end())
                {
                    it->second.isValid = false;
                    GDriveLog::Log("[CHANGES] Folder cache invalidated: id='%s' (had %u cached items)", fId.c_str(), (uint32_t)it->second.items.size());
                }
                m_folderSizes.erase(fId);
            }
            InvalidateVirtualFolders();
            m_dirty = true;
        }
        else
        {
            GDriveLog::Log("[CHANGES] CheckForRemoteChanges: 0 changes reported (cache is up-to-date)");
        }
        return true;
    }
    else
    {
        GDriveLog::Log("[CHANGES] CheckForRemoteChanges failed: %s", err.c_str());
    }

    return false;
}

void CacheManager::AddOrUpdateItem(const std::string& folderKey, const GDriveApi::GDriveItem& item)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_folderSizes.erase(folderKey);
    auto it = m_folders.find(folderKey);
    if (it != m_folders.end() && it->second.isValid)
    {
        bool found = false;
        for (auto& existing : it->second.items)
        {
            if (existing.id == item.id)
            {
                existing = item;
                found = true;
                break;
            }
        }
        if (!found)
        {
            it->second.items.push_back(item);
        }
        m_dirty = true;
    }
    InvalidateVirtualFolders();
}

void CacheManager::RemoveItem(const std::string& folderKey, const std::string& fileId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_folderSizes.erase(folderKey);
    auto it = m_folders.find(folderKey);
    if (it != m_folders.end() && it->second.isValid)
    {
        it->second.items.erase(
            std::remove_if(it->second.items.begin(), it->second.items.end(),
                           [&fileId](const GDriveApi::GDriveItem& item) {
                               return item.id == fileId;
                           }),
            it->second.items.end());
        m_dirty = true;
    }
    InvalidateVirtualFolders();
}

void CacheManager::RenameItem(const std::string& folderKey, const std::string& fileId, const std::string& newName)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_folders.find(folderKey);
    if (it != m_folders.end() && it->second.isValid)
    {
        for (auto& item : it->second.items)
        {
            if (item.id == fileId)
            {
                item.name = newName;
                break;
            }
        }
        m_dirty = true;
    }
    InvalidateVirtualFolders();
}

void CacheManager::SetStarStatus(const std::string& fileId, bool starred)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& pair : m_folders)
    {
        if (pair.second.isValid)
        {
            for (auto& item : pair.second.items)
            {
                if (item.id == fileId)
                {
                    item.isStarred = starred;
                }
            }
        }
    }
    m_dirty = true;
    InvalidateVirtualFolders();
}

} // namespace GDriveCache
