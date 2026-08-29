// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "gdrive.h"
#include "gdrive_auth.h"
#include "dialogs.h"

// Globals
HINSTANCE DLLInstance = NULL;
HINSTANCE HLanguage = NULL;
CSalamanderGeneralAbstract* SalamanderGeneral = NULL;
CSalamanderDebugAbstract* SalamanderDebug = NULL;
CSalamanderGUIAbstract* SalamanderGUI = NULL;
int SalamanderVersion = 0;
unsigned char* LowerCase = NULL;
unsigned char* UpperCase = NULL;

const char* PluginNameEN = "Google Drive";
const char* PluginNameShort = "GDRIVE";

char AssignedFSName[MAX_PATH] = "gdrive";
int AssignedFSNameLen = 6;

BOOL CfgIncludeSharedDrives = TRUE;

CPluginInterface PluginInterface;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        DLLInstance = hinstDLL;

        INITCOMMONCONTROLSEX initCtrls;
        initCtrls.dwSize = sizeof(INITCOMMONCONTROLSEX);
        initCtrls.dwICC = ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&initCtrls);
    }
    return TRUE;
}

char* LoadStr(int resID)
{
    if (SalamanderGeneral != NULL && HLanguage != NULL)
        return SalamanderGeneral->LoadStr(HLanguage, resID);
    static char buf[1024];
    buf[0] = 0;
    LoadStringA(HLanguage ? HLanguage : DLLInstance, resID, buf, sizeof(buf));
    return buf;
}

extern "C" {

int WINAPI SalamanderPluginGetReqVer()
{
    return 103;
}

int WINAPI SalamanderPluginGetSDKVer()
{
    return LAST_VERSION_OF_SALAMANDER;
}

CPluginInterfaceAbstract* WINAPI SalamanderPluginEntry(CSalamanderPluginEntryAbstract* salamander)
{
    SalamanderDebug = salamander->GetSalamanderDebug();
    SalamanderVersion = salamander->GetVersion();

    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), PluginNameEN);
    if (HLanguage == NULL)
        HLanguage = DLLInstance;

    if (SalamanderVersion < 102)
    {
        MessageBoxA(salamander->GetParentWindow(),
                    LoadStr(IDS_REQUIRE_SAL500),
                    PluginNameEN, MB_OK | MB_ICONERROR);
        return NULL;
    }

    SalamanderGeneral = salamander->GetSalamanderGeneral();
    SalamanderGeneral->GetLowerAndUpperCase(&LowerCase, &UpperCase);
    SalamanderGUI = salamander->GetSalamanderGUI();

    if (!InitializeWinLib(PluginNameEN, DLLInstance))
        return NULL;

    BOOL useDark = FALSE;
    if (SalamanderGeneral->GetConfigParameter(SALCFG_USEWINDOWSDARKMODE, &useDark, sizeof(useDark), NULL))
    {
        PluginDarkMode_SetHostPolicyAvailable(TRUE, useDark);
    }
    GDriveDarkMode::InitTheme();

    salamander->SetBasicPluginData(LoadStr(IDS_PLUGINNAME),
                                   FUNCTION_FILESYSTEM | FUNCTION_CONFIGURATION | FUNCTION_LOADSAVECONFIGURATION,
                                   VERSINFO_VERSION_NO_PLATFORM,
                                   VERSINFO_COPYRIGHT,
                                   LoadStr(IDS_PLUGIN_DESCRIPTION),
                                   PluginNameShort,
                                   NULL,
                                   "gdrive");

    SalamanderGeneral->GetPluginFSName(AssignedFSName, 0);
    AssignedFSNameLen = (int)strlen(AssignedFSName);

    salamander->SetPluginHomePageURL(LoadStr(IDS_PLUGIN_HOME));

    return &PluginInterface;
}

} // extern "C"

//
// CPluginInterface implementation
//

#include "gdrive_cache.h"

void WINAPI CPluginInterface::About(HWND parent)
{
    OnAbout(parent);
}

BOOL WINAPI CPluginInterface::Release(HWND parent, BOOL force)
{
    GDriveCache::CacheManager::GetInstance().SaveToDisk();
    GDriveDarkMode::ReleaseTheme();
    ReleaseWinLib(DLLInstance);
    return TRUE;
}

