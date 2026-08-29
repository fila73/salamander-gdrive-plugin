// SPDX-FileCopyrightText: 2026 Open Salamander Authors & Red Salamander Authors
// SPDX-FileContributor: Ported to Open Salamander framework by fila73
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
    std::string driveId;
    std::string exportMimeType;
    std::string exportExtension;
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

    bool GetAbout(AboutInfo& info, std::string* errorOut = nullptr);
    bool ListSharedDrives(std::vector<GDriveItem>& drivesOut, std::string* errorOut = nullptr);
    bool ListFolder(const std::string& folderId,
                    const std::string& driveId,
                    bool isSharedDrive,
                    std::vector<GDriveItem>& itemsOut,
                    std::string* errorOut = nullptr);

    bool GetFileMetadata(const std::string& fileId, GDriveItem& itemOut, std::string* errorOut = nullptr);

    bool DownloadFile(const GDriveItem& item,
                      const std::wstring& targetLocalPath,
                      GDriveHttp::ProgressCallback progressCallback = nullptr,
                      const bool* cancelFlag = nullptr,
                      std::string* errorOut = nullptr);

    static FILETIME Iso8601ToFileTime(const std::string& isoStr);
    static void SetupGoogleDocExport(GDriveItem& item);

private:
    ApiClient();
    ~ApiClient();

    std::string GetToken(std::string* errorOut);
};

} // namespace GDriveApi
