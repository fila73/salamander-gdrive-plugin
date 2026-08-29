// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "gdrive_http.h"

namespace GDriveHttp
{

std::wstring HttpClient::Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty()) return L"";
    int req = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.length(), NULL, 0);
    if (req <= 0) return L"";
    std::wstring wide(req, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.length(), &wide[0], req);
    return wide;
}

std::string HttpClient::WideToUtf8(const std::wstring& wide)
{
    if (wide.empty()) return "";
    int req = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.length(), NULL, 0, NULL, NULL);
    if (req <= 0) return "";
    std::string utf8(req, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.length(), &utf8[0], req, NULL, NULL);
    return utf8;
}

std::string HttpClient::UrlEncode(const std::string& str)
{
    std::string out;
    static const char hexChars[] = "0123456789ABCDEF";
    for (unsigned char c : str)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
        {
            out.push_back((char)c);
        }
        else
        {
            out.push_back('%');
            out.push_back(hexChars[(c >> 4) & 0x0F]);
            out.push_back(hexChars[c & 0x0F]);
        }
    }
    return out;
}

HttpClient::HttpClient()
{
}

HttpClient::~HttpClient()
{
    Release();
}

bool HttpClient::Initialize(const std::wstring& userAgent)
{
    if (m_hSession) return true;

    m_hSession = WinHttpOpen(userAgent.c_str(),
                             WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                             WINHTTP_NO_PROXY_NAME,
                             WINHTTP_NO_PROXY_BYPASS,
                             0);
    if (!m_hSession)
        return false;

    // Enable TLS 1.2 and TLS 1.3
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    WinHttpSetOption(m_hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    // Timeouts: 15s resolve, 15s connect, 30s send, 60s receive
    WinHttpSetTimeouts(m_hSession, 15000, 15000, 30000, 60000);

    return true;
}

void HttpClient::Release()
{
    if (m_hSession)
    {
        WinHttpCloseHandle(m_hSession);
        m_hSession = NULL;
    }
}

HttpResponse HttpClient::Get(const std::string& url,
                             const std::string& bearerToken,
                             const std::map<std::string, std::string>& customHeaders)
{
    return ExecuteRequest(L"GET", url, "", "", bearerToken, customHeaders);
}

HttpResponse HttpClient::Post(const std::string& url,
                              const std::string& body,
                              const std::string& contentType,
                              const std::string& bearerToken,
                              const std::map<std::string, std::string>& customHeaders)
{
    return ExecuteRequest(L"POST", url, body, contentType, bearerToken, customHeaders);
}

HttpResponse HttpClient::Patch(const std::string& url,
                               const std::string& body,
                               const std::string& contentType,
                               const std::string& bearerToken,
                               const std::map<std::string, std::string>& customHeaders)
{
    return ExecuteRequest(L"PATCH", url, body, contentType, bearerToken, customHeaders);
}

HttpResponse HttpClient::Delete(const std::string& url,
                                const std::string& bearerToken,
                                const std::map<std::string, std::string>& customHeaders)
{
    return ExecuteRequest(L"DELETE", url, "", "", bearerToken, customHeaders);
}

HttpResponse HttpClient::ExecuteRequest(const std::wstring& verb,
                                        const std::string& url,
                                        const std::string& body,
                                        const std::string& contentType,
                                        const std::string& bearerToken,
                                        const std::map<std::string, std::string>& customHeaders)
{
    HttpResponse resp;
    if (!Initialize())
    {
        resp.errorMessage = "Failed to initialize WinHTTP session";
        return resp;
    }

    std::wstring wUrl = Utf8ToWide(url);

    URL_COMPONENTS urlComp{};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostName[256] = {0};
    wchar_t urlPath[2048] = {0};
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = sizeof(hostName) / sizeof(hostName[0]);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = sizeof(urlPath) / sizeof(urlPath[0]);

    if (!WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.length(), 0, &urlComp))
    {
        resp.errorMessage = "Invalid URL format: " + url;
        return resp;
    }

    bool isHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
    INTERNET_PORT port = urlComp.nPort;

    HINTERNET hConnect = WinHttpConnect(m_hSession, hostName, port, 0);
    if (!hConnect)
    {
        resp.errorMessage = "WinHttpConnect failed (err=" + std::to_string(GetLastError()) + ")";
        return resp;
    }

    DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect,
                                            verb.c_str(),
                                            urlPath,
                                            NULL,
                                            WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            flags);
    if (!hRequest)
    {
        WinHttpCloseHandle(hConnect);
        resp.errorMessage = "WinHttpOpenRequest failed (err=" + std::to_string(GetLastError()) + ")";
        return resp;
    }

    // Build headers
    std::wstring headers;
    if (!bearerToken.empty())
    {
        headers += L"Authorization: Bearer " + Utf8ToWide(bearerToken) + L"\r\n";
    }
    if (!contentType.empty() && (verb == L"POST" || verb == L"PUT" || verb == L"PATCH"))
    {
        headers += L"Content-Type: " + Utf8ToWide(contentType) + L"\r\n";
    }
    for (const auto& [k, v] : customHeaders)
    {
        headers += Utf8ToWide(k) + L": " + Utf8ToWide(v) + L"\r\n";
    }

    BOOL sent = WinHttpSendRequest(hRequest,
                                   headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                                   (DWORD)headers.length(),
                                   body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                                   (DWORD)body.length(),
                                   (DWORD)body.length(),
                                   0);
    if (!sent)
    {
        resp.errorMessage = "WinHttpSendRequest failed (err=" + std::to_string(GetLastError()) + ")";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        return resp;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL))
    {
        resp.errorMessage = "WinHttpReceiveResponse failed (err=" + std::to_string(GetLastError()) + ")";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        return resp;
    }

    // Status code
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &statusCode,
                        &statusCodeSize,
                        WINHTTP_NO_HEADER_INDEX);
    resp.statusCode = (int)statusCode;
    resp.success = (resp.statusCode >= 200 && resp.statusCode < 300);

    // Read body
    std::string responseBody;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0)
    {
        std::vector<char> buffer(bytesAvailable);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead) && bytesRead > 0)
        {
            responseBody.append(buffer.data(), bytesRead);
        }
        else
        {
            break;
        }
    }
    resp.body = std::move(responseBody);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    return resp;
}

