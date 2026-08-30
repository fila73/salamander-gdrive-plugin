// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <set>
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

static const char* kItemFields = "id,name,mimeType,size,modifiedTime,createdTime,starred,trashed,shared,ownedByMe,owners(displayName,emailAddress),version,webViewLink,webContentLink";

static void ParseItemFromJson(const GDriveJson::Value& f, GDriveItem& item, bool isSharedDrive = false, const std::string& driveId = "")
{
    item.id = f.GetString("id");
    item.name = f.GetString("name");
    item.mimeType = f.GetString("mimeType");
    item.size = f.GetInt64("size", 0);
    item.isFolder = (item.mimeType == kFolderMimeType);
    item.modifiedTime = ApiClient::Iso8601ToFileTime(f.GetString("modifiedTime"));
    item.createdTime = ApiClient::Iso8601ToFileTime(f.GetString("createdTime"));
    item.isStarred = f.GetBool("starred", false);
    item.isTrashed = f.GetBool("trashed", false);
    item.isShared = f.GetBool("shared", false);
    item.isOwnedByMe = f.GetBool("ownedByMe", true);
    item.version = f.GetInt64("version", 0);
    if (f.Has("owners") && f.GetArray("owners").IsArray())
    {
        const auto& owners = f.GetArray("owners");
        if (owners.Size() > 0)
        {
            item.ownerName = owners[0].GetString("displayName");
            item.ownerEmail = owners[0].GetString("emailAddress");
        }
    }
    item.webViewLink = f.GetString("webViewLink");
    item.webContentLink = f.GetString("webContentLink");
    item.isSharedDrive = isSharedDrive;
    item.driveId = driveId;

    ApiClient::SetupGoogleDocExport(item);
}

bool ApiClient::ListSharedWithMe(std::vector<GDriveItem>& itemsOut, std::string* errorOut)
{
    std::string token = GetToken(errorOut);
    if (token.empty()) return false;

    GDriveHttp::HttpClient http;
    std::string pageToken;

    std::string q = "sharedWithMe = true and trashed = false";

    while (true)
    {
        std::string url = "https://www.googleapis.com/drive/v3/files?"
                          "q=" + GDriveHttp::HttpClient::UrlEncode(q) +
                          "&fields=" + GDriveHttp::HttpClient::UrlEncode(std::string("nextPageToken,files(") + kItemFields + ")") +
                          "&pageSize=1000" +
                          "&supportsAllDrives=true" +
                          "&includeItemsFromAllDrives=true";

        if (!pageToken.empty())
        {
            url += "&pageToken=" + GDriveHttp::HttpClient::UrlEncode(pageToken);
        }

        auto resp = http.Get(url, token);
        if (!resp.success)
        {
            if (errorOut) *errorOut = "Failed to list Shared with me items: " + ExtractErrorMessage(resp);
            return false;
        }

        auto json = GDriveJson::Value::Parse(resp.body);
        if (json.Has("files") && json.GetArray("files").IsArray())
        {
            const auto& filesArr = json.GetArray("files");
            for (size_t i = 0; i < filesArr.Size(); ++i)
            {
                GDriveItem item;
                ParseItemFromJson(filesArr[i], item);
                itemsOut.push_back(item);
            }
        }

        pageToken = json.GetString("nextPageToken");
        if (pageToken.empty())
            break;
    }

    return true;
}

bool ApiClient::ListStarred(std::vector<GDriveItem>& itemsOut, std::string* errorOut)
{
    std::string token = GetToken(errorOut);
    if (token.empty()) return false;

    GDriveHttp::HttpClient http;
    std::string pageToken;

    std::string q = "starred = true and trashed = false";

    while (true)
    {
        std::string url = "https://www.googleapis.com/drive/v3/files?"
                          "q=" + GDriveHttp::HttpClient::UrlEncode(q) +
                          "&fields=" + GDriveHttp::HttpClient::UrlEncode(std::string("nextPageToken,files(") + kItemFields + ")") +
                          "&pageSize=1000" +
                          "&supportsAllDrives=true" +
                          "&includeItemsFromAllDrives=true";

        if (!pageToken.empty())
        {
            url += "&pageToken=" + GDriveHttp::HttpClient::UrlEncode(pageToken);
        }

        auto resp = http.Get(url, token);
        if (!resp.success)
        {
            if (errorOut) *errorOut = "Failed to list Starred items: " + ExtractErrorMessage(resp);
            return false;
        }

        auto json = GDriveJson::Value::Parse(resp.body);
        if (json.Has("files") && json.GetArray("files").IsArray())
        {
            const auto& filesArr = json.GetArray("files");
            for (size_t i = 0; i < filesArr.Size(); ++i)
            {
                GDriveItem item;
                ParseItemFromJson(filesArr[i], item);
                itemsOut.push_back(item);
            }
        }

        pageToken = json.GetString("nextPageToken");
        if (pageToken.empty())
            break;
    }

    return true;
}

