// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <cstdint>
#include <windows.h>

namespace GDriveAuth
{

constexpr const char* kDefaultClientId = "";
constexpr const char* kDefaultClientSecret = "";
constexpr const char* kOAuthScope = "https://www.googleapis.com/auth/drive";
constexpr const char* kTokenEndpoint = "https://oauth2.googleapis.com/token";

struct AuthTokens
{
    std::string accessToken;
    std::string refreshToken;
    int64_t expiresAt = 0; // Unix timestamp in seconds
    std::string accountEmail;
    std::string accountName;
};

struct AccountProfile
{
    std::string email;
    std::string displayName;
    bool isActive = false;
};

class AuthManager
{
public:
    static AuthManager& GetInstance();

    void Initialize(const std::string& customClientId = "", const std::string& customClientSecret = "");
    bool IsAuthenticated();
    std::string GetValidAccessToken(std::string* errorOut = nullptr);

    bool LaunchInteractiveAuth(HWND hParent, std::string* errorOut = nullptr);
    void Logout();

    // Multi-account management
    std::vector<AccountProfile> GetAccounts();
    bool SwitchAccount(const std::string& email);
    bool RemoveAccount(const std::string& email);
    bool AddAccount(HWND hParent, std::string* errorOut = nullptr);

    bool LoadSavedTokens();
    bool SaveTokens();

    const std::string& GetClientId() const { return m_clientId; }
    void SetClientId(const std::string& id);
    const std::string& GetClientSecret() const { return m_clientSecret; }
    void SetClientSecret(const std::string& secret);

    const AuthTokens& GetTokens() const { return m_tokens; }
    std::string GetAccountDisplay() const;

private:
    AuthManager();
    ~AuthManager();

    std::string m_clientId;
    std::string m_clientSecret;
    AuthTokens m_tokens;
    std::mutex m_mutex;

    bool RefreshAccessTokenInternal(std::string* errorOut = nullptr);
    bool FetchUserInfo(const std::string& accessToken);

    static std::string GenerateRandomString(size_t length);
    static std::string ComputePkceChallenge(const std::string& verifier);
    static std::string Base64UrlEncode(const unsigned char* data, size_t length);
    static std::string UrlDecode(const std::string& in);
};

} // namespace GDriveAuth
