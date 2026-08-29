// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <queue>
#include "dialogs.h"
#include "gdrive_auth.h"
#include "gdrive.h"
#include "gdrive_api.h"

CCommonDialog::CCommonDialog(HINSTANCE hInstance, int resID, HWND hParent, CObjectOrigin origin)
    : CDialog(hInstance, resID, hParent, origin)
{
}

CCommonDialog::CCommonDialog(HINSTANCE hInstance, int resID, int helpID, HWND hParent, CObjectOrigin origin)
    : CDialog(hInstance, resID, helpID, hParent, origin)
{
}

INT_PTR CCommonDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_INITDIALOG && Parent != NULL)
    {
        SalamanderGeneral->MultiMonCenterWindow(HWindow, Parent, TRUE);
    }
    return CDialog::DialogProc(uMsg, wParam, lParam);
}

void CCommonDialog::NotifDlgJustCreated()
{
    SalamanderGUI->ArrangeHorizontalLines(HWindow);
}

void CCommonPropSheetPage::NotifDlgJustCreated()
{
    SalamanderGUI->ArrangeHorizontalLines(HWindow);
}

#include "gdrive_cache.h"

CConfigPageGeneral::CConfigPageGeneral()
    : CCommonPropSheetPage(LoadStr(IDS_CFG_PAGE_GENERAL), HLanguage, IDD_CFGPAGEGENERAL, IDD_CFGPAGEGENERAL, PSP_HASHELP, NULL)
{
}

void CConfigPageGeneral::RefreshAccountsList()
{
    HWND hCombo = GetDlgItem(HWindow, IDC_CFG_ACCOUNTS_COMBO);
    if (!hCombo) return;

    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);

    auto accounts = GDriveAuth::AuthManager::GetInstance().GetAccounts();
    int activeIndex = -1;

    for (size_t i = 0; i < accounts.size(); ++i)
    {
        std::string display = accounts[i].email;
        if (!accounts[i].displayName.empty() && accounts[i].displayName != accounts[i].email)
        {
            display += " (" + accounts[i].displayName + ")";
        }
        if (accounts[i].isActive)
        {
            display += " [Active]";
            activeIndex = (int)i;
        }

        int idx = (int)SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)display.c_str());
        SendMessage(hCombo, CB_SETITEMDATA, idx, (LPARAM)i);
    }

    if (accounts.empty())
    {
        SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)LoadStr(IDS_NOT_LOGGED_IN));
        SendMessage(hCombo, CB_SETCURSEL, 0, 0);
        EnableWindow(GetDlgItem(HWindow, IDC_CFG_ACCOUNT_SWITCH_BTN), FALSE);
        EnableWindow(GetDlgItem(HWindow, IDC_CFG_ACCOUNT_REMOVE_BTN), FALSE);
    }
    else
    {
        SendMessage(hCombo, CB_SETCURSEL, activeIndex >= 0 ? activeIndex : 0, 0);
        EnableWindow(GetDlgItem(HWindow, IDC_CFG_ACCOUNT_SWITCH_BTN), TRUE);
        EnableWindow(GetDlgItem(HWindow, IDC_CFG_ACCOUNT_REMOVE_BTN), TRUE);
    }
}

void CConfigPageGeneral::Transfer(CTransferInfo& ti)
{
    char szClientId[512] = {0};
    char szClientSecret[512] = {0};
    if (ti.Type == ttDataToWindow)
    {
        std::string id = GDriveAuth::AuthManager::GetInstance().GetClientId();
        strncpy(szClientId, id.c_str(), sizeof(szClientId) - 1);
        std::string sec = GDriveAuth::AuthManager::GetInstance().GetClientSecret();
        strncpy(szClientSecret, sec.c_str(), sizeof(szClientSecret) - 1);
    }

    ti.EditLine(IDC_CFG_CLIENTID, szClientId, sizeof(szClientId));
    ti.EditLine(IDC_CFG_CLIENTSECRET, szClientSecret, sizeof(szClientSecret));

    int useShared = CfgIncludeSharedDrives ? 1 : 0;
    ti.CheckBox(IDC_CFG_USE_SHARED_DRIVES, useShared);

    if (ti.Type == ttDataFromWindow)
    {
        GDriveAuth::AuthManager::GetInstance().SetClientId(szClientId);
        GDriveAuth::AuthManager::GetInstance().SetClientSecret(szClientSecret);
        CfgIncludeSharedDrives = (useShared != 0);
    }
}