bool ApiClient::ListTrash(std::vector<GDriveItem>& itemsOut, std::string* errorOut)
{
    std::string token = GetToken(errorOut);
    if (token.empty()) return false;

    GDriveHttp::HttpClient http;
    std::string pageToken;

    std::string q = "trashed = true";

    while (true)
    {
        std::string url = "https://www.googleapis.com/drive/v3/files?"
                          "q=" + GDriveHttp::HttpClient::UrlEncode(q) +
                          "&fields=" + GDriveHttp::HttpClient::UrlEncode(std::string("nextPageToken,files(") + kItemFields + ")") +
                          "&pageSize=1000" +
                          "&supportsAllDrives=true" +
                          "&includeItemsFromAllDrives=true";

        if (!pageToken.empty())
        {
            url += "&pageToken=" + GDriveHttp::HttpClient::UrlEncode(pageToken);
        }

        auto resp = http.Get(url, token);
        if (!resp.success)
        {
            if (errorOut) *errorOut = "Failed to list Trash items: " + ExtractErrorMessage(resp);
            return false;
        }

        auto json = GDriveJson::Value::Parse(resp.body);
        if (json.Has("files") && json.GetArray("files").IsArray())
        {
            const auto& filesArr = json.GetArray("files");
            for (size_t i = 0; i < filesArr.Size(); ++i)
            {
                GDriveItem item;
                ParseItemFromJson(filesArr[i], item);
                itemsOut.push_back(item);
            }
        }

        pageToken = json.GetString("nextPageToken");
        if (pageToken.empty())
            break;
    }

    return true;
}

bool ApiClient::ListRecent(std::vector<GDriveItem>& itemsOut, std::string* errorOut)
{
    std::string token = GetToken(errorOut);
    if (token.empty()) return false;

    GDriveHttp::HttpClient http;

    std::string q = "trashed = false";
    std::string url = "https://www.googleapis.com/drive/v3/files?"
                      "q=" + GDriveHttp::HttpClient::UrlEncode(q) +
                      "&orderBy=" + GDriveHttp::HttpClient::UrlEncode("viewedByMeTime desc,modifiedTime desc") +
                      "&fields=" + GDriveHttp::HttpClient::UrlEncode(std::string("files(") + kItemFields + ")") +
                      "&pageSize=100" +
                      "&supportsAllDrives=true" +
                      "&includeItemsFromAllDrives=true";

    auto resp = http.Get(url, token);
    if (!resp.success)
    {
        if (errorOut) *errorOut = "Failed to list Recent items: " + ExtractErrorMessage(resp);
        return false;
    }

    auto json = GDriveJson::Value::Parse(resp.body);
    if (json.Has("files") && json.GetArray("files").IsArray())
    {
        const auto& filesArr = json.GetArray("files");
        for (size_t i = 0; i < filesArr.Size(); ++i)
        {
            GDriveItem item;
            ParseItemFromJson(filesArr[i], item);
            itemsOut.push_back(item);
        }
    }

    return true;
}

bool ApiClient::SetStarred(const std::string& fileId, bool starred, std::string* errorOut)
{
    if (fileId.empty() || fileId == "root" || fileId == "shared_drives_root" || fileId == "shared_with_me_root")
    {
        if (errorOut) *errorOut = "Cannot modify star for root folder";
        return false;
    }

    std::string token = GetToken(errorOut);
    if (token.empty()) return false;

    GDriveHttp::HttpClient http;
    std::string url = "https://www.googleapis.com/drive/v3/files/" + GDriveHttp::HttpClient::UrlEncode(fileId) +
                      "?supportsAllDrives=true";

    GDriveJson::Value bodyObj;
    bodyObj.Set("starred", starred);
    std::string bodyStr = bodyObj.Serialize();

    auto resp = http.Patch(url, bodyStr, "application/json; charset=UTF-8", token);
    if (!resp.success)
    {
        if (errorOut) *errorOut = "Failed to update starred status: " + ExtractErrorMessage(resp);
        return false;
    }

    return true;
}

