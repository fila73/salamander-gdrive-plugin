// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "gdrive_auth.h"
#include "gdrive_cache.h"
#include "gdrive_http.h"
#include "gdrive_json.h"
#include <algorithm>

namespace GDriveAuth
{

static const wchar_t* kRegSubKey = L"Software\\Altap\\Salamander\\Plugins\\gdrive";
static const wchar_t* kRegAccountsSubKey = L"Software\\Altap\\Salamander\\Plugins\\gdrive\\Accounts";
static const wchar_t* kRegValActiveAccount = L"ActiveAccount";
static const wchar_t* kRegValRefreshToken = L"EncryptedRefreshToken";
static const wchar_t* kRegValClientId = L"OAuthClientId";
static const wchar_t* kRegValClientSecret = L"OAuthClientSecret";
static const wchar_t* kRegValAccountEmail = L"AccountEmail";
static const wchar_t* kRegValAccountName = L"AccountName";

static std::wstring MakeSafeAccountKey(const std::string& email)
{
    std::string safe = email.empty() ? "default" : email;
    std::replace(safe.begin(), safe.end(), '@', '_');
    std::replace(safe.begin(), safe.end(), '.', '_');
    std::replace(safe.begin(), safe.end(), ':', '_');
    std::replace(safe.begin(), safe.end(), '/', '_');
    std::replace(safe.begin(), safe.end(), '\\', '_');
    return GDriveHttp::HttpClient::Utf8ToWide(safe);
}

AuthManager& AuthManager::GetInstance()
{
    static AuthManager instance;
    return instance;
}

AuthManager::AuthManager()
    : m_clientId(kDefaultClientId),
      m_clientSecret(kDefaultClientSecret)
{
    LoadSavedTokens();
}

AuthManager::~AuthManager()
{
}

void AuthManager::Initialize(const std::string& customClientId, const std::string& customClientSecret)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!customClientId.empty())
        m_clientId = customClientId;
    else
        m_clientId = kDefaultClientId;

    if (!customClientSecret.empty())
        m_clientSecret = customClientSecret;
    else
        m_clientSecret = kDefaultClientSecret;

    LoadSavedTokens();
}

void AuthManager::SetClientId(const std::string& id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (id.empty())
        m_clientId = kDefaultClientId;
    else
        m_clientId = id;
}

void AuthManager::SetClientSecret(const std::string& secret)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (secret.empty())
        m_clientSecret = kDefaultClientSecret;
    else
        m_clientSecret = secret;
}

bool AuthManager::IsAuthenticated()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_tokens.refreshToken.empty() || !m_tokens.accessToken.empty();
}

std::string AuthManager::GetAccountDisplay() const
{
    if (!m_tokens.accountEmail.empty())
        return m_tokens.accountEmail;
    if (!m_tokens.accountName.empty())
        return m_tokens.accountName;
    if (!m_tokens.refreshToken.empty() || !m_tokens.accessToken.empty())
        return "Connected";
    return "Not connected";
}

std::string AuthManager::GetValidAccessToken(std::string* errorOut)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // If access token is still valid with > 60s margin, return it
    if (!m_tokens.accessToken.empty() && m_tokens.expiresAt > (now + 60))
    {
        return m_tokens.accessToken;
    }

    // Need refresh
    if (!m_tokens.refreshToken.empty())
    {
        if (RefreshAccessTokenInternal(errorOut))
        {
            return m_tokens.accessToken;
        }
    }

    if (errorOut && errorOut->empty())
    {
        *errorOut = "No valid authentication tokens available. Please sign in.";
    }
    return "";
}