INT_PTR CConfigPageGeneral::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_INITDIALOG)
    {
        RefreshAccountsList();
    }
    else if (uMsg == WM_COMMAND)
    {
        WORD ctrlId = LOWORD(wParam);
        WORD notifyCode = HIWORD(wParam);

        if (ctrlId == IDC_CFG_ACCOUNT_ADD_BTN && notifyCode == BN_CLICKED)
        {
            char szClientId[512] = {0};
            char szClientSecret[512] = {0};
            GetDlgItemTextA(HWindow, IDC_CFG_CLIENTID, szClientId, sizeof(szClientId));
            GetDlgItemTextA(HWindow, IDC_CFG_CLIENTSECRET, szClientSecret, sizeof(szClientSecret));
            GDriveAuth::AuthManager::GetInstance().SetClientId(szClientId);
            GDriveAuth::AuthManager::GetInstance().SetClientSecret(szClientSecret);

            std::string err;
            if (GDriveAuth::AuthManager::GetInstance().AddAccount(HWindow, &err))
            {
                SalamanderGeneral->SalMessageBox(HWindow, LoadStr(IDS_STATUS_AUTH_SUCCESS), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONINFORMATION);
            }
            else
            {
                SalamanderGeneral->SalMessageBox(HWindow, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            }
            RefreshAccountsList();
            return TRUE;
        }
        else if (ctrlId == IDC_CFG_ACCOUNTS_COMBO && notifyCode == CBN_SELCHANGE)
        {
            HWND hCombo = GetDlgItem(HWindow, IDC_CFG_ACCOUNTS_COMBO);
            int curSel = (int)SendMessage(hCombo, CB_GETCURSEL, 0, 0);
            auto accounts = GDriveAuth::AuthManager::GetInstance().GetAccounts();
            if (curSel >= 0 && curSel < (int)accounts.size())
            {
                std::string cId = accounts[curSel].clientId.empty() ? GDriveAuth::AuthManager::GetInstance().GetClientId() : accounts[curSel].clientId;
                std::string cSec = accounts[curSel].clientSecret.empty() ? GDriveAuth::AuthManager::GetInstance().GetClientSecret() : accounts[curSel].clientSecret;
                SetDlgItemTextA(HWindow, IDC_CFG_CLIENTID, cId.c_str());
                SetDlgItemTextA(HWindow, IDC_CFG_CLIENTSECRET, cSec.c_str());
            }
            return TRUE;
        }
        else if (ctrlId == IDC_CFG_ACCOUNT_SWITCH_BTN && notifyCode == BN_CLICKED)
        {
            HWND hCombo = GetDlgItem(HWindow, IDC_CFG_ACCOUNTS_COMBO);
            int curSel = (int)SendMessage(hCombo, CB_GETCURSEL, 0, 0);
            auto accounts = GDriveAuth::AuthManager::GetInstance().GetAccounts();
            if (curSel >= 0 && curSel < (int)accounts.size())
            {
                GDriveAuth::AuthManager::GetInstance().SwitchAccount(accounts[curSel].email);
                SetDlgItemTextA(HWindow, IDC_CFG_CLIENTID, GDriveAuth::AuthManager::GetInstance().GetClientId().c_str());
                SetDlgItemTextA(HWindow, IDC_CFG_CLIENTSECRET, GDriveAuth::AuthManager::GetInstance().GetClientSecret().c_str());
                RefreshAccountsList();
            }
            return TRUE;
        }
        else if (ctrlId == IDC_CFG_ACCOUNT_REMOVE_BTN && notifyCode == BN_CLICKED)
        {
            HWND hCombo = GetDlgItem(HWindow, IDC_CFG_ACCOUNTS_COMBO);
            int curSel = (int)SendMessage(hCombo, CB_GETCURSEL, 0, 0);
            auto accounts = GDriveAuth::AuthManager::GetInstance().GetAccounts();
            if (curSel >= 0 && curSel < (int)accounts.size())
            {
                char msg[512] = {0};
                snprintf(msg, sizeof(msg), LoadStr(IDS_CONFIRM_REMOVE_ACCOUNT), accounts[curSel].email.c_str());
                if (SalamanderGeneral->SalMessageBox(HWindow, msg, LoadStr(IDS_PLUGINNAME), MB_YESNO | MB_ICONQUESTION) == IDYES)
                {
                    GDriveAuth::AuthManager::GetInstance().RemoveAccount(accounts[curSel].email);
                    SetDlgItemTextA(HWindow, IDC_CFG_CLIENTID, GDriveAuth::AuthManager::GetInstance().GetClientId().c_str());
                    SetDlgItemTextA(HWindow, IDC_CFG_CLIENTSECRET, GDriveAuth::AuthManager::GetInstance().GetClientSecret().c_str());
                    RefreshAccountsList();
                }
            }
            return TRUE;
        }
    }

    return CCommonPropSheetPage::DialogProc(uMsg, wParam, lParam);
}

