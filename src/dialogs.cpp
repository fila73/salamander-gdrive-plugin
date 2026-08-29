// SPDX-FileCopyrightText: 2026 Open Salamander Authors & Red Salamander Authors
// SPDX-FileContributor: Ported to Open Salamander framework by fila73
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

CConfigPageGeneral::CConfigPageGeneral()
    : CCommonPropSheetPage(NULL, HLanguage, IDD_CFGPAGEGENERAL, IDD_CFGPAGEGENERAL, PSP_HASHELP, NULL)
{
}

void CConfigPageGeneral::UpdateStatusText()
{
    std::string status = GDriveAuth::AuthManager::GetInstance().GetAccountDisplay();
    SetDlgItemTextA(HWindow, IDC_CFG_ACCOUNT_STATUS, status.c_str());

    bool isAuth = GDriveAuth::AuthManager::GetInstance().IsAuthenticated();
    EnableWindow(GetDlgItem(HWindow, IDC_CFG_LOGOUT_BTN), isAuth ? TRUE : FALSE);
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
        UpdateStatusText();
    }
    else if (uMsg == WM_COMMAND)
    {
        WORD ctrlId = LOWORD(wParam);
        WORD notifyCode = HIWORD(wParam);

        if (ctrlId == IDC_CFG_LOGIN_BTN && notifyCode == BN_CLICKED)
        {
            char szClientId[512] = {0};
            char szClientSecret[512] = {0};
            GetDlgItemTextA(HWindow, IDC_CFG_CLIENTID, szClientId, sizeof(szClientId));
            GetDlgItemTextA(HWindow, IDC_CFG_CLIENTSECRET, szClientSecret, sizeof(szClientSecret));
            GDriveAuth::AuthManager::GetInstance().SetClientId(szClientId);
            GDriveAuth::AuthManager::GetInstance().SetClientSecret(szClientSecret);

            std::string err;
            if (GDriveAuth::AuthManager::GetInstance().LaunchInteractiveAuth(HWindow, &err))
            {
                SalamanderGeneral->SalMessageBox(HWindow, LoadStr(IDS_STATUS_AUTH_SUCCESS), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONINFORMATION);
            }
            else
            {
                SalamanderGeneral->SalMessageBox(HWindow, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            }
            UpdateStatusText();
            return TRUE;
        }
        else if (ctrlId == IDC_CFG_LOGOUT_BTN && notifyCode == BN_CLICKED)
        {
            if (SalamanderGeneral->SalMessageBox(HWindow, LoadStr(IDS_CONFIRM_DISCONNECT), LoadStr(IDS_PLUGINNAME), MB_YESNO | MB_ICONQUESTION) == IDYES)
            {
                GDriveAuth::AuthManager::GetInstance().Logout();
                UpdateStatusText();
            }
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

    std::queue<std::string> folderQueue;
    folderQueue.push(m_folderId);

    while (!folderQueue.empty() && !m_cancelled)
    {
        std::string currentId = folderQueue.front();
        folderQueue.pop();

        ProcessMessages();
        if (m_cancelled) break;

        std::vector<GDriveApi::GDriveItem> items;
        std::string err;
        bool listOk = false;
        if (currentId == "shared_with_me_root")
        {
            listOk = GDriveApi::ApiClient::GetInstance().ListSharedWithMe(items, &err);
        }
        else
        {
            listOk = GDriveApi::ApiClient::GetInstance().ListFolder(currentId, m_driveId, m_isSharedDrive, items, &err);
        }

        if (!listOk)
        {
            continue;
        }

        for (const auto& it : items)
        {
            ProcessMessages();
            if (m_cancelled) break;

            if (it.isFolder)
            {
                m_totalDirs++;
                folderQueue.push(it.id);
                UpdateUI(it.name);
            }
            else
            {
                m_totalFiles++;
                m_totalBytes += it.size;
                UpdateUI("");
            }
        }
    }

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
