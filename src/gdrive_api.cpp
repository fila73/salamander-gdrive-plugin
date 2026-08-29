// SPDX-FileCopyrightText: 2026 Open Salamander Authors & Red Salamander Authors
// SPDX-FileContributor: Ported to Open Salamander framework by fila73
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "gdrive_api.h"
#include "gdrive_auth.h"
#include "gdrive_http.h"
#include "gdrive_json.h"

namespace GDriveApi
{

static const char* kFolderMimeType = "application/vnd.google-apps.folder";

ApiClient& ApiClient::GetInstance()
{
    static ApiClient instance;
    return instance;
}

ApiClient::ApiClient()
{
}

ApiClient::~ApiClient()
{
}

std::string ApiClient::GetToken(std::string* errorOut)
{
    return GDriveAuth::AuthManager::GetInstance().GetValidAccessToken(errorOut);
}

static std::string ExtractErrorMessage(const GDriveHttp::HttpResponse& resp)
{
    if (!resp.body.empty())
    {
        auto errJson = GDriveJson::Value::Parse(resp.body);
        if (errJson.IsObject() && errJson.Has("error"))
        {
            const auto& errObj = errJson.GetObject("error");
            if (errObj.IsObject() && errObj.Has("message"))
            {
                return errObj.GetString("message");
            }
            if (errObj.IsString())
            {
                return errObj.AsString();
            }
        }
        else if (errJson.IsObject() && errJson.Has("error_description"))
        {
            return errJson.GetString("error_description");
        }
    }
    return resp.errorMessage.empty() ? ("HTTP " + std::to_string(resp.statusCode)) : resp.errorMessage;
}

FILETIME ApiClient::Iso8601ToFileTime(const std::string& isoStr)
{
    FILETIME ft = {0, 0};
    if (isoStr.length() < 19) return ft;

    SYSTEMTIME st = {0};
    int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;
    if (sscanf(isoStr.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &min, &sec) >= 6)
    {
        st.wYear = (WORD)year;
        st.wMonth = (WORD)month;
        st.wDay = (WORD)day;
        st.wHour = (WORD)hour;
        st.wMinute = (WORD)min;
        st.wSecond = (WORD)sec;
        SystemTimeToFileTime(&st, &ft);
    }
    return ft;
}

void ApiClient::SetupGoogleDocExport(GDriveItem& item)
{
    if (item.mimeType == "application/vnd.google-apps.document")
    {
        item.isGoogleDoc = true;
        item.exportMimeType = "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
        item.exportExtension = ".docx";
    }
    else if (item.mimeType == "application/vnd.google-apps.spreadsheet")
    {
        item.isGoogleDoc = true;
        item.exportMimeType = "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
        item.exportExtension = ".xlsx";
    }
    else if (item.mimeType == "application/vnd.google-apps.presentation")
    {
        item.isGoogleDoc = true;
        item.exportMimeType = "application/vnd.openxmlformats-officedocument.presentationml.presentation";
        item.exportExtension = ".pptx";
    }
    else if (item.mimeType == "application/vnd.google-apps.drawing")
    {
        item.isGoogleDoc = true;
        item.exportMimeType = "image/png";
        item.exportExtension = ".png";
    }
    else if (item.mimeType.rfind("application/vnd.google-apps.", 0) == 0 && item.mimeType != kFolderMimeType)
    {
        item.isGoogleDoc = true;
        item.exportMimeType = "application/pdf";
        item.exportExtension = ".pdf";
    }
    else
    {
        item.isGoogleDoc = false;
        item.exportMimeType = "";
        item.exportExtension = "";
    }
}

bool ApiClient::GetAbout(AboutInfo& info, std::string* errorOut)
{
    std::string token = GetToken(errorOut);
    if (token.empty()) return false;

    GDriveHttp::HttpClient http;
    std::string url = "https://www.googleapis.com/drive/v3/about?fields=user,storageQuota";

    auto resp = http.Get(url, token);
    if (!resp.success)
    {
        if (errorOut) *errorOut = "Failed to get Google Drive information: " + ExtractErrorMessage(resp);
        return false;
    }

    auto json = GDriveJson::Value::Parse(resp.body);
    if (json.Has("user"))
    {
        const auto& u = json.GetObject("user");
        info.userName = u.GetString("displayName");
        info.userEmail = u.GetString("emailAddress");
    }

    if (json.Has("storageQuota"))
    {
        const auto& q = json.GetObject("storageQuota");
        info.quota.usage = q.GetInt64("usage");
        info.quota.limit = q.GetInt64("limit");
        info.quota.usageInDrive = q.GetInt64("usageInDrive");
        info.quota.usageInTrash = q.GetInt64("usageInDriveTrash");
    }

    return true;
}

bool ApiClient::ListSharedDrives(std::vector<GDriveItem>& drivesOut, std::string* errorOut)
{
    std::string token = GetToken(errorOut);
    if (token.empty()) return false;

    GDriveHttp::HttpClient http;
    std::string pageToken;

    while (true)
    {
        std::string url = "https://www.googleapis.com/drive/v3/drives?pageSize=100";
        if (!pageToken.empty())
        {
            url += "&pageToken=" + GDriveHttp::HttpClient::UrlEncode(pageToken);
        }

        auto resp = http.Get(url, token);
        if (!resp.success)
        {
            if (errorOut) *errorOut = "Failed to list Shared Drives: " + ExtractErrorMessage(resp);
            return false;
        }

        auto json = GDriveJson::Value::Parse(resp.body);
        if (json.Has("drives") && json.GetArray("drives").IsArray())
        {
            const auto& drivesArr = json.GetArray("drives");
            for (size_t i = 0; i < drivesArr.Size(); ++i)
            {
                const auto& d = drivesArr[i];
                GDriveItem item;
                item.id = d.GetString("id");
                item.name = d.GetString("name");
                item.isFolder = true;
                item.isSharedDrive = true;
                item.driveId = item.id;
                item.mimeType = kFolderMimeType;
                drivesOut.push_back(item);
            }
        }

        pageToken = json.GetString("nextPageToken");
        if (pageToken.empty())
            break;
    }

    return true;
}

bool ApiClient::ListFolder(const std::string& folderId,
                           const std::string& driveId,
                           bool isSharedDrive,
                           std::vector<GDriveItem>& itemsOut,
                           std::string* errorOut)
{
    std::string token = GetToken(errorOut);
    if (token.empty()) return false;

    GDriveHttp::HttpClient http;
    std::string pageToken;

    std::string q = "'" + folderId + "' in parents and trashed = false";

    while (true)
    {
        std::string url = "https://www.googleapis.com/drive/v3/files?"
                          "q=" + GDriveHttp::HttpClient::UrlEncode(q) +
                          "&fields=" + GDriveHttp::HttpClient::UrlEncode("nextPageToken,files(id,name,mimeType,size,modifiedTime)") +
                          "&pageSize=1000" +
                          "&supportsAllDrives=true" +
                          "&includeItemsFromAllDrives=true";

        if (isSharedDrive && !driveId.empty())
        {
            url += "&corpora=drive&driveId=" + GDriveHttp::HttpClient::UrlEncode(driveId);
        }

        if (!pageToken.empty())
        {
            url += "&pageToken=" + GDriveHttp::HttpClient::UrlEncode(pageToken);
        }

        auto resp = http.Get(url, token);
        if (!resp.success)
        {
            if (errorOut) *errorOut = "Failed to list Google Drive folder: " + ExtractErrorMessage(resp);
            return false;
        }

        auto json = GDriveJson::Value::Parse(resp.body);
        if (json.Has("files") && json.GetArray("files").IsArray())
        {
            const auto& filesArr = json.GetArray("files");
            for (size_t i = 0; i < filesArr.Size(); ++i)
            {
                const auto& f = filesArr[i];
                GDriveItem item;
                item.id = f.GetString("id");
                item.name = f.GetString("name");
                item.mimeType = f.GetString("mimeType");
                item.size = f.GetInt64("size", 0);
                item.isFolder = (item.mimeType == kFolderMimeType);
                item.modifiedTime = Iso8601ToFileTime(f.GetString("modifiedTime"));
                item.isSharedDrive = isSharedDrive;
                item.driveId = driveId;

                SetupGoogleDocExport(item);

                itemsOut.push_back(item);
            }
        }

        pageToken = json.GetString("nextPageToken");
        if (pageToken.empty())
            break;
    }

    return true;
}

bool ApiClient::GetFileMetadata(const std::string& fileId, GDriveItem& itemOut, std::string* errorOut)
{
    std::string token = GetToken(errorOut);
    if (token.empty()) return false;

    GDriveHttp::HttpClient http;
    std::string url = "https://www.googleapis.com/drive/v3/files/" + fileId +
                      "?fields=" + GDriveHttp::HttpClient::UrlEncode("id,name,mimeType,size,modifiedTime") +
                      "&supportsAllDrives=true";

    auto resp = http.Get(url, token);
    if (!resp.success)
    {
        if (errorOut) *errorOut = "Failed to get file metadata: " + ExtractErrorMessage(resp);
        return false;
    }

    auto json = GDriveJson::Value::Parse(resp.body);
    itemOut.id = json.GetString("id");
    itemOut.name = json.GetString("name");
    itemOut.mimeType = json.GetString("mimeType");
    itemOut.size = json.GetInt64("size", 0);
    itemOut.isFolder = (itemOut.mimeType == kFolderMimeType);
    itemOut.modifiedTime = Iso8601ToFileTime(json.GetString("modifiedTime"));

    SetupGoogleDocExport(itemOut);
    return true;
}

bool ApiClient::DownloadFile(const GDriveItem& item,
                             const std::wstring& targetLocalFilePath,
                             GDriveHttp::ProgressCallback progressCb,
                             const bool* cancelFlag,
                             std::string* errorOut)
{
    std::string token = GetToken(errorOut);
    if (token.empty()) return false;

    GDriveHttp::HttpClient http;
    std::string url;

    if (item.isGoogleDoc && !item.exportMimeType.empty())
    {
        url = "https://www.googleapis.com/drive/v3/files/" + item.id +
              "/export?mimeType=" + GDriveHttp::HttpClient::UrlEncode(item.exportMimeType);
    }
    else
    {
        url = "https://www.googleapis.com/drive/v3/files/" + item.id +
              "?alt=media&supportsAllDrives=true";
    }

    bool ok = http.DownloadToFile(url, targetLocalFilePath, token, progressCb, cancelFlag, errorOut);
    return ok;
}

} // namespace GDriveApi