bool ApiClient::RestoreFromTrash(const std::string& fileId, std::string* errorOut)
{
    if (fileId.empty())
    {
        if (errorOut) *errorOut = "Invalid file ID";
        return false;
    }

    std::string token = GetToken(errorOut);
    if (token.empty()) return false;

    GDriveHttp::HttpClient http;
    std::string url = "https://www.googleapis.com/drive/v3/files/" + GDriveHttp::HttpClient::UrlEncode(fileId) +
                      "?supportsAllDrives=true";

    GDriveJson::Value bodyObj;
    bodyObj.Set("trashed", false);
    std::string bodyStr = bodyObj.Serialize();

    auto resp = http.Patch(url, bodyStr, "application/json; charset=UTF-8", token);
    if (!resp.success)
    {
        if (errorOut) *errorOut = "Failed to restore item from trash: " + ExtractErrorMessage(resp);
        return false;
    }

    return true;
}

bool ApiClient::EmptyTrash(std::string* errorOut)
{
    std::string token = GetToken(errorOut);
    if (token.empty()) return false;

    GDriveHttp::HttpClient http;
    std::string url = "https://www.googleapis.com/drive/v3/files/trash";

    auto resp = http.Delete(url, token);
    if (!resp.success)
    {
        if (errorOut) *errorOut = "Failed to empty trash: " + ExtractErrorMessage(resp);
        return false;
    }

    return true;
}

bool ApiClient::GetStartPageToken(std::string& tokenOut, std::string* errorOut)
{
    std::string token = GetToken(errorOut);
    if (token.empty()) return false;

    GDriveHttp::HttpClient http;
    std::string url = "https://www.googleapis.com/drive/v3/changes/startPageToken?supportsAllDrives=true";

    auto resp = http.Get(url, token);
    if (!resp.success)
    {
        if (errorOut) *errorOut = "Failed to get start page token: " + ExtractErrorMessage(resp);
        return false;
    }

    auto json = GDriveJson::Value::Parse(resp.body);
    tokenOut = json.GetString("startPageToken");
    return !tokenOut.empty();
}

bool ApiClient::GetChanges(const std::string& pageToken,
                          std::vector<std::string>& changedFolderIdsOut,
                          std::string& newStartPageTokenOut,
                          std::string* errorOut)
{
    if (pageToken.empty())
    {
        if (errorOut) *errorOut = "Invalid page token";
        return false;
    }

    std::string token = GetToken(errorOut);
    if (token.empty()) return false;

    GDriveHttp::HttpClient http;
    std::string currentPageToken = pageToken;
    std::set<std::string> uniqueFolderIds;

    while (true)
    {
        std::string url = "https://www.googleapis.com/drive/v3/changes?"
                          "pageToken=" + GDriveHttp::HttpClient::UrlEncode(currentPageToken) +
                          "&pageSize=1000" +
                          "&supportsAllDrives=true" +
                          "&includeItemsFromAllDrives=true" +
                          "&fields=" + GDriveHttp::HttpClient::UrlEncode("nextPageToken,newStartPageToken,changes(fileId,removed,file(id,name,mimeType,parents,trashed))");

        auto resp = http.Get(url, token);
        if (!resp.success)
        {
            if (errorOut) *errorOut = "Failed to get changes: " + ExtractErrorMessage(resp);
            return false;
        }

        auto json = GDriveJson::Value::Parse(resp.body);
        if (json.Has("changes") && json.GetArray("changes").IsArray())
        {
            const auto& changesArr = json.GetArray("changes");
            for (size_t i = 0; i < changesArr.Size(); ++i)
            {
                const auto& ch = changesArr[i];
                if (ch.Has("file") && ch.GetObject("file").IsObject())
                {
                    const auto& fileObj = ch.GetObject("file");
                    std::string mime = fileObj.GetString("mimeType");
                    std::string fId = fileObj.GetString("id");

                    if (mime == kFolderMimeType && !fId.empty())
                    {
                        uniqueFolderIds.insert(fId);
                    }

                    if (fileObj.Has("parents") && fileObj.GetArray("parents").IsArray())
                    {
                        const auto& parentsArr = fileObj.GetArray("parents");
                        for (size_t p = 0; p < parentsArr.Size(); ++p)
                        {
                            std::string pId = parentsArr[p].AsString();
                            if (!pId.empty())
                            {
                                uniqueFolderIds.insert(pId);
                            }
                        }
                    }
                }
                else
                {
                    std::string fId = ch.GetString("fileId");
                    if (!fId.empty())
                    {
                        uniqueFolderIds.insert(fId);
                    }
                }
            }
        }

        std::string newStart = json.GetString("newStartPageToken");
        if (!newStart.empty())
        {
            newStartPageTokenOut = newStart;
        }

        std::string next = json.GetString("nextPageToken");
        if (!next.empty())
        {
            currentPageToken = next;
        }
        else
        {
            break;
        }
    }

    changedFolderIdsOut.assign(uniqueFolderIds.begin(), uniqueFolderIds.end());
    return true;
}