bool HttpClient::DownloadToFile(const std::string& url,
                                const std::wstring& targetLocalPath,
                                const std::string& bearerToken,
                                ProgressCallback progressCallback,
                                const bool* cancelFlag,
                                std::string* errorOut)
{
    if (!Initialize())
    {
        if (errorOut) *errorOut = "Failed to initialize WinHTTP";
        return false;
    }

    std::wstring wUrl = Utf8ToWide(url);

    URL_COMPONENTS urlComp{};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostName[256] = {0};
    wchar_t urlPath[2048] = {0};
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = sizeof(hostName) / sizeof(hostName[0]);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = sizeof(urlPath) / sizeof(urlPath[0]);

    if (!WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.length(), 0, &urlComp))
    {
        if (errorOut) *errorOut = "Invalid URL format: " + url;
        return false;
    }

    bool isHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
    HINTERNET hConnect = WinHttpConnect(m_hSession, hostName, urlComp.nPort, 0);
    if (!hConnect)
    {
        if (errorOut) *errorOut = "WinHttpConnect failed";
        return false;
    }

    DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath, NULL,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest)
    {
        WinHttpCloseHandle(hConnect);
        if (errorOut) *errorOut = "WinHttpOpenRequest failed";
        return false;
    }

    std::wstring headers;
    if (!bearerToken.empty())
    {
        headers += L"Authorization: Bearer " + Utf8ToWide(bearerToken) + L"\r\n";
    }

    if (!WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(), NULL, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, NULL))
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        if (errorOut) *errorOut = "Failed to send request or receive response";
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);

    if (statusCode < 200 || statusCode >= 300)
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        if (errorOut) *errorOut = "HTTP error code " + std::to_string(statusCode);
        return false;
    }

    // Try to get Content-Length
    int64_t totalBytes = -1;
    wchar_t contentLenStr[64] = {0};
    DWORD contentLenSize = sizeof(contentLenStr);
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                            contentLenStr, &contentLenSize, WINHTTP_NO_HEADER_INDEX))
    {
        try { totalBytes = std::stoll(contentLenStr); } catch (...) {}
    }

    // Open local destination file
    HANDLE hFile = CreateFileW(targetLocalPath.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        if (errorOut) *errorOut = "Failed to create local destination file";
        return false;
    }

    int64_t bytesTransferred = 0;
    DWORD bytesAvailable = 0;
    bool success = true;

    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0)
    {
        if (cancelFlag && *cancelFlag)
        {
            success = false;
            if (errorOut) *errorOut = "Download cancelled";
            break;
        }

        std::vector<char> buffer(bytesAvailable);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead) && bytesRead > 0)
        {
            DWORD bytesWritten = 0;
            if (!WriteFile(hFile, buffer.data(), bytesRead, &bytesWritten, NULL) || bytesWritten != bytesRead)
            {
                success = false;
                if (errorOut) *errorOut = "Failed writing to destination file";
                break;
            }

            bytesTransferred += bytesRead;
            if (progressCallback)
            {
                if (!progressCallback(bytesTransferred, totalBytes))
                {
                    success = false;
                    if (errorOut) *errorOut = "Download cancelled by user";
                    break;
                }
            }
        }
        else
        {
            break;
        }
    }

    CloseHandle(hFile);
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);

    if (!success)
    {
        DeleteFileW(targetLocalPath.c_str());
    }

    return success;
}