bool AuthManager::RefreshAccessTokenInternal(std::string* errorOut)
{
    if (m_tokens.refreshToken.empty())
    {
        if (errorOut) *errorOut = "No refresh token available";
        return false;
    }

    GDriveHttp::HttpClient http;
    std::string postBody = "client_id=" + GDriveHttp::HttpClient::UrlEncode(m_clientId);
    if (!m_clientSecret.empty())
    {
        postBody += "&client_secret=" + GDriveHttp::HttpClient::UrlEncode(m_clientSecret);
    }
    postBody += "&refresh_token=" + GDriveHttp::HttpClient::UrlEncode(m_tokens.refreshToken) +
                "&grant_type=refresh_token";

    auto resp = http.Post(kTokenEndpoint, postBody, "application/x-www-form-urlencoded");
    if (!resp.success)
    {
        if (errorOut) *errorOut = "Token refresh HTTP request failed: " + resp.errorMessage;
        return false;
    }

    auto json = GDriveJson::Value::Parse(resp.body);
    if (!json.IsObject() || !json.Has("access_token"))
    {
        std::string errDesc = json.GetString("error_description", json.GetString("error", "Unknown token error"));
        if (errorOut) *errorOut = "Token refresh failed: " + errDesc;
        return false;
    }

    m_tokens.accessToken = json.GetString("access_token");
    int expiresIn = json.GetInt("expires_in", 3600);

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    m_tokens.expiresAt = now + expiresIn;

    return true;
}

std::string AuthManager::Base64UrlEncode(const unsigned char* data, size_t length)
{
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789-_";

    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (length--)
    {
        char_array_3[i++] = *(data++);
        if (i == 3)
        {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; (i < 4); i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i)
    {
        for (j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; (j < i + 1); j++)
            ret += base64_chars[char_array_4[j]];
    }

    return ret;
}

std::string AuthManager::UrlDecode(const std::string& in)
{
    std::string out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); ++i)
    {
        if (in[i] == '%' && i + 2 < in.length())
        {
            int h1 = in[i + 1];
            int h2 = in[i + 2];
            auto hexVal = [](int c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int v1 = hexVal(h1);
            int v2 = hexVal(h2);
            if (v1 >= 0 && v2 >= 0)
            {
                out += static_cast<char>((v1 << 4) | v2);
                i += 2;
                continue;
            }
        }
        else if (in[i] == '+')
        {
            out += ' ';
            continue;
        }
        out += in[i];
    }
    return out;
}

std::string AuthManager::GenerateRandomString(size_t length)
{
    std::vector<unsigned char> randomBytes(length);
    HCRYPTPROV hProv = 0;
    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
    {
        CryptGenRandom(hProv, (DWORD)length, randomBytes.data());
        CryptReleaseContext(hProv, 0);
    }
    else
    {
        for (size_t i = 0; i < length; ++i)
            randomBytes[i] = (unsigned char)(rand() % 256);
    }
    return Base64UrlEncode(randomBytes.data(), randomBytes.size()).substr(0, length);
}

std::string AuthManager::ComputePkceChallenge(const std::string& verifier)
{
    unsigned char hash[32] = {0};
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;

    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
    {
        if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
        {
            CryptHashData(hHash, (const BYTE*)verifier.data(), (DWORD)verifier.length(), 0);
            DWORD hashLen = sizeof(hash);
            CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0);
            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
    }

    return Base64UrlEncode(hash, sizeof(hash));
}

bool AuthManager::FetchUserInfo(const std::string& accessToken)
{
    GDriveHttp::HttpClient http;
    auto resp = http.Get("https://www.googleapis.com/drive/v3/about?fields=user", accessToken);
    if (resp.success)
    {
        auto json = GDriveJson::Value::Parse(resp.body);
        if (json.IsObject() && json.Has("user"))
        {
            const auto& user = json.GetObject("user");
            m_tokens.accountEmail = user.GetString("emailAddress");
            m_tokens.accountName = user.GetString("displayName");
            return true;
        }
    }
    return false;
}