//
// CConfigPageCache
//

CConfigPageCache::CConfigPageCache()
    : CCommonPropSheetPage(LoadStr(IDS_CFG_PAGE_CACHE), HLanguage, IDD_CFGPAGECACHE, IDD_CFGPAGECACHE, PSP_HASHELP, NULL)
{
}

void CConfigPageCache::UpdateCacheStatus()
{
    std::string acc = GDriveCache::CacheManager::GetInstance().GetCurrentAccount();
    char buf[256] = {0};
    if (!acc.empty())
    {
        snprintf(buf, sizeof(buf), "Active account: %s (Isolated cache)", acc.c_str());
    }
    else
    {
        snprintf(buf, sizeof(buf), "No active account");
    }
    SetDlgItemTextA(HWindow, IDC_CFG_CACHE_STATUS, buf);
}

void CConfigPageCache::Transfer(CTransferInfo& ti)
{
    int enabled = GDriveCache::CacheManager::GetInstance().IsEnabled() ? 1 : 0;
    ti.CheckBox(IDC_CFG_CACHE_ENABLED, enabled);

    int smartCtrlR = GDriveCache::CacheManager::GetInstance().IsSmartCtrlR() ? 1 : 0;
    ti.CheckBox(IDC_CFG_SMART_CTRL_R, smartCtrlR);

    int sanitize = CfgSanitizeInvalidChars ? 1 : 0;
    ti.CheckBox(IDC_CFG_SANITIZE_INVALID_CHARS, sanitize);

    char szSanitizeChar[8] = { CfgSanitizeChar, '\0' };
    ti.EditLine(IDC_CFG_SANITIZE_CHAR, szSanitizeChar, sizeof(szSanitizeChar));

    if (ti.Type == ttDataFromWindow)
    {
        GDriveCache::CacheManager::GetInstance().SetEnabled(enabled != 0);
        GDriveCache::CacheManager::GetInstance().SetSmartCtrlR(smartCtrlR != 0);
        CfgSanitizeInvalidChars = (sanitize != 0);
        if (szSanitizeChar[0]) CfgSanitizeChar = szSanitizeChar[0];

        HWND hCombo = GetDlgItem(HWindow, IDC_CFG_CACHE_INTERVAL_COMBO);
        if (hCombo)
        {
            int curSel = (int)SendMessage(hCombo, CB_GETCURSEL, 0, 0);
            DWORD ms = 30000;
            switch (curSel)
            {
            case 0: ms = 10000; break;
            case 1: ms = 30000; break;
            case 2: ms = 60000; break;
            case 3: ms = 300000; break;
            case 4: ms = 0xFFFFFFFF; break;
            default: ms = 30000; break;
            }
            GDriveCache::CacheManager::GetInstance().SetCheckIntervalMs(ms);
        }
    }
}

INT_PTR CConfigPageCache::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_INITDIALOG)
    {
        HWND hCombo = GetDlgItem(HWindow, IDC_CFG_CACHE_INTERVAL_COMBO);
        if (hCombo)
        {
            SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
            SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)LoadStr(IDS_TTL_10SEC));
            SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)LoadStr(IDS_TTL_30SEC));
            SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)LoadStr(IDS_TTL_1MIN));
            SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)LoadStr(IDS_TTL_5MIN));
            SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)LoadStr(IDS_TTL_MANUAL));

            DWORD curMs = GDriveCache::CacheManager::GetInstance().GetCheckIntervalMs();
            int sel = 1; // 30s default
            if (curMs <= 10000) sel = 0;
            else if (curMs <= 30000) sel = 1;
            else if (curMs <= 60000) sel = 2;
            else if (curMs <= 300000) sel = 3;
            else sel = 4;

            SendMessage(hCombo, CB_SETCURSEL, sel, 0);
        }
        UpdateCacheStatus();
    }
    else if (uMsg == WM_COMMAND)
    {
        WORD ctrlId = LOWORD(wParam);
        WORD notifyCode = HIWORD(wParam);

        if (ctrlId == IDC_CFG_CACHE_CLEAR_BTN && notifyCode == BN_CLICKED)
        {
            GDriveCache::CacheManager::GetInstance().ClearDiskCache();
            SalamanderGeneral->SalMessageBox(HWindow, LoadStr(IDS_CACHE_CLEARED), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONINFORMATION);
            UpdateCacheStatus();
            return TRUE;
        }
    }

    return CCommonPropSheetPage::DialogProc(uMsg, wParam, lParam);
}