bool ApiClient::ListFolder(const std::string& folderId,
                           const std::string& driveId,
                           bool isSharedDrive,
                           std::vector<GDriveItem>& itemsOut,
                           std::string* errorOut)
{
    if (folderId.empty() || folderId == "shared_drives_root" || folderId == "shared_with_me_root")
    {
        if (errorOut) *errorOut = "Invalid folder ID";
        return false;
    }

    std::string token = GetToken(errorOut);
    if (token.empty()) return false;

    GDriveHttp::HttpClient http;
    std::string pageToken;

    std::string q = "'" + folderId + "' in parents and trashed = false";

    while (true)
    {
        std::string url = "https://www.googleapis.com/drive/v3/files?"
                          "q=" + GDriveHttp::HttpClient::UrlEncode(q) +
                          "&fields=" + GDriveHttp::HttpClient::UrlEncode(std::string("nextPageToken,files(") + kItemFields + ")") +
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
                GDriveItem item;
                ParseItemFromJson(filesArr[i], item, isSharedDrive, driveId);
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
    std::string url = "https://www.googleapis.com/drive/v3/files/" + GDriveHttp::HttpClient::UrlEncode(fileId) +
                      "?fields=" + GDriveHttp::HttpClient::UrlEncode(kItemFields) +
                      "&supportsAllDrives=true";

    auto resp = http.Get(url, token);
    if (!resp.success)
    {
        if (errorOut) *errorOut = "Failed to get file metadata: " + ExtractErrorMessage(resp);
        return false;
    }

    auto json = GDriveJson::Value::Parse(resp.body);
    ParseItemFromJson(json, itemOut);
    return true;
}

bool ApiClient::CreateFolder(const std::string& parentFolderId,
                             const std::string& folderName,
                             GDriveItem& itemOut,
                             std::string* errorOut)
{
    std::string token = GetToken(errorOut);
    if (token.empty()) return false;

    GDriveHttp::HttpClient http;
    std::string url = "https://www.googleapis.com/drive/v3/files?supportsAllDrives=true"
                      "&fields=" + GDriveHttp::HttpClient::UrlEncode("id,name,mimeType,size,modifiedTime");

    GDriveJson::Value bodyObj;
    bodyObj.Set("name", folderName);
    bodyObj.Set("mimeType", kFolderMimeType);

    if (!parentFolderId.empty())
    {
        GDriveJson::Value parentsArr;
        parentsArr.PushBack(parentFolderId);
        bodyObj.Set("parents", parentsArr);
    }

    std::string bodyStr = bodyObj.Serialize();

    auto resp = http.Post(url, bodyStr, "application/json; charset=UTF-8", token);
    if (!resp.success)
    {
        if (errorOut) *errorOut = "Failed to create folder: " + ExtractErrorMessage(resp);
        return false;
    }

    auto json = GDriveJson::Value::Parse(resp.body);
    itemOut.id = json.GetString("id");
    itemOut.name = json.GetString("name");
    itemOut.mimeType = json.GetString("mimeType");
    itemOut.size = json.GetInt64("size", 0);
    itemOut.isFolder = true;
    itemOut.modifiedTime = Iso8601ToFileTime(json.GetString("modifiedTime"));

    return true;
}

bool ApiClient::RenameItem(const std::string& fileId,
                           const std::string& newName,
                           std::string* errorOut)
{
    if (fileId.empty() || fileId == "root" || fileId == "shared_drives_root" || fileId == "shared_with_me_root")
    {
        if (errorOut) *errorOut = "Cannot rename root folder";
        return false;
    }

    std::string token = GetToken(errorOut);
    if (token.empty()) return false;

    GDriveHttp::HttpClient http;
    std::string url = "https://www.googleapis.com/drive/v3/files/" + GDriveHttp::HttpClient::UrlEncode(fileId) +
                      "?supportsAllDrives=true";

    GDriveJson::Value bodyObj;
    bodyObj.Set("name", newName);
    std::string bodyStr = bodyObj.Serialize();

    auto resp = http.Patch(url, bodyStr, "application/json; charset=UTF-8", token);
    if (!resp.success)
    {
        if (errorOut) *errorOut = "Failed to rename item: " + ExtractErrorMessage(resp);
        return false;
    }

    return true;
}

bool ApiClient::MoveItem(const std::string& fileId,
                         const std::string& previousParents,
                         const std::string& newParents,
                         std::string* errorOut)
{
    if (fileId.empty())
    {
        if (errorOut) *errorOut = "Invalid file ID";
        return false;
    }

    std::string token = GetToken(errorOut);
    if (token.empty()) return false;

    GDriveHttp::HttpClient http;
    std::string url = "https://www.googleapis.com/drive/v3/files/" + GDriveHttp::HttpClient::UrlEncode(fileId) +
                      "?supportsAllDrives=true";

    if (!newParents.empty())
    {
        url += "&addParents=" + GDriveHttp::HttpClient::UrlEncode(newParents);
    }
    if (!previousParents.empty() && previousParents != "root" && previousParents != "shared_drives_root" && previousParents != "shared_with_me_root")
    {
        url += "&removeParents=" + GDriveHttp::HttpClient::UrlEncode(previousParents);
    }

    auto resp = http.Patch(url, "{}", "application/json; charset=UTF-8", token);
    if (!resp.success)
    {
        if (errorOut) *errorOut = "Failed to move item: " + ExtractErrorMessage(resp);
        return false;
    }

    return true;
}

bool ApiClient::CopyFile(const std::string& fileId,
                         const std::string& targetParentId,
                         const std::string& newName,
                         GDriveItem& itemOut,
                         std::string* errorOut)
{
    if (fileId.empty())
    {
        if (errorOut) *errorOut = "Invalid file ID";
        return false;
    }

    std::string token = GetToken(errorOut);
    if (token.empty()) return false;

    GDriveHttp::HttpClient http;
    std::string url = "https://www.googleapis.com/drive/v3/files/" + GDriveHttp::HttpClient::UrlEncode(fileId) + "/copy?supportsAllDrives=true";

    GDriveJson::Value bodyObj;
    if (!newName.empty()) bodyObj.Set("name", newName);
    if (!targetParentId.empty())
    {
        GDriveJson::Value parentsArr;
        parentsArr.PushBack(GDriveJson::Value(targetParentId));
        bodyObj.Set("parents", parentsArr);
    }

    std::string bodyStr = bodyObj.Serialize();
    auto resp = http.Post(url, bodyStr, "application/json; charset=UTF-8", token);
    if (!resp.success)
    {
        if (errorOut) *errorOut = "Failed to copy file: " + ExtractErrorMessage(resp);
        return false;
    }

    auto json = GDriveJson::Value::Parse(resp.body);
    ParseItemFromJson(json, itemOut);
    return true;
}

bool ApiClient::TrashItem(const std::string& fileId,
                          std::string* errorOut)
{
    if (fileId.empty() || fileId == "root" || fileId == "shared_drives_root" || fileId == "shared_with_me_root")
    {
        if (errorOut) *errorOut = "Cannot trash root folder";
        return false;
    }

    std::string token = GetToken(errorOut);
    if (token.empty()) return false;

    GDriveHttp::HttpClient http;
    std::string url = "https://www.googleapis.com/drive/v3/files/" + GDriveHttp::HttpClient::UrlEncode(fileId) +
                      "?supportsAllDrives=true";

    GDriveJson::Value bodyObj;
    bodyObj.Set("trashed", true);
    std::string bodyStr = bodyObj.Serialize();

    auto resp = http.Patch(url, bodyStr, "application/json; charset=UTF-8", token);
    if (!resp.success)
    {
        if (errorOut) *errorOut = "Failed to move item to trash: " + ExtractErrorMessage(resp);
        return false;
    }

    return true;
}

bool ApiClient::DeleteItem(const std::string& fileId,
                           std::string* errorOut)
{
    if (fileId.empty() || fileId == "root" || fileId == "shared_drives_root" || fileId == "shared_with_me_root")
    {
        if (errorOut) *errorOut = "Cannot delete root folder";
        return false;
    }

    std::string token = GetToken(errorOut);
    if (token.empty()) return false;

    GDriveHttp::HttpClient http;
    std::string url = "https://www.googleapis.com/drive/v3/files/" + GDriveHttp::HttpClient::UrlEncode(fileId) +
                      "?supportsAllDrives=true";

    auto resp = http.Delete(url, token);
    if (!resp.success)
    {
        if (errorOut) *errorOut = "Failed to permanently delete item: " + ExtractErrorMessage(resp);
        return false;
    }

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
        url = "https://www.googleapis.com/drive/v3/files/" + GDriveHttp::HttpClient::UrlEncode(item.id) +
              "/export?mimeType=" + GDriveHttp::HttpClient::UrlEncode(item.exportMimeType);
    }
    else
    {
        url = "https://www.googleapis.com/drive/v3/files/" + GDriveHttp::HttpClient::UrlEncode(item.id) +
              "?alt=media&supportsAllDrives=true";
    }

    bool ok = http.DownloadToFile(url, targetLocalFilePath, token, progressCb, cancelFlag, errorOut);
    return ok;
}