void WINAPI CPluginInterface::LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    if (!registry || !regKey) return;

    DWORD dwShared = 1;
    if (registry->GetValue(regKey, "IncludeSharedDrives", REG_DWORD, &dwShared, sizeof(dwShared)))
    {
        CfgIncludeSharedDrives = (dwShared != 0);
    }

    DWORD dwCacheEnabled = 1;
    if (registry->GetValue(regKey, "CacheEnabled", REG_DWORD, &dwCacheEnabled, sizeof(dwCacheEnabled)))
    {
        GDriveCache::CacheManager::GetInstance().SetEnabled(dwCacheEnabled != 0);
    }

    DWORD dwCacheInterval = 30000;
    if (registry->GetValue(regKey, "CacheCheckIntervalMs", REG_DWORD, &dwCacheInterval, sizeof(dwCacheInterval)))
    {
        GDriveCache::CacheManager::GetInstance().SetCheckIntervalMs(dwCacheInterval);
    }

    char szClientId[512] = {0};
    if (registry->GetValue(regKey, "ClientId", REG_SZ, szClientId, sizeof(szClientId)))
    {
        if (szClientId[0])
            GDriveAuth::AuthManager::GetInstance().SetClientId(szClientId);
    }

    char szClientSecret[512] = {0};
    if (registry->GetValue(regKey, "ClientSecret", REG_SZ, szClientSecret, sizeof(szClientSecret)))
    {
        if (szClientSecret[0])
            GDriveAuth::AuthManager::GetInstance().SetClientSecret(szClientSecret);
    }

    GDriveAuth::AuthManager::GetInstance().LoadSavedTokens();
}

void WINAPI CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    if (!registry || !regKey) return;

    DWORD dwShared = CfgIncludeSharedDrives ? 1 : 0;
    registry->SetValue(regKey, "IncludeSharedDrives", REG_DWORD, &dwShared, sizeof(dwShared));

    DWORD dwCacheEnabled = GDriveCache::CacheManager::GetInstance().IsEnabled() ? 1 : 0;
    registry->SetValue(regKey, "CacheEnabled", REG_DWORD, &dwCacheEnabled, sizeof(dwCacheEnabled));

    DWORD dwCacheInterval = GDriveCache::CacheManager::GetInstance().GetCheckIntervalMs();
    registry->SetValue(regKey, "CacheCheckIntervalMs", REG_DWORD, &dwCacheInterval, sizeof(dwCacheInterval));

    std::string cid = GDriveAuth::AuthManager::GetInstance().GetClientId();
    registry->SetValue(regKey, "ClientId", REG_SZ, cid.c_str(), (int)cid.length() + 1);

    std::string sec = GDriveAuth::AuthManager::GetInstance().GetClientSecret();
    registry->SetValue(regKey, "ClientSecret", REG_SZ, sec.c_str(), (int)sec.length() + 1);

    GDriveCache::CacheManager::GetInstance().SaveToDisk();
}

void WINAPI CPluginInterface::Configuration(HWND parent)
{
    OnConfiguration(parent);
}

void WINAPI CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_CONNECT), 0, CM_OPEN_GDRIVE, FALSE,
                            MENU_EVENT_TRUE, 0, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_DISCONNECT), 0, CM_DISCONNECT, FALSE,
                            MENU_EVENT_TRUE, MENU_EVENT_THIS_PLUGIN_FS, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, NULL, 0, 0, FALSE, 0, 0, MENU_SKILLLEVEL_ALL); // separator
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_CONFIG), 0, CM_CONFIG, FALSE,
                            MENU_EVENT_TRUE, 0, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_ABOUT), 0, CM_ABOUT, FALSE,
                            MENU_EVENT_TRUE, 0, MENU_SKILLLEVEL_ALL);

    CGUIIconListAbstract* iconList = SalamanderGUI->CreateIconList();
    if (iconList)
    {
        iconList->Create(16, 16, 1);
        HICON hIcon = (HICON)LoadImage(DLLInstance, MAKEINTRESOURCE(IDI_GDRIVE), IMAGE_ICON, 16, 16, SalamanderGeneral->GetIconLRFlags());
        iconList->ReplaceIcon(0, hIcon);
        DestroyIcon(hIcon);
        salamander->SetIconListForGUI(iconList);
    }

    salamander->SetChangeDriveMenuItem("\tGoogle Drive", 0);
    salamander->SetPluginIcon(0);
    salamander->SetPluginMenuAndToolbarIcon(0);
}

CPluginInterfaceForMenuExtAbstract* WINAPI CPluginInterface::GetInterfaceForMenuExt()
{
    return &InterfaceForMenuExt;
}

CPluginInterfaceForFSAbstract* WINAPI CPluginInterface::GetInterfaceForFS()
{
    return &InterfaceForFS;
}