class CCenteredPropertyWindow : public CWindow
{
protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg)
        {
        case WM_WINDOWPOSCHANGING:
        {
            WINDOWPOS* pos = (WINDOWPOS*)lParam;
            if (pos->flags & SWP_SHOWWINDOW)
            {
                HWND hParent = GetParent(HWindow);
                if (hParent != NULL)
                    SalamanderGeneral->MultiMonCenterWindow(HWindow, hParent, TRUE);
            }
            break;
        }
        case WM_APP + 1000:
        {
            DetachWindow();
            delete this;
            return 0;
        }
        }
        return CWindow::WindowProc(uMsg, wParam, lParam);
    }
};

static int CALLBACK CenterCallback(HWND HWindow, UINT uMsg, LPARAM lParam)
{
    if (uMsg == PSCB_INITIALIZED)
    {
        CCenteredPropertyWindow* wnd = new CCenteredPropertyWindow;
        if (wnd != NULL)
        {
            wnd->AttachToWindow(HWindow);
            if (wnd->HWindow == NULL)
                delete wnd;
            else
                PostMessage(wnd->HWindow, WM_APP + 1000, 0, 0);
        }
    }
    return 0;
}

static DWORD LastCfgPage = 0;

CConfigDialog::CConfigDialog(HWND parent)
    : CPropertyDialog(parent, HLanguage, LoadStr(IDS_CFG_TITLE),
                      LastCfgPage, PSH_USECALLBACK | PSH_NOAPPLYNOW | PSH_HASHELP,
                      NULL, &LastCfgPage, CenterCallback)
{
    Add(&PageGeneral);
    Add(&PageCache);
}

void OnConfiguration(HWND hParent)
{
    static BOOL InConfiguration = FALSE;
    if (InConfiguration)
    {
        SalamanderGeneral->SalMessageBox(hParent, LoadStr(IDS_CFG_ALREADY_OPENED), LoadStr(IDS_PLUGINNAME),
                                         MB_ICONINFORMATION | MB_OK);
        return;
    }
    InConfiguration = TRUE;
    CConfigDialog(hParent).Execute();
    InConfiguration = FALSE;
}

void OnAbout(HWND hParent)
{
    char buf[1024];
    snprintf(buf, sizeof(buf),
             "%s %s\n\n"
             "%s\n\n"
             "%s",
             LoadStr(IDS_PLUGINNAME), VERSINFO_VERSION,
             LoadStr(IDS_PLUGIN_DESCRIPTION),
             VERSINFO_COPYRIGHT);
    SalamanderGeneral->SalMessageBox(hParent, buf, LoadStr(IDS_ABOUT), MB_OK | MB_ICONINFORMATION);
}

//
// CCalcSizeProgressDialog
//

CCalcSizeProgressDialog::CCalcSizeProgressDialog(HWND hParent, const char* itemName,
                                                 const std::string& folderId,
                                                 const std::string& driveId,
                                                 bool isSharedDrive)
    : CCommonDialog(HLanguage, IDD_CALC_SIZE_PROGRESS, hParent, ooStatic),
      m_itemName(itemName ? itemName : ""),
      m_folderId(folderId),
      m_driveId(driveId),
      m_isSharedDrive(isSharedDrive),
      m_totalBytes(0),
      m_totalFiles(0),
      m_totalDirs(0),
      m_cancelled(false)
{
}