std::string ApiClient::DetectMimeType(const std::string& fileName)
{
    size_t dotPos = fileName.rfind('.');
    if (dotPos == std::string::npos) return "application/octet-stream";

    std::string ext = fileName.substr(dotPos);
    for (auto& c : ext) c = (char)tolower((unsigned char)c);

    if (ext == ".txt" || ext == ".log" || ext == ".ini" || ext == ".cfg") return "text/plain";
    if (ext == ".html" || ext == ".htm") return "text/html";
    if (ext == ".css") return "text/css";
    if (ext == ".js") return "application/javascript";
    if (ext == ".json") return "application/json";
    if (ext == ".xml") return "application/xml";
    if (ext == ".csv") return "text/csv";
    if (ext == ".pdf") return "application/pdf";
    if (ext == ".zip") return "application/zip";
    if (ext == ".rar") return "application/vnd.rar";
    if (ext == ".7z") return "application/x-7z-compressed";
    if (ext == ".tar") return "application/x-tar";
    if (ext == ".gz") return "application/gzip";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".webp") return "image/webp";
    if (ext == ".bmp") return "image/bmp";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".mp3") return "audio/mpeg";
    if (ext == ".wav") return "audio/wav";
    if (ext == ".ogg") return "audio/ogg";
    if (ext == ".mp4") return "video/mp4";
    if (ext == ".mkv") return "video/x-matroska";
    if (ext == ".avi") return "video/x-msvideo";
    if (ext == ".docx") return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
    if (ext == ".xlsx") return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
    if (ext == ".pptx") return "application/vnd.openxmlformats-officedocument.presentationml.presentation";
    if (ext == ".doc") return "application/msword";
    if (ext == ".xls") return "application/vnd.ms-excel";
    if (ext == ".ppt") return "application/vnd.ms-powerpoint";

    return "application/octet-stream";
}

