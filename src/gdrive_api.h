// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <windows.h>
#include "gdrive_http.h"

namespace GDriveApi
{

struct GDriveItem
{
    std::string id;
    std::string name;
    std::string mimeType;
    int64_t size = 0;
    FILETIME modifiedTime = {0, 0};
    bool isFolder = false;
    bool isGoogleDoc = false;
    bool isSharedDrive = false;
    bool isStarred = false;
    bool isTrashed = false;
    bool isShared = false;
    bool isOwnedByMe = true;
    std::string ownerName;
    std::string ownerEmail;
    FILETIME createdTime = {0, 0};
    int64_t version = 0;
    std::string parentId;
    std::string parentPath;
    std::string webViewLink;
    std::string webContentLink;
    std::string driveId;
    std::string exportMimeType;
    std::string exportExtension;
};

struct SearchOptions
{
    std::string queryNamed;     // Name / wildcard to search, e.g. "budget", "*.pdf"
    bool searchSubdirs = true;  // If folderScopeId is set, search subdirs or just this folder
    bool searchContent = false; // Search inside file content
    std::string queryContent;   // Content string for fullText contains '...'
    bool caseSensitive = false;
    std::string folderScopeId;  // Scope folder ID (empty = entire drive)
    std::string driveId;        // Shared drive ID if searching within a specific shared drive
    bool isSharedDrive = false;
    bool sharedWithMeOnly = false;
    std::string targetFolderPath; // Normalized target path e.g. "\My Drive\Knihy"
    int typeFilter = 0;         // 0: All, 1: Docs, 2: Sheets, 3: Slides, 4: PDF, 5: Images, 6: Folders
    bool starredOnly = false;
    bool trashedOnly = false;
};

struct StorageQuota
{
    int64_t usage = 0;
    int64_t limit = 0;
    int64_t usageInDrive = 0;
    int64_t usageInTrash = 0;
};

struct AboutInfo
{
    std::string userEmail;
    std::string userName;
    std::string userPhotoUrl;
    StorageQuota quota;
};

class ApiClient
{
public:
    static ApiClient& GetInstance();

    bool SearchFiles(const SearchOptions& opts,
                     std::vector<GDriveItem>& resultsOut,
                     const std::atomic<bool>* cancelFlag = nullptr,
                     std::string* errorOut = nullptr);

    bool GetAbout(AboutInfo& info, std::string* errorOut = nullptr);
    bool ListSharedDrives(std::vector<GDriveItem>& drivesOut, std::string* errorOut = nullptr);
    bool ListSharedWithMe(std::vector<GDriveItem>& itemsOut, std::string* errorOut = nullptr);
    bool ListStarred(std::vector<GDriveItem>& itemsOut, std::string* errorOut = nullptr);
    bool ListTrash(std::vector<GDriveItem>& itemsOut, std::string* errorOut = nullptr);
    bool ListRecent(std::vector<GDriveItem>& itemsOut, std::string* errorOut = nullptr);
    bool ListFolder(const std::string& folderId,
                    const std::string& driveId,
                    bool isSharedDrive,
                    std::vector<GDriveItem>& itemsOut,
                    std::string* errorOut = nullptr);

    bool SetStarred(const std::string& fileId, bool starred, std::string* errorOut = nullptr);
    bool RestoreFromTrash(const std::string& fileId, std::string* errorOut = nullptr);
    bool EmptyTrash(std::string* errorOut = nullptr);

    bool GetStartPageToken(std::string& tokenOut, std::string* errorOut = nullptr);
    bool GetChanges(const std::string& pageToken,
                    std::vector<std::string>& changedFolderIdsOut,
                    std::string& newStartPageTokenOut,
                    std::string* errorOut = nullptr);

    bool GetFileMetadata(const std::string& fileId, GDriveItem& itemOut, std::string* errorOut = nullptr);

    bool CreateFolder(const std::string& parentFolderId,
                      const std::string& folderName,
                      GDriveItem& itemOut,
                      std::string* errorOut = nullptr);

    bool RenameItem(const std::string& fileId,
                    const std::string& newName,
                    std::string* errorOut = nullptr);

    bool MoveItem(const std::string& fileId,
                  const std::string& previousParents,
                  const std::string& newParents,
                  std::string* errorOut = nullptr);

    bool CopyFile(const std::string& fileId,
                  const std::string& targetParentId,
                  const std::string& newName,
                  GDriveItem& itemOut,
                  std::string* errorOut = nullptr);

    bool TrashItem(const std::string& fileId,
                   std::string* errorOut = nullptr);

    bool DeleteItem(const std::string& fileId,
                    std::string* errorOut = nullptr);

    bool DownloadFile(const GDriveItem& item,
                      const std::wstring& targetLocalPath,
                      GDriveHttp::ProgressCallback progressCallback = nullptr,
                      const bool* cancelFlag = nullptr,
                      std::string* errorOut = nullptr);

    bool UploadFile(const std::string& parentFolderId,
                    const std::wstring& localFilePath,
                    const std::string& remoteFileName,
                    const std::string& mimeType,
                    GDriveHttp::ProgressCallback progressCallback,
                    const bool* cancelFlag,
                    GDriveItem& itemOut,
                    std::string* errorOut = nullptr);

    static std::string DetectMimeType(const std::string& fileName);
    static FILETIME Iso8601ToFileTime(const std::string& isoStr);
    static void SetupGoogleDocExport(GDriveItem& item);

private:
    ApiClient();
    ~ApiClient();

    std::string GetToken(std::string* errorOut);
};

} // namespace GDriveApi
