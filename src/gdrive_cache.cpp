// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Open Salamander Google Drive Plugin Authors

#include "gdrive_cache.h"
#include <algorithm>

namespace GDriveCache
{

CacheManager& CacheManager::GetInstance()
{
    static CacheManager s_instance;
    return s_instance;
}

CacheManager::CacheManager()
{
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
    auto it = m_folders.find(folderKey);
    if (it != m_folders.end())
    {
        it->second.isValid = false;
    }
}

void CacheManager::InvalidateFolderIds(const std::vector<std::string>& folderIds)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& fId : folderIds)
    {
        auto it = m_folders.find(fId);
        if (it != m_folders.end())
        {
            it->second.isValid = false;
        }
    }
    // Also invalidate virtual views when changes occur
    InvalidateVirtualFolders();
}

void CacheManager::InvalidateAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& pair : m_folders)
    {
        pair.second.isValid = false;
    }
    m_startPageToken.clear();
    m_lastChangeCheckTick = 0;
}

void CacheManager::InvalidateVirtualFolders()
{
    auto itStarred = m_folders.find("starred_root");
    if (itStarred != m_folders.end()) itStarred->second.isValid = false;

    auto itRecent = m_folders.find("recent_root");
    if (itRecent != m_folders.end()) itRecent->second.isValid = false;

    auto itTrash = m_folders.find("trash_root");
    if (itTrash != m_folders.end()) itTrash->second.isValid = false;
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
        }

        if (!changedFolders.empty())
        {
            for (const auto& fId : changedFolders)
            {
                auto it = m_folders.find(fId);
                if (it != m_folders.end())
                {
                    it->second.isValid = false;
                }
            }
            InvalidateVirtualFolders();
        }
        return true;
    }

    return false;
}

void CacheManager::AddOrUpdateItem(const std::string& folderKey, const GDriveApi::GDriveItem& item)
{
    std::lock_guard<std::mutex> lock(m_mutex);
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
    }
    InvalidateVirtualFolders();
}

void CacheManager::RemoveItem(const std::string& folderKey, const std::string& fileId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_folders.find(folderKey);
    if (it != m_folders.end() && it->second.isValid)
    {
        it->second.items.erase(
            std::remove_if(it->second.items.begin(), it->second.items.end(),
                           [&fileId](const GDriveApi::GDriveItem& item) {
                               return item.id == fileId;
                           }),
            it->second.items.end());
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
    InvalidateVirtualFolders();
}

} // namespace GDriveCache