bool ApiClient::UploadFile(const std::string& parentFolderId,
                          const std::wstring& localFilePath,
                          const std::string& remoteFileName,
                          const std::string& mimeType,
                          GDriveHttp::ProgressCallback progressCb,
                          const bool* cancelFlag,
                          GDriveItem& itemOut,
                          std::string* errorOut)
{
    std::string token = GetToken(errorOut);
    if (token.empty()) return false;

    GDriveHttp::HttpClient http;
    std::string url = "https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart&supportsAllDrives=true"
                      "&fields=" + GDriveHttp::HttpClient::UrlEncode("id,name,mimeType,size,modifiedTime");

    GDriveJson::Value metaObj;
    metaObj.Set("name", remoteFileName);
    if (!parentFolderId.empty())
    {
        GDriveJson::Value parentsArr;
        parentsArr.PushBack(parentFolderId);
        metaObj.Set("parents", parentsArr);
    }
    std::string metaStr = metaObj.Serialize();

    std::string contentType = mimeType.empty() ? DetectMimeType(remoteFileName) : mimeType;
    std::string responseBody;

    bool ok = http.UploadMultipartFile(url, localFilePath, metaStr, contentType, token,
                                      progressCb, cancelFlag, &responseBody, errorOut);
    if (!ok)
    {
        return false;
    }

    auto json = GDriveJson::Value::Parse(responseBody);
    itemOut.id = json.GetString("id");
    itemOut.name = json.GetString("name");
    itemOut.mimeType = json.GetString("mimeType");
    itemOut.size = json.GetInt64("size", 0);
    itemOut.isFolder = (itemOut.mimeType == kFolderMimeType);
    itemOut.modifiedTime = Iso8601ToFileTime(json.GetString("modifiedTime"));

    return true;
}

} // namespace GDriveApi