bool AuthManager::LaunchInteractiveAuth(HWND hParent, std::string* errorOut)
{
    if (m_clientId.empty())
    {
        if (errorOut) *errorOut = LoadStr(IDS_ERR_NO_CLIENT_ID);
        return false;
    }

    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        if (errorOut) *errorOut = "Failed to initialize Winsock";
        return false;
    }

    // Create IPv4 loopback listener
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET)
    {
        WSACleanup();
        if (errorOut) *errorOut = "Failed to create loopback socket";
        return false;
    }

    sockaddr_in service{};
    service.sin_family = AF_INET;
    service.sin_addr.s_addr = inet_addr("127.0.0.1");
    service.sin_port = 0; // OS picks free ephemeral port

    if (bind(listener, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR)
    {
        closesocket(listener);
        WSACleanup();
        if (errorOut) *errorOut = "Failed to bind loopback socket";
        return false;
    }

    int addrLen = sizeof(service);
    if (getsockname(listener, (SOCKADDR*)&service, &addrLen) == SOCKET_ERROR)
    {
        closesocket(listener);
        WSACleanup();
        if (errorOut) *errorOut = "Failed to get local port";
        return false;
    }

    int assignedPort = ntohs(service.sin_port);

    if (listen(listener, 1) == SOCKET_ERROR)
    {
        closesocket(listener);
        WSACleanup();
        if (errorOut) *errorOut = "Failed to listen on loopback socket";
        return false;
    }

    // PKCE parameters
    std::string codeVerifier = GenerateRandomString(64);
    std::string codeChallenge = ComputePkceChallenge(codeVerifier);
    std::string state = GenerateRandomString(32);
    std::string redirectUri = "http://127.0.0.1:" + std::to_string(assignedPort) + "/oauth2callback";

    // Build Google OAuth authorization URL
    std::string authUrl = "https://accounts.google.com/o/oauth2/v2/auth?"
                          "client_id=" + GDriveHttp::HttpClient::UrlEncode(m_clientId) +
                          "&redirect_uri=" + GDriveHttp::HttpClient::UrlEncode(redirectUri) +
                          "&response_type=code" +
                          "&scope=" + GDriveHttp::HttpClient::UrlEncode(kOAuthScope) +
                          "&code_challenge=" + GDriveHttp::HttpClient::UrlEncode(codeChallenge) +
                          "&code_challenge_method=S256" +
                          "&state=" + GDriveHttp::HttpClient::UrlEncode(state) +
                          "&access_type=offline" +
                          "&prompt=consent";

    // Open user's default browser
    std::wstring wUrl = GDriveHttp::HttpClient::Utf8ToWide(authUrl);
    HINSTANCE hInst = ShellExecuteW(hParent, L"open", wUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)hInst <= 32)
    {
        closesocket(listener);
        WSACleanup();
        if (errorOut) *errorOut = "Failed to launch default web browser";
        return false;
    }

    // Set socket receive timeout to 120 seconds
    DWORD timeoutMs = 120000;
    setsockopt(listener, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs));

    SOCKET clientSock = accept(listener, NULL, NULL);
    if (clientSock == INVALID_SOCKET)
    {
        closesocket(listener);
        WSACleanup();
        if (errorOut) *errorOut = "Authorization timeout or canceled by user";
        return false;
    }

    // Read HTTP GET request from browser
    std::vector<char> buffer(4096, 0);
    int bytesReceived = recv(clientSock, buffer.data(), (int)buffer.size() - 1, 0);
    if (bytesReceived <= 0)
    {
        closesocket(clientSock);
        closesocket(listener);
        WSACleanup();
        if (errorOut) *errorOut = "Failed to read response from browser";
        return false;
    }

    std::string requestStr(buffer.data(), bytesReceived);
    std::string authCode;
    std::string returnedState;
    std::string authError;

    // Parse query line: GET /oauth2callback?code=...&state=... HTTP/1.1
    size_t qPos = requestStr.find("GET /oauth2callback?");
    if (qPos != std::string::npos)
    {
        size_t endPos = requestStr.find(" HTTP/", qPos);
        if (endPos != std::string::npos)
        {
            std::string query = requestStr.substr(qPos + 20, endPos - (qPos + 20));
            std::istringstream ss(query);
            std::string param;
            while (std::getline(ss, param, '&'))
            {
                size_t eqPos = param.find('=');
                if (eqPos != std::string::npos)
                {
                    std::string k = param.substr(0, eqPos);
                    std::string v = param.substr(eqPos + 1);
                    if (k == "code") authCode = v;
                    else if (k == "state") returnedState = v;
                    else if (k == "error") authError = v;
                }
            }
        }
    }

    // Decode URL components
    authCode = UrlDecode(authCode);
    returnedState = UrlDecode(returnedState);

    // Send response HTML to browser
    std::string htmlBody;
    if (!authCode.empty() && (returnedState == state))
    {
        htmlBody = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Open Salamander - Google Drive</title>"
                   "<style>body{font-family:Segoe UI,sans-serif;text-align:center;padding:50px;background:#f8f9fa;color:#333;}"
                   ".card{background:white;padding:30px;border-radius:8px;box-shadow:0 2px 10px rgba(0,0,0,0.1);display:inline-block;max-width:500px;}"
                   "h2{color:#1a73e8;margin-top:0;}p{font-size:16px;line-height:1.5;}</style></head>"
                   "<body><div class='card'>"
                   "<h2>&#x2705; Authentication Successful!</h2>"
                   "<p>You have successfully authorized Open Salamander to access Google Drive.</p>"
                   "<p><strong>You can close this tab and return to Salamander.</strong></p>"
                   "</div></body></html>";
    }
    else
    {
        htmlBody = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Open Salamander - Error</title>"
                   "<style>body{font-family:Segoe UI,sans-serif;text-align:center;padding:50px;background:#f8f9fa;color:#333;}"
                   ".card{background:white;padding:30px;border-radius:8px;box-shadow:0 2px 10px rgba(0,0,0,0.1);display:inline-block;max-width:500px;}"
                   "h2{color:#d93025;margin-top:0;}p{font-size:16px;line-height:1.5;}</style></head>"
                   "<body><div class='card'>"
                   "<h2>&#x274C; Authentication Failed</h2>"
                   "<p>" + (authError.empty() ? "State mismatch or invalid authorization code." : authError) + "</p>"
                   "</div></body></html>";
    }

    std::string httpResponse = "HTTP/1.1 200 OK\r\n"
                               "Content-Type: text/html; charset=utf-8\r\n"
                               "Content-Length: " + std::to_string(htmlBody.length()) + "\r\n"
                               "Connection: close\r\n\r\n" + htmlBody;

    send(clientSock, httpResponse.data(), (int)httpResponse.length(), 0);
    closesocket(clientSock);
    closesocket(listener);
    WSACleanup();

    if (authCode.empty())
    {
        if (errorOut) *errorOut = "Did not receive authorization code from Google (error=" + authError + ")";
        return false;
    }

    if (returnedState != state)
    {
        if (errorOut) *errorOut = "OAuth state parameter mismatch (possible CSRF)";
        return false;
    }

    // Exchange auth code for tokens via HTTPS POST
    GDriveHttp::HttpClient http;
    std::string postBody = "client_id=" + GDriveHttp::HttpClient::UrlEncode(m_clientId);
    if (!m_clientSecret.empty())
    {
        postBody += "&client_secret=" + GDriveHttp::HttpClient::UrlEncode(m_clientSecret);
    }
    postBody += "&code=" + GDriveHttp::HttpClient::UrlEncode(authCode) +
                "&code_verifier=" + GDriveHttp::HttpClient::UrlEncode(codeVerifier) +
                "&grant_type=authorization_code" +
                "&redirect_uri=" + GDriveHttp::HttpClient::UrlEncode(redirectUri);

    auto tokenResp = http.Post(kTokenEndpoint, postBody, "application/x-www-form-urlencoded");
    if (!tokenResp.success)
    {
        if (errorOut) *errorOut = "Token exchange failed: " + tokenResp.errorMessage + " (body: " + tokenResp.body + ")";
        return false;
    }

    auto json = GDriveJson::Value::Parse(tokenResp.body);
    if (!json.IsObject() || !json.Has("access_token"))
    {
        std::string errDesc = json.GetString("error_description", json.GetString("error", "Unknown token error"));
        if (errorOut) *errorOut = "Token exchange failed: " + errDesc;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tokens.accessToken = json.GetString("access_token");
        if (json.Has("refresh_token"))
        {
            m_tokens.refreshToken = json.GetString("refresh_token");
        }
        int expiresIn = json.GetInt("expires_in", 3600);

        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        m_tokens.expiresAt = now + expiresIn;

        FetchUserInfo(m_tokens.accessToken);
        SaveTokens();
    }

    return true;
}