INT_PTR CCalcSizeProgressDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_INITDIALOG)
    {
        SetDlgItemTextA(HWindow, IDC_CALC_CURRENT_FOLDER, m_itemName.c_str());
        HWND hProg = GetDlgItem(HWindow, IDC_CALC_PROGRESS_BAR);
        if (hProg)
        {
            LONG_PTR style = GetWindowLongPtr(hProg, GWL_STYLE);
            SetWindowLongPtr(hProg, GWL_STYLE, style | PBS_MARQUEE);
            SendMessage(hProg, PBM_SETMARQUEE, TRUE, 30);
        }
    }
    else if (uMsg == WM_COMMAND)
    {
        if (LOWORD(wParam) == IDCANCEL)
        {
            m_cancelled = true;
            return TRUE;
        }
    }
    else if (uMsg == WM_CLOSE)
    {
        m_cancelled = true;
        return TRUE;
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

void CCalcSizeProgressDialog::ProcessMessages()
{
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
        {
            m_cancelled = true;
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (HWindow && IsDialogMessage(HWindow, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void CCalcSizeProgressDialog::UpdateUI(const std::string& currentScanningFolder)
{
    if (!HWindow) return;
    if (!currentScanningFolder.empty())
    {
        SetDlgItemTextA(HWindow, IDC_CALC_CURRENT_FOLDER, currentScanningFolder.c_str());
    }
    SetDlgItemTextA(HWindow, IDC_CALC_DIRS_COUNT, FormatNumber(m_totalDirs).c_str());
    SetDlgItemTextA(HWindow, IDC_CALC_FILES_COUNT, FormatNumber(m_totalFiles).c_str());
    SetDlgItemTextA(HWindow, IDC_CALC_TOTAL_SIZE, FormatSize(m_totalBytes).c_str());
}

std::string CCalcSizeProgressDialog::FormatSize(int64_t bytes)
{
    char buf[64];
    if (bytes < 1024)
        snprintf(buf, sizeof(buf), "%lld B", (long long)bytes);
    else if (bytes < 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.2f KB", (double)bytes / 1024.0);
    else if (bytes < 1024LL * 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.2f MB", (double)bytes / (1024.0 * 1024.0));
    else if (bytes < 1024LL * 1024 * 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    else
        snprintf(buf, sizeof(buf), "%.2f TB", (double)bytes / (1024.0 * 1024.0 * 1024.0 * 1024.0));
    return buf;
}

std::string CCalcSizeProgressDialog::FormatNumber(int64_t num)
{
    std::string s = std::to_string(num);
    int n = (int)s.length() - 3;
    while (n > 0)
    {
        s.insert(n, " ");
        n -= 3;
    }
    return s;
}

bool CCalcSizeProgressDialog::Run()
{
    if (!Create()) return false;

    ShowWindow(HWindow, SW_SHOW);
    UpdateWindow(HWindow);
    HWND hParent = Parent;
    if (hParent) EnableWindow(hParent, FALSE);

    GDriveCache::CacheManager::GetInstance().CheckForRemoteChanges(false);

    std::queue<std::string> folderQueue;
    std::vector<std::string> visitedFolderIds;
    std::map<std::string, int64_t> directFolderBytes;
    std::map<std::string, std::vector<std::string>> childFoldersMap;
    folderQueue.push(m_folderId);

    while (!folderQueue.empty() && !m_cancelled)
    {
        std::string currentId = folderQueue.front();
        folderQueue.pop();
        visitedFolderIds.push_back(currentId);

        ProcessMessages();
        if (m_cancelled) break;

        std::vector<GDriveApi::GDriveItem> items;
        std::string err;
        bool listOk = false;

        // 1. Check cache first
        if (currentId != "shared_with_me_root" && GDriveCache::CacheManager::GetInstance().GetFolder(currentId, items))
        {
            listOk = true;
        }
        else
        {
            if (currentId == "shared_with_me_root")
            {
                listOk = GDriveApi::ApiClient::GetInstance().ListSharedWithMe(items, &err);
            }
            else
            {
                listOk = GDriveApi::ApiClient::GetInstance().ListFolder(currentId, m_driveId, m_isSharedDrive, items, &err);
            }

            if (listOk && currentId != "shared_with_me_root")
            {
                GDriveCache::CacheManager::GetInstance().PutFolder(currentId, items);
            }
        }

        if (!listOk)
        {
            continue;
        }

        int64_t myFilesBytes = 0;
        std::vector<std::string> mySubfolders;

        for (const auto& it : items)
        {
            ProcessMessages();
            if (m_cancelled) break;

            if (it.isFolder)
            {
                m_totalDirs++;
                mySubfolders.push_back(it.id);
                folderQueue.push(it.id);
                UpdateUI(it.name);
            }
            else
            {
                m_totalFiles++;
                m_totalBytes += it.size;
                myFilesBytes += it.size;
                UpdateUI("");
            }
        }

        directFolderBytes[currentId] = myFilesBytes;
        childFoldersMap[currentId] = mySubfolders;
    }

    if (!m_cancelled)
    {
        std::map<std::string, int64_t> computedFolderSizes;
        // Compute and store exact sizes for every visited subfolder (bottom-up from leaves to root)
        for (auto it = visitedFolderIds.rbegin(); it != visitedFolderIds.rend(); ++it)
        {
            const std::string& id = *it;
            int64_t totalForThisFolder = directFolderBytes[id];
            auto itChildren = childFoldersMap.find(id);
            if (itChildren != childFoldersMap.end())
            {
                for (const auto& childId : itChildren->second)
                {
                    auto itChildSize = computedFolderSizes.find(childId);
                    if (itChildSize != computedFolderSizes.end())
                    {
                        totalForThisFolder += itChildSize->second;
                    }
                }
            }
            computedFolderSizes[id] = totalForThisFolder;
            GDriveCache::CacheManager::GetInstance().SetFolderSize(id, totalForThisFolder);
        }
        GDriveCache::CacheManager::GetInstance().SetFolderSize(m_folderId, m_totalBytes);
    }

    GDriveCache::CacheManager::GetInstance().SaveToDisk();

    if (hParent) EnableWindow(hParent, TRUE);
    if (HWindow) DestroyWindow(HWindow);

    char msg[1024];
    std::string szStr = FormatSize(m_totalBytes);
    std::string numStr = FormatNumber(m_totalBytes);

    if (m_cancelled)
    {
        _snprintf_s(msg, _TRUNCATE, LoadStr(IDS_CALC_CANCELLED_FMT),
                    m_itemName.c_str(), m_totalDirs, m_totalFiles, szStr.c_str(), numStr.c_str());
        SalamanderGeneral->SalMessageBox(hParent, msg, LoadStr(IDS_CALC_TITLE), MB_OK | MB_ICONEXCLAMATION);
    }
    else
    {
        _snprintf_s(msg, _TRUNCATE, LoadStr(IDS_CALC_RESULT_FMT),
                    m_itemName.c_str(), m_totalDirs, m_totalFiles, szStr.c_str(), numStr.c_str());
        SalamanderGeneral->SalMessageBox(hParent, msg, LoadStr(IDS_CALC_TITLE), MB_OK | MB_ICONINFORMATION);
    }

    return !m_cancelled;
}

//
// CTransferProgressDialog
//

CTransferProgressDialog::CTransferProgressDialog(HWND hParent, bool isUpload, const std::string& fileName, int64_t totalBytes)
    : CCommonDialog(HLanguage, IDD_TRANSFER_PROGRESS, hParent, ooStatic),
      m_isUpload(isUpload),
      m_fileName(fileName),
      m_totalBytes(totalBytes),
      m_cancelled(false),
      m_startTick(0),
      m_lastUpdateTick(0)
{
}

CTransferProgressDialog::~CTransferProgressDialog()
{
    Stop();
}

INT_PTR CTransferProgressDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_COMMAND)
    {
        if (LOWORD(wParam) == IDCANCEL)
        {
            m_cancelled = true;
            return TRUE;
        }
    }
    else if (uMsg == WM_CLOSE)
    {
        m_cancelled = true;
        return TRUE;
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

void CTransferProgressDialog::ProcessMessages()
{
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
        {
            m_cancelled = true;
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (HWindow && IsDialogMessage(HWindow, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

bool CTransferProgressDialog::Start()
{
    if (!Create()) return false;

    HWND hParent = Parent;
    if (hParent) EnableWindow(hParent, FALSE);

    ShowWindow(HWindow, SW_SHOW);
    UpdateWindow(HWindow);

    std::string actionText = LoadStr(m_isUpload ? IDS_TRANSFER_UPLOADING : IDS_TRANSFER_DOWNLOADING);
    SetDlgItemTextA(HWindow, IDC_TRANSFER_ACTION_LABEL, actionText.c_str());

    std::string ansiFileName = GDriveHttp::HttpClient::Utf8ToAnsi(m_fileName);
    SetDlgItemTextA(HWindow, IDC_TRANSFER_FILENAME, ansiFileName.c_str());

    HWND hPb = GetDlgItem(HWindow, IDC_TRANSFER_PROGRESSBAR);
    if (hPb)
    {
        SendMessage(hPb, PBM_SETRANGE32, 0, 1000);
        SendMessage(hPb, PBM_SETPOS, 0, 0);
    }

    m_startTick = GetTickCount();
    m_lastUpdateTick = m_startTick;

    UpdateUI(0, m_totalBytes);
    return true;
}

void CTransferProgressDialog::SetCurrentFile(const std::string& fileName, int64_t totalBytes)
{
    m_fileName = fileName;
    m_totalBytes = totalBytes;
    m_startTick = GetTickCount();
    m_lastUpdateTick = m_startTick;

    if (HWindow)
    {
        std::string ansiFileName = GDriveHttp::HttpClient::Utf8ToAnsi(m_fileName);
        SetDlgItemTextA(HWindow, IDC_TRANSFER_FILENAME, ansiFileName.c_str());

        HWND hPb = GetDlgItem(HWindow, IDC_TRANSFER_PROGRESSBAR);
        if (hPb)
        {
            SendMessage(hPb, PBM_SETPOS, 0, 0);
        }
        UpdateUI(0, m_totalBytes);
    }
}

void CTransferProgressDialog::Stop()
{
    if (HWindow)
    {
        HWND hParent = Parent;
        if (hParent)
        {
            EnableWindow(hParent, TRUE);
            SetForegroundWindow(hParent);
        }
        DestroyWindow(HWindow);
        HWindow = NULL;
    }
}

std::string CTransferProgressDialog::FormatSize(int64_t bytes)
{
    char buf[64];
    if (bytes < 1024)
        snprintf(buf, sizeof(buf), "%lld B", (long long)bytes);
    else if (bytes < 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.2f KB", (double)bytes / 1024.0);
    else if (bytes < 1024LL * 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.2f MB", (double)bytes / (1024.0 * 1024.0));
    else if (bytes < 1024LL * 1024 * 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    else
        snprintf(buf, sizeof(buf), "%.2f TB", (double)bytes / (1024.0 * 1024.0 * 1024.0 * 1024.0));
    return buf;
}

void CTransferProgressDialog::UpdateUI(int64_t bytesTransferred, int64_t totalBytes)
{
    if (!HWindow) return;

    int percent = 0;
    if (totalBytes > 0)
    {
        percent = (int)((bytesTransferred * 100) / totalBytes);
        if (percent > 100) percent = 100;
    }

    HWND hPb = GetDlgItem(HWindow, IDC_TRANSFER_PROGRESSBAR);
    if (hPb)
    {
        int pos = (int)((bytesTransferred * 1000) / (totalBytes > 0 ? totalBytes : 1));
        if (pos > 1000) pos = 1000;
        SendMessage(hPb, PBM_SETPOS, pos, 0);
    }

    char bytesBuf[128];
    std::string trStr = FormatSize(bytesTransferred);
    std::string totStr = FormatSize(totalBytes);
    snprintf(bytesBuf, sizeof(bytesBuf), LoadStr(IDS_TRANSFER_BYTES_FMT), trStr.c_str(), totStr.c_str(), percent);
    SetDlgItemTextA(HWindow, IDC_TRANSFER_BYTES, bytesBuf);

    DWORD now = GetTickCount();
    DWORD elapsedMs = now - m_startTick;
    if (elapsedMs > 500 && bytesTransferred > 0)
    {
        double speedMBs = ((double)bytesTransferred / (1024.0 * 1024.0)) / ((double)elapsedMs / 1000.0);
        char speedBuf[64];
        snprintf(speedBuf, sizeof(speedBuf), LoadStr(IDS_TRANSFER_SPEED_FMT), speedMBs);
        SetDlgItemTextA(HWindow, IDC_TRANSFER_SPEED, speedBuf);
    }

    UpdateWindow(HWindow);
}

bool CTransferProgressDialog::OnProgress(int64_t bytesTransferred, int64_t totalBytes)
{
    ProcessMessages();
    if (m_cancelled) return false;

    DWORD now = GetTickCount();
    if (now - m_lastUpdateTick >= 50 || bytesTransferred >= totalBytes)
    {
        m_lastUpdateTick = now;
        UpdateUI(bytesTransferred, totalBytes);
    }

    return !m_cancelled;
}
