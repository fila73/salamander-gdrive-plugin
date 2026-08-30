// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "gdrive_api.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <windows.h>

namespace GDriveCache
{

struct CachedFolder
{
    std::string folderKey;
    std::vector<GDriveApi::GDriveItem> items;
    uint64_t lastFetchedTick = 0;
    bool isValid = false;
};

class CacheManager
{
public:
    static CacheManager& GetInstance();

    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    void SetCheckIntervalMs(DWORD ms) { m_checkIntervalMs = ms; }
    DWORD GetCheckIntervalMs() const { return m_checkIntervalMs; }

    void SetSmartCtrlR(bool smart) { m_smartCtrlR = smart; }
    bool IsSmartCtrlR() const { return m_smartCtrlR; }

    void SetCurrentAccount(const std::string& email);
    const std::string& GetCurrentAccount() const { return m_currentAccountEmail; }

    // Account switching and disk persistence
    void SwitchAccount(const std::string& newAccountEmail);
    bool LoadFromDisk();
    bool SaveToDisk();
    void ClearDiskCache(const std::string& accountEmail = "");

    // Retrieve cached folder contents if valid. Returns true if cache hit.
    bool GetFolder(const std::string& folderKey, std::vector<GDriveApi::GDriveItem>& itemsOut);

    // Store folder contents in cache.
    void PutFolder(const std::string& folderKey, const std::vector<GDriveApi::GDriveItem>& items);

    // Invalidate a specific folder.
    void InvalidateFolder(const std::string& folderKey);

    // Invalidate multiple folders by their Google Drive folder IDs.
    void InvalidateFolderIds(const std::vector<std::string>& folderIds);

    // Invalidate all folders.
    void InvalidateAll();

    // Invalidate virtual folders (/Starred, /Recent, /Trash).
    void InvalidateVirtualFolders();

    // Folder size caching and calculation
    void SetFolderSize(const std::string& folderId, int64_t size);
    bool GetFolderSize(const std::string& folderId, int64_t& sizeOut);
    bool ComputeFolderSizeFromCache(const std::string& folderId, int64_t& sizeOut, int& filesCountOut, int& dirsCountOut, std::set<std::string>* pVisitedFolderIdsOut = nullptr);

    // Query Google Drive Changes API if the check interval has elapsed.
    // Returns true if changes were checked successfully (or if interval has not elapsed yet).
    bool CheckForRemoteChanges(bool forceCheck = false);

    // Immediate local cache mutations
    void AddOrUpdateItem(const std::string& folderKey, const GDriveApi::GDriveItem& item);
    void RemoveItem(const std::string& folderKey, const std::string& fileId);
    void RenameItem(const std::string& folderKey, const std::string& fileId, const std::string& newName);
    void SetStarStatus(const std::string& fileId, bool starred);

private:
    CacheManager();
    ~CacheManager();

    std::wstring GetCacheFilePath(const std::string& accountEmail) const;

    std::string m_currentAccountEmail;
    std::map<std::string, CachedFolder> m_folders;
    std::map<std::string, int64_t> m_folderSizes;
    std::string m_startPageToken;
    uint64_t m_lastChangeCheckTick = 0;
    DWORD m_checkIntervalMs = 30000; // 30 seconds default
    bool m_smartCtrlR = false; // By default Ctrl+R forces reload of current folder
    bool m_enabled = true;
    bool m_dirty = false;
    std::mutex m_mutex;
};

} // namespace GDriveCache