void AuthManager::Logout()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string email = m_tokens.accountEmail;
    m_tokens = AuthTokens();

    if (!email.empty())
    {
        std::wstring wSafeKey = MakeSafeAccountKey(email);
        std::wstring accSubKey = std::wstring(kRegAccountsSubKey) + L"\\" + wSafeKey;
        RegDeleteKeyW(HKEY_CURRENT_USER, accSubKey.c_str());
        GDriveCache::CacheManager::GetInstance().ClearDiskCache(email);
    }

    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
    {
        RegDeleteValueW(hKey, kRegValActiveAccount);
        RegDeleteValueW(hKey, kRegValRefreshToken);
        RegDeleteValueW(hKey, kRegValAccountEmail);
        RegDeleteValueW(hKey, kRegValAccountName);
        RegCloseKey(hKey);
    }
}

bool AuthManager::SaveTokens()
{
    if (m_tokens.refreshToken.empty())
        return false;

    // Encrypt refresh token with Windows DPAPI
    DATA_BLOB inBlob{};
    inBlob.pbData = (BYTE*)m_tokens.refreshToken.data();
    inBlob.cbData = (DWORD)m_tokens.refreshToken.length();

    DATA_BLOB outBlob{};
    if (!CryptProtectData(&inBlob, L"Google Drive Refresh Token", NULL, NULL, NULL, 0, &outBlob))
    {
        return false;
    }

    // 1. Save main plugin config (ClientId, ClientSecret, ActiveAccount)
    HKEY hKeyMain = NULL;
    DWORD disposition = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, NULL, 0, KEY_WRITE, NULL, &hKeyMain, &disposition) == ERROR_SUCCESS)
    {
        std::wstring wClientId = GDriveHttp::HttpClient::Utf8ToWide(m_clientId);
        RegSetValueExW(hKeyMain, kRegValClientId, 0, REG_SZ, (const BYTE*)wClientId.c_str(), (DWORD)(wClientId.length() + 1) * sizeof(wchar_t));

        if (!m_clientSecret.empty())
        {
            std::wstring wSecret = GDriveHttp::HttpClient::Utf8ToWide(m_clientSecret);
            RegSetValueExW(hKeyMain, kRegValClientSecret, 0, REG_SZ, (const BYTE*)wSecret.c_str(), (DWORD)(wSecret.length() + 1) * sizeof(wchar_t));
        }

        if (!m_tokens.accountEmail.empty())
        {
            std::wstring wEmail = GDriveHttp::HttpClient::Utf8ToWide(m_tokens.accountEmail);
            RegSetValueExW(hKeyMain, kRegValActiveAccount, 0, REG_SZ, (const BYTE*)wEmail.c_str(), (DWORD)(wEmail.length() + 1) * sizeof(wchar_t));
        }

        RegCloseKey(hKeyMain);
    }

    // 2. Save under Accounts\<safe_email>
    if (!m_tokens.accountEmail.empty())
    {
        std::wstring wSafeKey = MakeSafeAccountKey(m_tokens.accountEmail);
        std::wstring accSubKey = std::wstring(kRegAccountsSubKey) + L"\\" + wSafeKey;
        HKEY hKeyAcc = NULL;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, accSubKey.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKeyAcc, &disposition) == ERROR_SUCCESS)
        {
            RegSetValueExW(hKeyAcc, kRegValRefreshToken, 0, REG_BINARY, outBlob.pbData, outBlob.cbData);

            std::wstring wEmail = GDriveHttp::HttpClient::Utf8ToWide(m_tokens.accountEmail);
            RegSetValueExW(hKeyAcc, kRegValAccountEmail, 0, REG_SZ, (const BYTE*)wEmail.c_str(), (DWORD)(wEmail.length() + 1) * sizeof(wchar_t));

            if (!m_tokens.accountName.empty())
            {
                std::wstring wName = GDriveHttp::HttpClient::Utf8ToWide(m_tokens.accountName);
                RegSetValueExW(hKeyAcc, kRegValAccountName, 0, REG_SZ, (const BYTE*)wName.c_str(), (DWORD)(wName.length() + 1) * sizeof(wchar_t));
            }

            RegCloseKey(hKeyAcc);
        }

        GDriveCache::CacheManager::GetInstance().SetCurrentAccount(m_tokens.accountEmail);
    }

    LocalFree(outBlob.pbData);
    return true;
}