bool HttpClient::UploadMultipartFile(const std::string& url,
                                    const std::wstring& localFilePath,
                                    const std::string& metadataJson,
                                    const std::string& fileContentType,
                                    const std::string& bearerToken,
                                    ProgressCallback progressCallback,
                                    const bool* cancelFlag,
                                    std::string* responseBodyOut,
                                    std::string* errorOut)
{
    if (!Initialize())
    {
        if (errorOut) *errorOut = "Failed to initialize WinHTTP";
        return false;
    }

    // Open local file
    HANDLE hFile = CreateFileW(localFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        if (errorOut) *errorOut = "Failed to open local file for reading";
        return false;
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize))
    {
        CloseHandle(hFile);
        if (errorOut) *errorOut = "Failed to get local file size";
        return false;
    }

    std::wstring wUrl = Utf8ToWide(url);

    URL_COMPONENTS urlComp{};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostName[256] = {0};
    wchar_t urlPath[2048] = {0};
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = sizeof(hostName) / sizeof(hostName[0]);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = sizeof(urlPath) / sizeof(urlPath[0]);

    if (!WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.length(), 0, &urlComp))
    {
        CloseHandle(hFile);
        if (errorOut) *errorOut = "Invalid URL format: " + url;
        return false;
    }

    bool isHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
    HINTERNET hConnect = WinHttpConnect(m_hSession, hostName, urlComp.nPort, 0);
    if (!hConnect)
    {
        CloseHandle(hFile);
        if (errorOut) *errorOut = "WinHttpConnect failed";
        return false;
    }

    DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", urlPath, NULL,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest)
    {
        CloseHandle(hFile);
        WinHttpCloseHandle(hConnect);
        if (errorOut) *errorOut = "WinHttpOpenRequest failed";
        return false;
    }

    // Prepare multipart headers and boundary
    std::string boundary = "----SalGDriveUploadBoundary" + std::to_string(GetTickCount64());
    std::string preamble = "--" + boundary + "\r\n"
                           "Content-Type: application/json; charset=UTF-8\r\n\r\n" +
                           metadataJson + "\r\n"
                           "--" + boundary + "\r\n"
                           "Content-Type: " + fileContentType + "\r\n\r\n";
    std::string epilogue = "\r\n--" + boundary + "--\r\n";

    int64_t totalLength = (int64_t)preamble.length() + fileSize.QuadPart + (int64_t)epilogue.length();

    std::wstring headers;
    if (!bearerToken.empty())
    {
        headers += L"Authorization: Bearer " + Utf8ToWide(bearerToken) + L"\r\n";
    }
    headers += L"Content-Type: multipart/related; boundary=" + Utf8ToWide(boundary) + L"\r\n";

    if (!WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(),
                            NULL, 0, (DWORD)totalLength, 0))
    {
        CloseHandle(hFile);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        if (errorOut) *errorOut = "Failed to send upload request header";
        return false;
    }

    // Write preamble
    DWORD bytesWritten = 0;
    if (!WinHttpWriteData(hRequest, preamble.data(), (DWORD)preamble.length(), &bytesWritten) ||
        bytesWritten != preamble.length())
    {
        CloseHandle(hFile);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        if (errorOut) *errorOut = "Failed writing multipart preamble";
        return false;
    }

    // Stream file content (256 KB chunk size for optimal throughput)
    std::vector<char> buffer(256 * 1024);
    int64_t bytesUploaded = 0;
    bool success = true;
    DWORD bytesRead = 0;

    while (ReadFile(hFile, buffer.data(), (DWORD)buffer.size(), &bytesRead, NULL) && bytesRead > 0)
    {
        if (cancelFlag && *cancelFlag)
        {
            success = false;
            if (errorOut) *errorOut = "Upload cancelled";
            break;
        }

        DWORD written = 0;
        if (!WinHttpWriteData(hRequest, buffer.data(), bytesRead, &written) || written != bytesRead)
        {
            success = false;
            if (errorOut) *errorOut = "Failed writing file data during upload";
            break;
        }

        bytesUploaded += bytesRead;
        if (progressCallback)
        {
            if (!progressCallback(bytesUploaded, fileSize.QuadPart))
            {
                success = false;
                if (errorOut) *errorOut = "Upload cancelled by user";
                break;
            }
        }
    }

    CloseHandle(hFile);

    if (success)
    {
        // Write epilogue
        DWORD epilogueWritten = 0;
        if (!WinHttpWriteData(hRequest, epilogue.data(), (DWORD)epilogue.length(), &epilogueWritten) ||
            epilogueWritten != epilogue.length())
        {
            success = false;
            if (errorOut) *errorOut = "Failed writing multipart epilogue";
        }
    }

    if (success && !WinHttpReceiveResponse(hRequest, NULL))
    {
        success = false;
        if (errorOut) *errorOut = "Failed to receive upload response";
    }

    if (success)
    {
        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);

        if (statusCode < 200 || statusCode >= 300)
        {
            success = false;
            if (errorOut) *errorOut = "HTTP upload error: " + std::to_string(statusCode);
        }

        // Read response body
        std::string responseBody;
        DWORD bytesAvailable = 0;
        while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0)
        {
            std::vector<char> respBuffer(bytesAvailable);
            DWORD respBytesRead = 0;
            if (WinHttpReadData(hRequest, respBuffer.data(), bytesAvailable, &respBytesRead) && respBytesRead > 0)
            {
                responseBody.append(respBuffer.data(), respBytesRead);
            }
            else
            {
                break;
            }
        }

        if (responseBodyOut)
        {
            *responseBodyOut = std::move(responseBody);
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    return success;
}

} // namespace GDriveHttp
