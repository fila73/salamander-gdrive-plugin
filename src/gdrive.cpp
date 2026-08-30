// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "gdrive.h"
#include "gdrive_auth.h"
#include "dialogs.h"
#include "gdrive_fs.h"
#include "gdrive_cache.h"
#include "gdrive_log.h"

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
BOOL CfgSanitizeInvalidChars = TRUE;
char CfgSanitizeChar = '_';
std::string CfgClientId = "";
std::string CfgClientSecret = "";

CPluginInterface PluginInterface;

//
// Hook procedure for panel Spacebar key detection
//
static HHOOK s_hGetMsgHook = NULL;

static LRESULT CALLBACK GetMsgHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0 && wParam == PM_REMOVE)
    {
        MSG* pMsg = (MSG*)lParam;
        if (pMsg && pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_SPACE)
        {
            if (SalamanderGeneral)
            {
                HWND hMain = SalamanderGeneral->GetMainWindowHWND();
                HWND hActive = GetActiveWindow();

                // Only intercept if Salamander main window is active (no dialogs, popups or message boxes open)
                if (hMain != NULL && hActive == hMain && pMsg->hwnd != NULL && IsChild(hMain, pMsg->hwnd))
                {
                    char className[64] = {0};
                    GetClassNameA(pMsg->hwnd, className, sizeof(className));

                    // Do not intercept if typing in an editbox, pressing a button, or using a combobox/listbox
                    if (_stricmp(className, "Edit") != 0 &&
                        _stricmp(className, "Button") != 0 &&
                        _stricmp(className, "ComboBox") != 0 &&
                        _stricmp(className, "ListBox") != 0 &&
                        _stricmp(className, "RichEdit") != 0 &&
                        _stricmp(className, "RichEdit20W") != 0 &&
                        _stricmp(className, "RichEdit20A") != 0)
                    {
                        CPluginFSInterfaceAbstract* activeFS = SalamanderGeneral->GetPanelPluginFS(PANEL_SOURCE);
                        if (activeFS)
                        {
                            BOOL isDir = FALSE;
                            const CFileData* f = SalamanderGeneral->GetPanelFocusedItem(PANEL_SOURCE, &isDir);
                            if (f && isDir && strcmp(f->Name, "..") != 0)
                            {
                                CPluginFS* gdriveFS = (CPluginFS*)activeFS;
                                gdriveFS->OnSpacePressedOnFolder(PANEL_SOURCE, f);
                            }
                        }
                    }
                }
            }
        }
    }
    return CallNextHookEx(s_hGetMsgHook, nCode, wParam, lParam);
}

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

    if (!s_hGetMsgHook)
    {
        s_hGetMsgHook = SetWindowsHookEx(WH_GETMESSAGE, GetMsgHookProc, NULL, GetCurrentThreadId());
    }

    return &PluginInterface;
}

} // extern "C"

//
// CPluginInterface implementation
//

void WINAPI CPluginInterface::About(HWND parent)
{
    OnAbout(parent);
}

BOOL WINAPI CPluginInterface::Release(HWND parent, BOOL force)
{
    if (s_hGetMsgHook)
    {
        UnhookWindowsHookEx(s_hGetMsgHook);
        s_hGetMsgHook = NULL;
    }

    GDriveCache::CacheManager::GetInstance().SaveToDisk();
    GDriveDarkMode::ReleaseTheme();
    ReleaseWinLib(DLLInstance);
    return TRUE;
}

void WINAPI CPluginInterface::LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    if (!registry || !regKey) return;

    DWORD dwShared = 0;
    if (registry->GetValue(regKey, "IncludeSharedDrives", REG_DWORD, &dwShared, sizeof(dwShared)))
    {
        CfgIncludeSharedDrives = (dwShared != 0);
    }

    DWORD dwSanitize = 1;
    if (registry->GetValue(regKey, "SanitizeInvalidChars", REG_DWORD, &dwSanitize, sizeof(dwSanitize)))
    {
        CfgSanitizeInvalidChars = (dwSanitize != 0);
    }

    char szSanitizeChar[8] = {0};
    if (registry->GetValue(regKey, "SanitizeChar", REG_SZ, szSanitizeChar, sizeof(szSanitizeChar)))
    {
        if (szSanitizeChar[0]) CfgSanitizeChar = szSanitizeChar[0];
    }

    DWORD dwCacheEnabled = 1;
    if (registry->GetValue(regKey, "CacheEnabled", REG_DWORD, &dwCacheEnabled, sizeof(dwCacheEnabled)))
    {
        GDriveCache::CacheManager::GetInstance().SetEnabled(dwCacheEnabled != 0);
        GDriveLog::Log("[CONFIG] LoadConfiguration: CacheEnabled=%u", dwCacheEnabled);
    }
    else
    {
        GDriveCache::CacheManager::GetInstance().SetEnabled(true);
        GDriveLog::Log("[CONFIG] LoadConfiguration: CacheEnabled not found, default to 1");
    }

    DWORD dwCacheInterval = 30000;
    if (registry->GetValue(regKey, "CacheCheckIntervalMs", REG_DWORD, &dwCacheInterval, sizeof(dwCacheInterval)))
    {
        GDriveCache::CacheManager::GetInstance().SetCheckIntervalMs(dwCacheInterval);
    }

    DWORD dwSmartCtrlR = 0;
    if (registry->GetValue(regKey, "SmartCtrlR", REG_DWORD, &dwSmartCtrlR, sizeof(dwSmartCtrlR)))
    {
        GDriveCache::CacheManager::GetInstance().SetSmartCtrlR(dwSmartCtrlR != 0);
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

    DWORD dwSanitize = CfgSanitizeInvalidChars ? 1 : 0;
    registry->SetValue(regKey, "SanitizeInvalidChars", REG_DWORD, &dwSanitize, sizeof(dwSanitize));

    char szSanitizeChar[2] = { CfgSanitizeChar, '\0' };
    registry->SetValue(regKey, "SanitizeChar", REG_SZ, szSanitizeChar, (int)strlen(szSanitizeChar) + 1);

    DWORD dwCacheEnabled = GDriveCache::CacheManager::GetInstance().IsEnabled() ? 1 : 0;
    registry->SetValue(regKey, "CacheEnabled", REG_DWORD, &dwCacheEnabled, sizeof(dwCacheEnabled));

    DWORD dwCacheInterval = GDriveCache::CacheManager::GetInstance().GetCheckIntervalMs();
    registry->SetValue(regKey, "CacheCheckIntervalMs", REG_DWORD, &dwCacheInterval, sizeof(dwCacheInterval));

    DWORD dwSmartCtrlR = GDriveCache::CacheManager::GetInstance().IsSmartCtrlR() ? 1 : 0;
    registry->SetValue(regKey, "SmartCtrlR", REG_DWORD, &dwSmartCtrlR, sizeof(dwSmartCtrlR));

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