bool AuthManager::LoadSavedTokens()
{
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
    {
        return false;
    }

    // Read client ID if saved
    wchar_t szClientId[256] = {0};
    DWORD cbClientId = sizeof(szClientId);
    if (RegQueryValueExW(hKey, kRegValClientId, NULL, NULL, (LPBYTE)szClientId, &cbClientId) == ERROR_SUCCESS)
    {
        std::string loadedId = GDriveHttp::HttpClient::WideToUtf8(szClientId);
        if (!loadedId.empty())
            m_clientId = loadedId;
    }

    // Read client secret if saved
    wchar_t szClientSecret[256] = {0};
    DWORD cbClientSecret = sizeof(szClientSecret);
    if (RegQueryValueExW(hKey, kRegValClientSecret, NULL, NULL, (LPBYTE)szClientSecret, &cbClientSecret) == ERROR_SUCCESS)
    {
        std::string loadedSecret = GDriveHttp::HttpClient::WideToUtf8(szClientSecret);
        if (!loadedSecret.empty())
            m_clientSecret = loadedSecret;
    }

    // Check ActiveAccount
    wchar_t szActiveAcc[256] = {0};
    DWORD cbActiveAcc = sizeof(szActiveAcc);
    std::string activeEmail;
    if (RegQueryValueExW(hKey, kRegValActiveAccount, NULL, NULL, (LPBYTE)szActiveAcc, &cbActiveAcc) == ERROR_SUCCESS)
    {
        activeEmail = GDriveHttp::HttpClient::WideToUtf8(szActiveAcc);
    }

    RegCloseKey(hKey);

    // If active account exists, load from Accounts\<safe_email>
    HKEY hKeyAcc = NULL;
    bool openedAccountKey = false;

    if (!activeEmail.empty())
    {
        std::wstring accSubKey = std::wstring(kRegAccountsSubKey) + L"\\" + MakeSafeAccountKey(activeEmail);
        if (RegOpenKeyExW(HKEY_CURRENT_USER, accSubKey.c_str(), 0, KEY_READ, &hKeyAcc) == ERROR_SUCCESS)
        {
            openedAccountKey = true;
        }
    }

    // Fallback to main key for legacy configurations
    if (!openedAccountKey)
    {
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_READ, &hKeyAcc) != ERROR_SUCCESS)
        {
            return false;
        }
    }

    // Read account email and name
    wchar_t szEmail[256] = {0};
    DWORD cbEmail = sizeof(szEmail);
    if (RegQueryValueExW(hKeyAcc, kRegValAccountEmail, NULL, NULL, (LPBYTE)szEmail, &cbEmail) == ERROR_SUCCESS)
    {
        m_tokens.accountEmail = GDriveHttp::HttpClient::WideToUtf8(szEmail);
    }

    wchar_t szName[256] = {0};
    DWORD cbName = sizeof(szName);
    if (RegQueryValueExW(hKeyAcc, kRegValAccountName, NULL, NULL, (LPBYTE)szName, &cbName) == ERROR_SUCCESS)
    {
        m_tokens.accountName = GDriveHttp::HttpClient::WideToUtf8(szName);
    }

    // Read encrypted refresh token
    DWORD cbData = 0;
    if (RegQueryValueExW(hKeyAcc, kRegValRefreshToken, NULL, NULL, NULL, &cbData) != ERROR_SUCCESS || cbData == 0)
    {
        RegCloseKey(hKeyAcc);
        return false;
    }

    std::vector<BYTE> encData(cbData);
    if (RegQueryValueExW(hKeyAcc, kRegValRefreshToken, NULL, NULL, encData.data(), &cbData) != ERROR_SUCCESS)
    {
        RegCloseKey(hKeyAcc);
        return false;
    }
    RegCloseKey(hKeyAcc);

    DATA_BLOB inBlob{};
    inBlob.pbData = encData.data();
    inBlob.cbData = cbData;

    DATA_BLOB outBlob{};
    if (!CryptUnprotectData(&inBlob, NULL, NULL, NULL, NULL, 0, &outBlob))
    {
        return false;
    }

    m_tokens.refreshToken = std::string((char*)outBlob.pbData, outBlob.cbData);
    LocalFree(outBlob.pbData);

    if (!m_tokens.accountEmail.empty())
    {
        GDriveCache::CacheManager::GetInstance().SetCurrentAccount(m_tokens.accountEmail);
        GDriveCache::CacheManager::GetInstance().LoadFromDisk();
    }

    return !m_tokens.refreshToken.empty();
}

