// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <cstdint>
#include <windows.h>
#include <winhttp.h>

namespace GDriveHttp
{

struct HttpResponse
{
    int statusCode = 0;
    std::string body;
    std::map<std::string, std::string> headers;
    bool success = false;
    std::string errorMessage;
};

using ProgressCallback = std::function<bool(int64_t bytesTransferred, int64_t totalBytes)>;

class HttpClient
{
public:
    HttpClient();
    ~HttpClient();

    bool Initialize(const std::wstring& userAgent = L"OpenSalamander-GDrive/1.0");
    void Release();

    HttpResponse Get(const std::string& url,
                     const std::string& bearerToken = "",
                     const std::map<std::string, std::string>& customHeaders = {});

    HttpResponse Post(const std::string& url,
                      const std::string& body,
                      const std::string& contentType = "application/x-www-form-urlencoded",
                      const std::string& bearerToken = "",
                      const std::map<std::string, std::string>& customHeaders = {});

    HttpResponse Patch(const std::string& url,
                       const std::string& body,
                       const std::string& contentType = "application/json; charset=UTF-8",
                       const std::string& bearerToken = "",
                       const std::map<std::string, std::string>& customHeaders = {});

    HttpResponse Delete(const std::string& url,
                        const std::string& bearerToken = "",
                        const std::map<std::string, std::string>& customHeaders = {});

    bool DownloadToFile(const std::string& url,
                        const std::wstring& targetLocalPath,
                        const std::string& bearerToken = "",
                        ProgressCallback progressCallback = nullptr,
                        const bool* cancelFlag = nullptr,
                        std::string* errorOut = nullptr);

    bool UploadMultipartFile(const std::string& url,
                             const std::wstring& localFilePath,
                             const std::string& metadataJson,
                             const std::string& fileContentType,
                             const std::string& bearerToken = "",
                             ProgressCallback progressCallback = nullptr,
                             const bool* cancelFlag = nullptr,
                             std::string* responseBodyOut = nullptr,
                             std::string* errorOut = nullptr);

    static std::string UrlEncode(const std::string& str);
    static std::wstring Utf8ToWide(const std::string& utf8);
    static std::string WideToUtf8(const std::wstring& wide);
    static std::string AnsiToUtf8(const std::string& ansi);
    static std::string Utf8ToAnsi(const std::string& utf8);
    static std::wstring AnsiToWide(const std::string& ansi);
    static std::string WideToAnsi(const std::wstring& wide);

    static std::wstring SanitizeFileNameForLocalFsW(const std::wstring& name, wchar_t replacementChar = L'_');
    static std::string SanitizeFileNameForLocalFs(const std::string& name, char replacementChar = '_');

private:
    HINTERNET m_hSession = NULL;

    HttpResponse ExecuteRequest(const std::wstring& verb,
                               const std::string& url,
                               const std::string& body,
                               const std::string& contentType,
                               const std::string& bearerToken,
                               const std::map<std::string, std::string>& customHeaders);
};

} // namespace GDriveHttp