std::vector<AccountProfile> AuthManager::GetAccounts()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AccountProfile> accounts;

    HKEY hKeyAccounts = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegAccountsSubKey, 0, KEY_READ, &hKeyAccounts) == ERROR_SUCCESS)
    {
        wchar_t subKeyName[256] = {0};
        DWORD index = 0;
        DWORD nameLen = sizeof(subKeyName) / sizeof(subKeyName[0]);

        while (RegEnumKeyExW(hKeyAccounts, index, subKeyName, &nameLen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
        {
            std::wstring fullSubKey = std::wstring(kRegAccountsSubKey) + L"\\" + subKeyName;
            HKEY hKeyItem = NULL;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, fullSubKey.c_str(), 0, KEY_READ, &hKeyItem) == ERROR_SUCCESS)
            {
                AccountProfile profile;
                wchar_t szVal[256] = {0};
                DWORD cbVal = sizeof(szVal);

                if (RegQueryValueExW(hKeyItem, kRegValAccountEmail, NULL, NULL, (LPBYTE)szVal, &cbVal) == ERROR_SUCCESS)
                {
                    profile.email = GDriveHttp::HttpClient::WideToUtf8(szVal);
                }

                cbVal = sizeof(szVal);
                if (RegQueryValueExW(hKeyItem, kRegValAccountName, NULL, NULL, (LPBYTE)szVal, &cbVal) == ERROR_SUCCESS)
                {
                    profile.displayName = GDriveHttp::HttpClient::WideToUtf8(szVal);
                }

                if (!profile.email.empty())
                {
                    profile.isActive = (_stricmp(profile.email.c_str(), m_tokens.accountEmail.c_str()) == 0);
                    accounts.push_back(profile);
                }

                RegCloseKey(hKeyItem);
            }

            index++;
            nameLen = sizeof(subKeyName) / sizeof(subKeyName[0]);
        }
        RegCloseKey(hKeyAccounts);
    }

    if (accounts.empty() && !m_tokens.accountEmail.empty())
    {
        AccountProfile prof;
        prof.email = m_tokens.accountEmail;
        prof.displayName = m_tokens.accountName;
        prof.isActive = true;
        accounts.push_back(prof);
    }

    return accounts;
}

bool AuthManager::SwitchAccount(const std::string& email)
{
    if (email.empty()) return false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (_stricmp(email.c_str(), m_tokens.accountEmail.c_str()) == 0)
        {
            return true; // already active
        }
    }

    // Save tokens of active account first
    SaveTokens();

    std::wstring accSubKey = std::wstring(kRegAccountsSubKey) + L"\\" + MakeSafeAccountKey(email);
    HKEY hKeyAcc = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, accSubKey.c_str(), 0, KEY_READ, &hKeyAcc) != ERROR_SUCCESS)
    {
        return false;
    }

    std::string newEmail, newName, newRefreshToken;

    wchar_t szBuf[256] = {0};
    DWORD cbBuf = sizeof(szBuf);
    if (RegQueryValueExW(hKeyAcc, kRegValAccountEmail, NULL, NULL, (LPBYTE)szBuf, &cbBuf) == ERROR_SUCCESS)
    {
        newEmail = GDriveHttp::HttpClient::WideToUtf8(szBuf);
    }

    cbBuf = sizeof(szBuf);
    if (RegQueryValueExW(hKeyAcc, kRegValAccountName, NULL, NULL, (LPBYTE)szBuf, &cbBuf) == ERROR_SUCCESS)
    {
        newName = GDriveHttp::HttpClient::WideToUtf8(szBuf);
    }

    DWORD cbData = 0;
    if (RegQueryValueExW(hKeyAcc, kRegValRefreshToken, NULL, NULL, NULL, &cbData) == ERROR_SUCCESS && cbData > 0)
    {
        std::vector<BYTE> encData(cbData);
        if (RegQueryValueExW(hKeyAcc, kRegValRefreshToken, NULL, NULL, encData.data(), &cbData) == ERROR_SUCCESS)
        {
            DATA_BLOB inBlob{}, outBlob{};
            inBlob.pbData = encData.data();
            inBlob.cbData = cbData;
            if (CryptUnprotectData(&inBlob, NULL, NULL, NULL, NULL, 0, &outBlob))
            {
                newRefreshToken = std::string((char*)outBlob.pbData, outBlob.cbData);
                LocalFree(outBlob.pbData);
            }
        }
    }
    RegCloseKey(hKeyAcc);

    if (newRefreshToken.empty())
    {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tokens.accountEmail = newEmail;
        m_tokens.accountName = newName;
        m_tokens.refreshToken = newRefreshToken;
        m_tokens.accessToken = "";
        m_tokens.expiresAt = 0;
    }

    // Set ActiveAccount in registry
    HKEY hKeyMain = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_WRITE, &hKeyMain) == ERROR_SUCCESS)
    {
        std::wstring wEmail = GDriveHttp::HttpClient::Utf8ToWide(newEmail);
        RegSetValueExW(hKeyMain, kRegValActiveAccount, 0, REG_SZ, (const BYTE*)wEmail.c_str(), (DWORD)(wEmail.length() + 1) * sizeof(wchar_t));
        RegCloseKey(hKeyMain);
    }

    // Switch persistent cache
    GDriveCache::CacheManager::GetInstance().SwitchAccount(newEmail);

    // Refresh token for the new account
    GetValidAccessToken();

    return true;
}

bool AuthManager::AddAccount(HWND hParent, std::string* errorOut)
{
    return LaunchInteractiveAuth(hParent, errorOut);
}

bool AuthManager::RemoveAccount(const std::string& email)
{
    if (email.empty()) return false;

    std::wstring accSubKey = std::wstring(kRegAccountsSubKey) + L"\\" + MakeSafeAccountKey(email);
    RegDeleteKeyW(HKEY_CURRENT_USER, accSubKey.c_str());

    GDriveCache::CacheManager::GetInstance().ClearDiskCache(email);

    bool wasActive = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        wasActive = (_stricmp(email.c_str(), m_tokens.accountEmail.c_str()) == 0);
    }

    if (wasActive)
    {
        auto remaining = GetAccounts();
        if (!remaining.empty())
        {
            SwitchAccount(remaining[0].email);
        }
        else
        {
            Logout();
        }
    }

    return true;
}

} // namespace GDriveAuth
