// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dialog_find.h"
#include "dialogs.h"
#include "gdrive.h"
#include "gdrive.rh2"
#include "lang.rh"
#include "gdrive_log.h"
#include "gdrivedarkmode.h"
#include "gdrive_auth.h"
#include "gdrive_http.h"
#include "gdrive_cache.h"
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>

#define WM_USER_SEARCH_FINISHED (WM_USER + 101)
#define WM_USER_SEARCH_PROGRESS (WM_USER + 102)

static CGDriveFindDialog* s_activeFindDialog = nullptr;

CGDriveFindDialog::CGDriveFindDialog(HWND hParent, CPluginFS* pFS, int panel,
                                     const std::string& currentPath,
                                     const std::string& currentFolderId)
    : m_hParent(hParent),
      m_pFS(pFS),
      m_panel(panel),
      m_initialPath(currentPath),
      m_initialFolderId(currentFolderId)
{
}

CGDriveFindDialog::~CGDriveFindDialog()
{
    StopSearch();
    if (m_searchThread.joinable())
    {
        m_searchThread.join();
    }
}

bool CGDriveFindDialog::ShowModal()
{
    s_activeFindDialog = this;
    INT_PTR res = DialogBoxParam(HLanguage ? HLanguage : DLLInstance,
                                 MAKEINTRESOURCE(IDD_FIND),
                                 m_hParent,
                                 DialogProc,
                                 (LPARAM)this);
    s_activeFindDialog = nullptr;
    return res == IDOK;
}

INT_PTR CALLBACK CGDriveFindDialog::DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CGDriveFindDialog* pThis = nullptr;
    if (uMsg == WM_INITDIALOG)
    {
        pThis = reinterpret_cast<CGDriveFindDialog*>(lParam);
        SetWindowLongPtr(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(pThis));
        pThis->m_hDlg = hDlg;
    }
    else
    {
        pThis = reinterpret_cast<CGDriveFindDialog*>(GetWindowLongPtr(hDlg, DWLP_USER));
    }

    if (pThis)
    {
        return pThis->HandleMessage(hDlg, uMsg, wParam, lParam);
    }
    return FALSE;
}

INT_PTR CGDriveFindDialog::HandleMessage(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    INT_PTR colorRes = 0;
    if (GDriveDarkMode::HandleDialogColors(uMsg, wParam, lParam, &colorRes))
    {
        return colorRes;
    }

    switch (uMsg)
    {
    case WM_INITDIALOG:
        OnInitDialog(hDlg);
        return TRUE;

    case WM_COMMAND:
        OnCommand(hDlg, LOWORD(wParam), (HWND)lParam, HIWORD(wParam));
        return TRUE;

    case WM_SIZE:
        OnSize(hDlg, (UINT)wParam, LOWORD(lParam), HIWORD(lParam));
        return TRUE;

    case WM_GETMINMAXINFO:
        OnGetMinMaxInfo(hDlg, (LPMINMAXINFO)lParam);
        return TRUE;

    case WM_NOTIFY:
    {
        LPNMHDR pnm = (LPNMHDR)lParam;
        HWND hwndHeader = ListView_GetHeader(m_hResultsList);
        if (hwndHeader && pnm->hwndFrom == hwndHeader && pnm->code == NM_CUSTOMDRAW)
        {
            LPNMCUSTOMDRAW pnmcd = (LPNMCUSTOMDRAW)lParam;
            if (pnmcd->dwDrawStage == CDDS_PREPAINT)
            {
                SetWindowLongPtr(hDlg, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW);
                return TRUE;
            }
            else if (pnmcd->dwDrawStage == CDDS_ITEMPREPAINT)
            {
                if (GDriveDarkMode::IsDarkMode())
                {
                    HDC hdc = pnmcd->hdc;
                    RECT rc = pnmcd->rc;

                    // Fill dark background
                    HBRUSH hBg = CreateSolidBrush(RGB(40, 40, 40));
                    FillRect(hdc, &rc, hBg);
                    DeleteObject(hBg);

                    // Right divider and bottom line
                    HPEN hBorder = CreatePen(PS_SOLID, 1, RGB(70, 70, 70));
                    HPEN hOldPen = (HPEN)SelectObject(hdc, hBorder);
                    MoveToEx(hdc, rc.right - 1, rc.top + 3, NULL);
                    LineTo(hdc, rc.right - 1, rc.bottom - 3);
                    MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
                    LineTo(hdc, rc.right, rc.bottom - 1);
                    SelectObject(hdc, hOldPen);
                    DeleteObject(hBorder);

                    // Text
                    char text[128] = {0};
                    HDITEMA hdi;
                    memset(&hdi, 0, sizeof(hdi));
                    hdi.mask = HDI_TEXT | HDI_FORMAT;
                    hdi.pszText = text;
                    hdi.cchTextMax = sizeof(text);
                    Header_GetItem(hwndHeader, (int)pnmcd->dwItemSpec, &hdi);

                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, RGB(225, 225, 225));
                    HFONT hFont = (HFONT)SendMessage(hwndHeader, WM_GETFONT, 0, 0);
                    HFONT hOldFont = NULL;
                    if (hFont) hOldFont = (HFONT)SelectObject(hdc, hFont);

                    RECT textRc = rc;
                    textRc.left += 6;
                    textRc.right -= 6;
                    UINT fmt = DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
                    if (hdi.fmt & HDF_RIGHT) fmt |= DT_RIGHT;
                    else fmt |= DT_LEFT;

                    DrawTextA(hdc, text, -1, &textRc, fmt);

                    if (hOldFont) SelectObject(hdc, hOldFont);

                    SetWindowLongPtr(hDlg, DWLP_MSGRESULT, CDRF_SKIPDEFAULT);
                    return TRUE;
                }
            }
        }
        else if (pnm->idFrom == IDC_FIND_RESULTS)
        {
            if (pnm->code == NM_DBLCLK)
            {
                FocusSelectedItem();
                return TRUE;
            }
            else if (pnm->code == LVN_COLUMNCLICK)
            {
                LPNMLISTVIEW pnmlv = (LPNMLISTVIEW)lParam;
                SortResults(pnmlv->iSubItem);
                return TRUE;
            }
            else if (pnm->code == LVN_KEYDOWN)
            {
                LPNMLVKEYDOWN pnkd = (LPNMLVKEYDOWN)lParam;
                if (pnkd->wVKey == VK_RETURN || pnkd->wVKey == VK_SPACE)
                {
                    FocusSelectedItem();
                    return TRUE;
                }
                else if (pnkd->wVKey == VK_F3)
                {
                    ViewSelectedItem();
                    return TRUE;
                }
            }
        }
        break;
    }

    case WM_USER_SEARCH_FINISHED:
    {
        m_isSearching = false;
        ShowWindow(m_hBtnStop, SW_HIDE);
        ShowWindow(m_hBtnFindNow, SW_SHOW);
        EnableWindow(m_hBtnFindNow, TRUE);

        double elapsedSec = (GetTickCount64() - m_searchStartTick) / 1000.0;
        char statusBuf[256];
        if (m_cancelRequested)
        {
            snprintf(statusBuf, sizeof(statusBuf), LoadStr(IDS_FIND_STOPPED_FMT), (int)m_results.size());
        }
        else
        {
            snprintf(statusBuf, sizeof(statusBuf), LoadStr(IDS_FIND_FINISHED_FMT), (int)m_results.size(), elapsedSec);
        }
        SetWindowTextA(m_hStatus, statusBuf);

        char countBuf[64];
        snprintf(countBuf, sizeof(countBuf), LoadStr(IDS_FIND_FOUND_FMT), (int)m_results.size());
        SetWindowTextA(m_hFoundCount, countBuf);

        PopulateResults(m_results);
        UpdateControlsState();
        return TRUE;
    }

    case WM_DESTROY:
        OnDestroy(hDlg);
        return TRUE;
    }

    return FALSE;
}

static const wchar_t* kRegFindKey = L"Software\\Altap\\Salamander\\Plugins\\gdrive\\Find";

static void LoadHistoryList(const wchar_t* subKeyName, std::vector<std::string>& listOut)
{
    listOut.clear();
    std::wstring fullKey = std::wstring(kRegFindKey) + L"\\" + subKeyName;
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, fullKey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        for (int i = 0; i < 20; ++i)
        {
            wchar_t valName[32];
            swprintf(valName, 32, L"Item_%d", i);
            char valData[512] = {0};
            DWORD dwSize = sizeof(valData);
            DWORD dwType = REG_SZ;
            if (RegQueryValueExA(hKey, GDriveHttp::HttpClient::WideToUtf8(valName).c_str(), NULL, &dwType, (LPBYTE)valData, &dwSize) == ERROR_SUCCESS)
            {
                if (valData[0] != '\0')
                {
                    listOut.push_back(valData);
                }
            }
        }
        RegCloseKey(hKey);
    }
}

static void SaveHistoryList(const wchar_t* subKeyName, const std::vector<std::string>& list)
{
    std::wstring fullKey = std::wstring(kRegFindKey) + L"\\" + subKeyName;
    HKEY hKey = NULL;
    DWORD disp = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, fullKey.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, &disp) == ERROR_SUCCESS)
    {
        for (int i = 0; i < 25; ++i)
        {
            wchar_t valName[32];
            swprintf(valName, 32, L"Item_%d", i);
            RegDeleteValueA(hKey, GDriveHttp::HttpClient::WideToUtf8(valName).c_str());
        }

        int count = std::min((int)list.size(), 20);
        for (int i = 0; i < count; ++i)
        {
            wchar_t valName[32];
            swprintf(valName, 32, L"Item_%d", i);
            RegSetValueExA(hKey, GDriveHttp::HttpClient::WideToUtf8(valName).c_str(), 0, REG_SZ,
                           (const BYTE*)list[i].c_str(), (DWORD)list[i].length() + 1);
        }
        RegCloseKey(hKey);
    }
}

static void PushToHistory(std::vector<std::string>& list, const std::string& item)
{
    if (item.empty()) return;
    for (auto it = list.begin(); it != list.end(); )
    {
        if (_stricmp(it->c_str(), item.c_str()) == 0)
        {
            it = list.erase(it);
        }
        else
        {
            ++it;
        }
    }
    list.insert(list.begin(), item);
    if (list.size() > 20)
    {
        list.resize(20);
    }
}

void CGDriveFindDialog::UpdateAdvancedText()
{
    std::string email = GDriveAuth::AuthManager::GetInstance().GetTokens().accountEmail;
    std::vector<std::string> parts;

    if (m_typeFilter == 1) parts.push_back("Docs");
    else if (m_typeFilter == 2) parts.push_back("Sheets");
    else if (m_typeFilter == 3) parts.push_back("Slides");
    else if (m_typeFilter == 4) parts.push_back("PDF");
    else if (m_typeFilter == 5) parts.push_back("Images");
    else if (m_typeFilter == 6) parts.push_back("Folders");

    if (m_starredOnly) parts.push_back("Starred");
    if (m_trashedOnly) parts.push_back("Trash");

    std::string desc;
    if (parts.empty())
    {
        desc = "All item types";
    }
    else
    {
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (i > 0) desc += ", ";
            desc += parts[i];
        }
    }

    if (!email.empty())
    {
        desc += " (Account: " + email + ")";
    }

    SetWindowTextA(m_hAdvancedText, desc.c_str());
}

static RECT GetChildRect(HWND hDlg, HWND hCtrl)
{
    RECT rc = {0, 0, 0, 0};
    if (hCtrl && IsWindow(hCtrl))
    {
        GetWindowRect(hCtrl, &rc);
        MapWindowPoints(NULL, hDlg, (LPPOINT)&rc, 2);
    }
    return rc;
}

void CGDriveFindDialog::OnInitDialog(HWND hDlg)
{
    m_hNamed = GetDlgItem(hDlg, IDC_FIND_NAMED);
    m_hLookIn = GetDlgItem(hDlg, IDC_FIND_LOOKIN);
    m_hSubdir = GetDlgItem(hDlg, IDC_FIND_INCLUDE_SUBDIR);
    m_hGrep = GetDlgItem(hDlg, IDC_FIND_GREP);
    m_hContaining = GetDlgItem(hDlg, IDC_FIND_CONTAINING);
    m_hDocsOcr = GetDlgItem(hDlg, IDC_FIND_DOCS_OCR);
    m_hCase = GetDlgItem(hDlg, IDC_FIND_CASE);
    m_hAdvanced = GetDlgItem(hDlg, IDC_FIND_ADVANCED);
    m_hAdvancedText = GetDlgItem(hDlg, IDC_FIND_ADVANCED_TEXT);
    m_hFoundCount = GetDlgItem(hDlg, IDC_FIND_FOUND_COUNT);
    m_hResultsList = GetDlgItem(hDlg, IDC_FIND_RESULTS);
    m_hStatus = GetDlgItem(hDlg, IDC_FIND_STATUS);
    m_hBtnFindNow = GetDlgItem(hDlg, IDC_FIND_BTN_FINDNOW);
    m_hBtnStop = GetDlgItem(hDlg, IDC_FIND_BTN_STOP);
    m_hBtnFocus = GetDlgItem(hDlg, IDC_FIND_BTN_FOCUS);
    m_hBtnView = GetDlgItem(hDlg, IDC_FIND_BTN_VIEW);
    m_hBtnOpenWeb = GetDlgItem(hDlg, IDC_FIND_BTN_OPENWEB);
    m_hBtnCopyLink = GetDlgItem(hDlg, IDC_FIND_BTN_COPYLINK);

    SetWindowTextA(hDlg, LoadStr(IDS_FIND_TITLE));
    GDriveDarkMode::ApplyWindowTheme(hDlg);
    GDriveDarkMode::ApplyDialogControlsTheme(hDlg);
    GDriveDarkMode::ApplyListViewTheme(m_hResultsList);

    // Capture initial layout rects
    GetClientRect(hDlg, &m_origClientRect);
    m_minSize.x = (m_origClientRect.right > 0) ? m_origClientRect.right : 500;
    m_minSize.y = (m_origClientRect.bottom > 0) ? m_origClientRect.bottom : 360;

    m_rcNamed = GetChildRect(hDlg, m_hNamed);
    m_rcLookIn = GetChildRect(hDlg, m_hLookIn);
    m_rcContaining = GetChildRect(hDlg, m_hContaining);
    m_rcFindNow = GetChildRect(hDlg, m_hBtnFindNow);
    m_rcStop = GetChildRect(hDlg, m_hBtnStop);
    m_rcBrowse = GetChildRect(hDlg, GetDlgItem(hDlg, IDC_FIND_LOOKIN_BROWSE));
    m_rcLine1 = GetChildRect(hDlg, GetDlgItem(hDlg, IDC_FIND_LINE1));
    m_rcLine2 = GetChildRect(hDlg, GetDlgItem(hDlg, IDC_FIND_LINE2));
    m_rcFoundCount = GetChildRect(hDlg, m_hFoundCount);
    m_rcResultsList = GetChildRect(hDlg, m_hResultsList);
    m_rcStatus = GetChildRect(hDlg, m_hStatus);
    m_initialLayoutDone = true;

    // Load histories
    LoadHistoryList(L"Named", m_historyNamed);
    LoadHistoryList(L"LookIn", m_historyLookIn);
    LoadHistoryList(L"Containing", m_historyContaining);

    // Named combo setup
    SendMessage(m_hNamed, CB_RESETCONTENT, 0, 0);
    if (m_historyNamed.empty())
    {
        m_historyNamed.push_back("*.*");
    }
    for (const auto& s : m_historyNamed)
    {
        SendMessage(m_hNamed, CB_ADDSTRING, 0, (LPARAM)s.c_str());
    }
    SetWindowTextA(m_hNamed, m_historyNamed[0].c_str());
    SendMessage(m_hNamed, CB_SETEDITSEL, 0, MAKELPARAM(0, -1));

    // Look in combo setup
    SendMessage(m_hLookIn, CB_RESETCONTENT, 0, 0);
    std::string entireDrive = LoadStr(IDS_FIND_LOOKIN_ALL_DRIVE);
    SendMessage(m_hLookIn, CB_ADDSTRING, 0, (LPARAM)entireDrive.c_str());
    SendMessage(m_hLookIn, CB_ADDSTRING, 0, (LPARAM)"gdrive:\\My Drive");
    for (const auto& s : m_historyLookIn)
    {
        if (s != entireDrive && s != "gdrive:\\My Drive")
        {
            SendMessage(m_hLookIn, CB_ADDSTRING, 0, (LPARAM)s.c_str());
        }
    }

    if (!m_initialPath.empty() && m_initialPath != "/" && m_initialPath != "/My Drive")
    {
        std::string winPath = "gdrive:" + m_initialPath;
        std::replace(winPath.begin(), winPath.end(), '/', '\\');
        int idx = (int)SendMessage(m_hLookIn, CB_ADDSTRING, 0, (LPARAM)winPath.c_str());
        SendMessage(m_hLookIn, CB_SETCURSEL, idx, 0);
    }
    else
    {
        SendMessage(m_hLookIn, CB_SETCURSEL, 0, 0);
    }

    // Containing combo setup
    SendMessage(m_hContaining, CB_RESETCONTENT, 0, 0);
    for (const auto& s : m_historyContaining)
    {
        SendMessage(m_hContaining, CB_ADDSTRING, 0, (LPARAM)s.c_str());
    }
    if (!m_historyContaining.empty())
    {
        SetWindowTextA(m_hContaining, m_historyContaining[0].c_str());
    }

    SendMessage(m_hSubdir, BM_SETCHECK, BST_CHECKED, 0);
    SendMessage(m_hDocsOcr, BM_SETCHECK, BST_CHECKED, 0);

    EnableWindow(m_hContaining, FALSE);
    EnableWindow(m_hDocsOcr, FALSE);
    EnableWindow(m_hCase, FALSE);

    UpdateAdvancedText();
    SetWindowTextA(m_hStatus, LoadStr(IDS_FIND_READY));

    // Results ListView setup
    ListView_SetExtendedListViewStyle(m_hResultsList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    LVCOLUMN col;
    memset(&col, 0, sizeof(col));
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT | LVCF_SUBITEM;

    col.fmt = LVCFMT_LEFT;
    col.cx = 200;
    col.pszText = const_cast<char*>(LoadStr(IDS_FIND_COL_NAME));
    col.iSubItem = 0;
    ListView_InsertColumn(m_hResultsList, 0, &col);

    col.fmt = LVCFMT_LEFT;
    col.cx = 160;
    col.pszText = const_cast<char*>(LoadStr(IDS_FIND_COL_PATH));
    col.iSubItem = 1;
    ListView_InsertColumn(m_hResultsList, 1, &col);

    col.fmt = LVCFMT_RIGHT;
    col.cx = 80;
    col.pszText = const_cast<char*>(LoadStr(IDS_FIND_COL_SIZE));
    col.iSubItem = 2;
    ListView_InsertColumn(m_hResultsList, 2, &col);

    col.fmt = LVCFMT_LEFT;
    col.cx = 85;
    col.pszText = const_cast<char*>(LoadStr(IDS_FIND_COL_DATE));
    col.iSubItem = 3;
    ListView_InsertColumn(m_hResultsList, 3, &col);

    col.fmt = LVCFMT_LEFT;
    col.cx = 70;
    col.pszText = const_cast<char*>(LoadStr(IDS_FIND_COL_TIME));
    col.iSubItem = 4;
    ListView_InsertColumn(m_hResultsList, 4, &col);

    col.fmt = LVCFMT_LEFT;
    col.cx = 120;
    col.pszText = const_cast<char*>(LoadStr(IDS_FIND_COL_OWNER));
    col.iSubItem = 5;
    ListView_InsertColumn(m_hResultsList, 5, &col);

    UpdateControlsState();
}

void CGDriveFindDialog::OnCommand(HWND hDlg, int id, HWND hCtrl, UINT codeNotify)
{
    switch (id)
    {
    case IDC_FIND_BTN_FINDNOW:
    case IDOK:
        if (m_isSearching)
        {
            StopSearch();
        }
        else
        {
            if (GetFocus() == m_hResultsList)
            {
                FocusSelectedItem();
            }
            else
            {
                StartSearch();
            }
        }
        break;

    case IDC_FIND_BTN_STOP:
        StopSearch();
        break;

    case IDC_FIND_LOOKIN_BROWSE:
    {
        RECT r;
        GetWindowRect(GetDlgItem(hDlg, IDC_FIND_LOOKIN_BROWSE), &r);

        HMENU hMenu = CreatePopupMenu();
        std::string allDrive = LoadStr(IDS_FIND_LOOKIN_ALL_DRIVE);
        AppendMenuA(hMenu, MF_STRING, 1001, allDrive.c_str());
        AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuA(hMenu, MF_STRING, 1002, "gdrive:\\My Drive");
        AppendMenuA(hMenu, MF_STRING, 1003, "gdrive:\\Shared with me");
        AppendMenuA(hMenu, MF_STRING, 1004, "gdrive:\\Shared Drives");
        AppendMenuA(hMenu, MF_STRING, 1005, "gdrive:\\Starred");
        AppendMenuA(hMenu, MF_STRING, 1006, "gdrive:\\Trash");

        if (!m_historyLookIn.empty())
        {
            AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
            for (size_t i = 0; i < std::min<size_t>(m_historyLookIn.size(), 8); ++i)
            {
                AppendMenuA(hMenu, MF_STRING, 1010 + (UINT)i, m_historyLookIn[i].c_str());
            }
        }

        int cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                 r.right, r.top, 0, hDlg, NULL);
        DestroyMenu(hMenu);

        std::string chosen;
        if (cmd == 1001) chosen = allDrive;
        else if (cmd == 1002) chosen = "gdrive:\\My Drive";
        else if (cmd == 1003) chosen = "gdrive:\\Shared with me";
        else if (cmd == 1004) chosen = "gdrive:\\Shared Drives";
        else if (cmd == 1005) chosen = "gdrive:\\Starred";
        else if (cmd == 1006) chosen = "gdrive:\\Trash";
        else if (cmd >= 1010 && cmd < 1010 + (int)m_historyLookIn.size())
        {
            chosen = m_historyLookIn[cmd - 1010];
        }

        if (!chosen.empty())
        {
            SetWindowTextA(m_hLookIn, chosen.c_str());
            SendMessage(m_hLookIn, CB_SETEDITSEL, 0, MAKELPARAM(0, -1));
        }
        break;
    }

    case IDC_FIND_ADVANCED:
    {
        RECT r;
        GetWindowRect(GetDlgItem(hDlg, IDC_FIND_ADVANCED), &r);

        HMENU hMenu = CreatePopupMenu();
        HMENU hTypeSub = CreatePopupMenu();
        AppendMenuA(hTypeSub, (m_typeFilter == 0 ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, 2000, "All Item Types");
        AppendMenuA(hTypeSub, (m_typeFilter == 6 ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, 2006, "Only Folders");
        AppendMenuA(hTypeSub, MF_SEPARATOR, 0, NULL);
        AppendMenuA(hTypeSub, (m_typeFilter == 1 ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, 2001, "Google Docs");
        AppendMenuA(hTypeSub, (m_typeFilter == 2 ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, 2002, "Google Sheets");
        AppendMenuA(hTypeSub, (m_typeFilter == 3 ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, 2003, "Google Slides");
        AppendMenuA(hTypeSub, (m_typeFilter == 4 ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, 2004, "PDF Documents");
        AppendMenuA(hTypeSub, (m_typeFilter == 5 ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, 2005, "Images");

        AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hTypeSub, "Filter by Item Type");
        AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuA(hMenu, (m_starredOnly ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, 2010, "Starred items only");
        AppendMenuA(hMenu, (m_trashedOnly ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, 2011, "Trash items only");
        AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuA(hMenu, MF_STRING, 2020, "Reset all filters");

        int cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                 r.left, r.bottom, 0, hDlg, NULL);
        DestroyMenu(hMenu);

        if (cmd >= 2000 && cmd <= 2006)
        {
            m_typeFilter = cmd - 2000;
            UpdateAdvancedText();
        }
        else if (cmd == 2010)
        {
            m_starredOnly = !m_starredOnly;
            if (m_starredOnly) m_trashedOnly = false;
            UpdateAdvancedText();
        }
        else if (cmd == 2011)
        {
            m_trashedOnly = !m_trashedOnly;
            if (m_trashedOnly) m_starredOnly = false;
            UpdateAdvancedText();
        }
        else if (cmd == 2020)
        {
            m_typeFilter = 0;
            m_starredOnly = false;
            m_trashedOnly = false;
            UpdateAdvancedText();
        }
        break;
    }

    case IDC_FIND_GREP:
    {
        BOOL grepChecked = (SendMessage(m_hGrep, BM_GETCHECK, 0, 0) == BST_CHECKED);
        EnableWindow(m_hContaining, grepChecked);
        EnableWindow(m_hDocsOcr, grepChecked);
        EnableWindow(m_hCase, grepChecked);
        if (grepChecked)
        {
            SetFocus(m_hContaining);
        }
        break;
    }

    case IDC_FIND_BTN_FOCUS:
        FocusSelectedItem();
        break;

    case IDC_FIND_BTN_VIEW:
        ViewSelectedItem();
        break;

    case IDC_FIND_BTN_OPENWEB:
        OpenWebSelectedItem();
        break;

    case IDC_FIND_BTN_COPYLINK:
        CopyLinkSelectedItem();
        break;

    case IDCANCEL:
        if (m_isSearching)
        {
            StopSearch();
        }
        else
        {
            EndDialog(hDlg, IDCANCEL);
        }
        break;
    }
}

void CGDriveFindDialog::OnSize(HWND hDlg, UINT state, int cx, int cy)
{
    if (!m_initialLayoutDone || cx <= 0 || cy <= 0) return;

    int deltaX = cx - m_origClientRect.right;
    int deltaY = cy - m_origClientRect.bottom;

    HDWP hdwp = BeginDeferWindowPos(12);
    if (!hdwp) return;

    if (m_hNamed)
        hdwp = DeferWindowPos(hdwp, m_hNamed, NULL, 0, 0, (m_rcNamed.right - m_rcNamed.left) + deltaX, m_rcNamed.bottom - m_rcNamed.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (m_hLookIn)
        hdwp = DeferWindowPos(hdwp, m_hLookIn, NULL, 0, 0, (m_rcLookIn.right - m_rcLookIn.left) + deltaX, m_rcLookIn.bottom - m_rcLookIn.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (m_hContaining)
        hdwp = DeferWindowPos(hdwp, m_hContaining, NULL, 0, 0, (m_rcContaining.right - m_rcContaining.left) + deltaX, m_rcContaining.bottom - m_rcContaining.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    HWND hLine1 = GetDlgItem(hDlg, IDC_FIND_LINE1);
    if (hLine1)
        hdwp = DeferWindowPos(hdwp, hLine1, NULL, 0, 0, (m_rcLine1.right - m_rcLine1.left) + deltaX, m_rcLine1.bottom - m_rcLine1.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    HWND hLine2 = GetDlgItem(hDlg, IDC_FIND_LINE2);
    if (hLine2)
        hdwp = DeferWindowPos(hdwp, hLine2, NULL, 0, 0, (m_rcLine2.right - m_rcLine2.left) + deltaX, m_rcLine2.bottom - m_rcLine2.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (m_hBtnFindNow)
        hdwp = DeferWindowPos(hdwp, m_hBtnFindNow, NULL, m_rcFindNow.left + deltaX, m_rcFindNow.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (m_hBtnStop)
        hdwp = DeferWindowPos(hdwp, m_hBtnStop, NULL, m_rcStop.left + deltaX, m_rcStop.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    HWND hBrowse = GetDlgItem(hDlg, IDC_FIND_LOOKIN_BROWSE);
    if (hBrowse)
        hdwp = DeferWindowPos(hdwp, hBrowse, NULL, m_rcBrowse.left + deltaX, m_rcBrowse.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (m_hFoundCount)
        hdwp = DeferWindowPos(hdwp, m_hFoundCount, NULL, m_rcFoundCount.left + deltaX, m_rcFoundCount.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (m_hResultsList)
    {
        int w = (m_rcResultsList.right - m_rcResultsList.left) + deltaX;
        int h = (m_rcResultsList.bottom - m_rcResultsList.top) + deltaY;
        if (w < 100) w = 100;
        if (h < 50) h = 50;
        hdwp = DeferWindowPos(hdwp, m_hResultsList, NULL, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (m_hStatus)
    {
        int w = (m_rcStatus.right - m_rcStatus.left) + deltaX;
        if (w < 100) w = 100;
        hdwp = DeferWindowPos(hdwp, m_hStatus, NULL, m_rcStatus.left, m_rcStatus.top + deltaY, w, m_rcStatus.bottom - m_rcStatus.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    EndDeferWindowPos(hdwp);
}

void CGDriveFindDialog::OnGetMinMaxInfo(HWND hDlg, LPMINMAXINFO lpMMI)
{
    lpMMI->ptMinTrackSize.x = m_minSize.x;
    lpMMI->ptMinTrackSize.y = m_minSize.y;
}

void CGDriveFindDialog::OnDestroy(HWND hDlg)
{
    StopSearch();
}

void CGDriveFindDialog::StartSearch()
{
    if (m_isSearching) return;

    // 1. Read all UI controls safely on the UI thread
    m_currentSearchOpts = GDriveApi::SearchOptions();

    char namedBuf[256] = {0};
    GetWindowTextA(m_hNamed, namedBuf, sizeof(namedBuf));
    m_currentSearchOpts.queryNamed = namedBuf;

    m_currentSearchOpts.searchSubdirs = (SendMessage(m_hSubdir, BM_GETCHECK, 0, 0) == BST_CHECKED);
    m_currentSearchOpts.searchContent = (SendMessage(m_hGrep, BM_GETCHECK, 0, 0) == BST_CHECKED);

    char containingBuf[256] = {0};
    GetWindowTextA(m_hContaining, containingBuf, sizeof(containingBuf));
    m_currentSearchOpts.queryContent = containingBuf;

    m_currentSearchOpts.caseSensitive = (SendMessage(m_hCase, BM_GETCHECK, 0, 0) == BST_CHECKED);
    m_currentSearchOpts.typeFilter = m_typeFilter;
    m_currentSearchOpts.starredOnly = m_starredOnly;
    m_currentSearchOpts.trashedOnly = m_trashedOnly;

    // 2. Resolve LookIn folder scope on UI thread
    char lookInBuf[512] = {0};
    GetWindowTextA(m_hLookIn, lookInBuf, sizeof(lookInBuf));
    std::string lookInStr = lookInBuf;

    std::string allDrive = LoadStr(IDS_FIND_LOOKIN_ALL_DRIVE);
    if (lookInStr == allDrive || lookInStr == "gdrive:\\" || lookInStr == "/")
    {
        // Entire Drive
        m_currentSearchOpts.folderScopeId.clear();
        m_currentSearchOpts.targetFolderPath.clear();
    }
    else if (_stricmp(lookInStr.c_str(), "gdrive:\\Shared with me") == 0 || _stricmp(lookInStr.c_str(), "\\Shared with me") == 0)
    {
        m_currentSearchOpts.sharedWithMeOnly = true;
        m_currentSearchOpts.targetFolderPath = "\\Shared with me";
    }
    else if (_stricmp(lookInStr.c_str(), "gdrive:\\Starred") == 0 || _stricmp(lookInStr.c_str(), "\\Starred") == 0)
    {
        m_currentSearchOpts.starredOnly = true;
        m_currentSearchOpts.targetFolderPath = "\\Starred";
    }
    else if (_stricmp(lookInStr.c_str(), "gdrive:\\Trash") == 0 || _stricmp(lookInStr.c_str(), "\\Trash") == 0)
    {
        m_currentSearchOpts.trashedOnly = true;
        m_currentSearchOpts.targetFolderPath = "\\Trash";
    }
    else
    {
        std::string path = lookInStr;
        if (path.rfind("gdrive:", 0) == 0)
        {
            path = path.substr(7);
        }
        std::replace(path.begin(), path.end(), '\\', '/');
        if (path.empty() || path[0] != '/') path = "/" + path;

        std::string winPath = path;
        std::replace(winPath.begin(), winPath.end(), '/', '\\');
        m_currentSearchOpts.targetFolderPath = winPath;

        std::string folderId, driveId;
        bool isShared = false;
        if (m_pFS && m_pFS->ResolveFolderIdForPath(path, folderId, driveId, isShared))
        {
            m_currentSearchOpts.folderScopeId = folderId;
            m_currentSearchOpts.driveId = driveId;
            m_currentSearchOpts.isSharedDrive = isShared;
        }
    }

    // Save histories
    if (!m_currentSearchOpts.queryNamed.empty())
    {
        PushToHistory(m_historyNamed, m_currentSearchOpts.queryNamed);
        SaveHistoryList(L"Named", m_historyNamed);
    }
    if (!lookInStr.empty())
    {
        PushToHistory(m_historyLookIn, lookInStr);
        SaveHistoryList(L"LookIn", m_historyLookIn);
    }
    if (m_currentSearchOpts.searchContent && !m_currentSearchOpts.queryContent.empty())
    {
        PushToHistory(m_historyContaining, m_currentSearchOpts.queryContent);
        SaveHistoryList(L"Containing", m_historyContaining);
    }

    m_isSearching = true;
    m_cancelRequested = false;
    m_searchStartTick = GetTickCount64();

    ShowWindow(m_hBtnFindNow, SW_HIDE);
    ShowWindow(m_hBtnStop, SW_SHOW);
    EnableWindow(m_hBtnStop, TRUE);

    SetWindowTextA(m_hStatus, LoadStr(IDS_FIND_SEARCHING));
    SetWindowTextA(m_hFoundCount, "Found items: 0");
    ListView_DeleteAllItems(m_hResultsList);

    {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        m_results.clear();
    }

    if (m_searchThread.joinable())
    {
        m_searchThread.join();
    }

    m_searchThread = std::thread(&CGDriveFindDialog::SearchWorker, this);
}

void CGDriveFindDialog::StopSearch()
{
    m_cancelRequested = true;
}

static std::string ResolveParentPath(const std::string& folderId,
                                     std::map<std::string, std::string>& pathCache,
                                     const std::atomic<bool>& cancelFlag)
{
    if (folderId.empty() || folderId == "root")
    {
        return "\\My Drive";
    }
    if (folderId == "shared_with_me_root")
    {
        return "\\Shared with me";
    }
    if (folderId == "shared_drives_root")
    {
        return "\\Shared Drives";
    }
    if (folderId == "trash_root")
    {
        return "\\Trash";
    }
    if (folderId == "starred_root")
    {
        return "\\Starred";
    }
    if (folderId == "recent_root")
    {
        return "\\Recent";
    }

    auto it = pathCache.find(folderId);
    if (it != pathCache.end())
    {
        return it->second;
    }

    if (cancelFlag)
    {
        return "\\My Drive";
    }

    GDriveApi::GDriveItem folderItem;
    if (GDriveApi::ApiClient::GetInstance().GetFileMetadata(folderId, folderItem))
    {
        std::string parentPath;
        if (folderItem.parentId.empty() || folderItem.parentId == "root")
        {
            parentPath = "\\My Drive";
        }
        else if (folderItem.isSharedDrive && !folderItem.driveId.empty() && folderItem.parentId == folderItem.driveId)
        {
            parentPath = "\\Shared Drives\\" + folderItem.name;
            pathCache[folderId] = parentPath;
            return parentPath;
        }
        else
        {
            parentPath = ResolveParentPath(folderItem.parentId, pathCache, cancelFlag);
        }

        std::string fullPath = parentPath;
        if (fullPath.empty() || fullPath.back() != '\\')
        {
            fullPath += "\\";
        }
        fullPath += folderItem.name;

        pathCache[folderId] = fullPath;
        return fullPath;
    }

    pathCache[folderId] = "\\My Drive";
    return "\\My Drive";
}

void CGDriveFindDialog::SearchWorker()
{
    std::vector<GDriveApi::GDriveItem> rawResults;
    std::string err;

    // Search in thread with atomic cancel flag
    bool ok = GDriveApi::ApiClient::GetInstance().SearchFiles(m_currentSearchOpts, rawResults, &m_cancelRequested, &err);

    if (m_cancelRequested)
    {
        return;
    }

    // Resolve parent folder paths for all items
    std::map<std::string, std::string> folderPathMap;
    folderPathMap[""] = "\\My Drive";
    folderPathMap["root"] = "\\My Drive";

    if (!m_currentSearchOpts.targetFolderPath.empty() && !m_currentSearchOpts.folderScopeId.empty())
    {
        folderPathMap[m_currentSearchOpts.folderScopeId] = m_currentSearchOpts.targetFolderPath;
    }

    std::vector<GDriveApi::GDriveItem> filteredResults;
    filteredResults.reserve(rawResults.size());

    for (auto& item : rawResults)
    {
        if (m_cancelRequested) break;

        if (item.parentId.empty() || item.parentId == "root")
        {
            item.parentPath = "\\My Drive";
        }
        else
        {
            item.parentPath = ResolveParentPath(item.parentId, folderPathMap, m_cancelRequested);
        }

        // Subtree path filtering: if a target folder is specified (e.g. \My Drive\Knihy) and searchSubdirs is true
        if (!m_currentSearchOpts.targetFolderPath.empty() &&
            m_currentSearchOpts.targetFolderPath != "\\" &&
            m_currentSearchOpts.targetFolderPath != "\\My Drive" &&
            m_currentSearchOpts.targetFolderPath != "\\Shared with me" &&
            m_currentSearchOpts.targetFolderPath != "\\Starred" &&
            m_currentSearchOpts.targetFolderPath != "\\Trash" &&
            m_currentSearchOpts.searchSubdirs)
        {
            const std::string& tgt = m_currentSearchOpts.targetFolderPath;
            if (_stricmp(item.parentPath.c_str(), tgt.c_str()) == 0 ||
                (item.parentPath.length() > tgt.length() &&
                 item.parentPath[tgt.length()] == '\\' &&
                 _strnicmp(item.parentPath.c_str(), tgt.c_str(), tgt.length()) == 0))
            {
                filteredResults.push_back(item);
            }
        }
        else
        {
            filteredResults.push_back(item);
        }
    }

    if (!m_cancelRequested)
    {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        m_results = filteredResults;
    }

    if (!m_cancelRequested && m_hDlg && IsWindow(m_hDlg))
    {
        PostMessage(m_hDlg, WM_USER_SEARCH_FINISHED, 0, 0);
    }
}

static std::string FormatSize(int64_t bytes, bool isFolder)
{
    if (isFolder) return "<DIR>";
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
        return buf;
    }
    if (bytes < 1024LL * 1024 * 1024)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
        return buf;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    return buf;
}

static void FormatDateTime(const FILETIME& ft, std::string& dateStr, std::string& timeStr)
{
    FILETIME lft;
    FileTimeToLocalFileTime(&ft, &lft);
    SYSTEMTIME st;
    FileTimeToSystemTime(&lft, &st);

    char dbuf[64] = {0};
    GetDateFormatA(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, dbuf, sizeof(dbuf));
    dateStr = dbuf;

    char tbuf[64] = {0};
    GetTimeFormatA(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, NULL, tbuf, sizeof(tbuf));
    timeStr = tbuf;
}

void CGDriveFindDialog::PopulateResults(const std::vector<GDriveApi::GDriveItem>& items)
{
    ListView_DeleteAllItems(m_hResultsList);

    for (int i = 0; i < (int)items.size(); ++i)
    {
        const auto& item = items[i];
        LVITEMA lvi;
        memset(&lvi, 0, sizeof(lvi));
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = i;
        lvi.iSubItem = 0;
        std::string ansiName = GDriveHttp::HttpClient::Utf8ToAnsi(item.name);
        lvi.pszText = const_cast<char*>(ansiName.c_str());
        lvi.lParam = (LPARAM)i;
        ListView_InsertItem(m_hResultsList, &lvi);

        // Path / Location
        std::string loc = item.parentPath.empty() ? "\\My Drive" : item.parentPath;
        std::replace(loc.begin(), loc.end(), '/', '\\');
        if (loc.empty() || loc[0] != '\\') loc = "\\" + loc;
        ListView_SetItemText(m_hResultsList, i, 1, const_cast<char*>(loc.c_str()));

        // Size
        std::string sz = FormatSize(item.size, item.isFolder);
        ListView_SetItemText(m_hResultsList, i, 2, const_cast<char*>(sz.c_str()));

        // Date & Time
        std::string dateStr, timeStr;
        FormatDateTime(item.modifiedTime, dateStr, timeStr);
        ListView_SetItemText(m_hResultsList, i, 3, const_cast<char*>(dateStr.c_str()));
        ListView_SetItemText(m_hResultsList, i, 4, const_cast<char*>(timeStr.c_str()));

        // Owner
        std::string owner = item.ownerName.empty() ? item.ownerEmail : item.ownerName;
        std::string ansiOwner = GDriveHttp::HttpClient::Utf8ToAnsi(owner);
        ListView_SetItemText(m_hResultsList, i, 5, const_cast<char*>(ansiOwner.c_str()));
    }

    if (!items.empty())
    {
        ListView_SetItemState(m_hResultsList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}

void CGDriveFindDialog::UpdateControlsState()
{
    int sel = ListView_GetNextItem(m_hResultsList, -1, LVNI_SELECTED);
    BOOL hasSel = (sel >= 0);
    EnableWindow(m_hBtnFocus, hasSel);
    EnableWindow(m_hBtnView, hasSel);
    EnableWindow(m_hBtnOpenWeb, hasSel);
    EnableWindow(m_hBtnCopyLink, hasSel);
}

void CGDriveFindDialog::FocusSelectedItem()
{
    int sel = ListView_GetNextItem(m_hResultsList, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= (int)m_results.size()) return;

    const auto& item = m_results[sel];

    std::string relPath = item.parentPath.empty() ? "\\My Drive" : item.parentPath;
    std::replace(relPath.begin(), relPath.end(), '/', '\\');
    if (relPath.empty() || relPath[0] != '\\')
    {
        relPath = "\\" + relPath;
    }

    InterfaceForMenuExt.PostFocusTarget(m_panel, relPath, item.name);

    EndDialog(m_hDlg, IDOK);
}

void CGDriveFindDialog::ViewSelectedItem()
{
    int sel = ListView_GetNextItem(m_hResultsList, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= (int)m_results.size()) return;

    const auto& item = m_results[sel];
    if (item.isFolder) return;

    if (!item.webViewLink.empty())
    {
        ShellExecuteA(NULL, "open", item.webViewLink.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
}

void CGDriveFindDialog::OpenWebSelectedItem()
{
    int sel = ListView_GetNextItem(m_hResultsList, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= (int)m_results.size()) return;

    const auto& item = m_results[sel];
    if (!item.webViewLink.empty())
    {
        ShellExecuteA(NULL, "open", item.webViewLink.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
}

void CGDriveFindDialog::CopyLinkSelectedItem()
{
    int sel = ListView_GetNextItem(m_hResultsList, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= (int)m_results.size()) return;

    const auto& item = m_results[sel];
    if (!item.webViewLink.empty() && OpenClipboard(m_hDlg))
    {
        EmptyClipboard();
        size_t len = item.webViewLink.length() + 1;
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
        if (hMem)
        {
            memcpy(GlobalLock(hMem), item.webViewLink.c_str(), len);
            GlobalUnlock(hMem);
            SetClipboardData(CF_TEXT, hMem);
        }
        CloseClipboard();
        SalamanderGeneral->SalMessageBox(m_hDlg, LoadStr(IDS_LINK_COPIED), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONINFORMATION);
    }
}

void CGDriveFindDialog::SortResults(int columnIndex)
{
    if (m_sortColumn == columnIndex)
    {
        m_sortAscending = !m_sortAscending;
    }
    else
    {
        m_sortColumn = columnIndex;
        m_sortAscending = true;
    }

    std::lock_guard<std::mutex> lock(m_resultsMutex);
    std::sort(m_results.begin(), m_results.end(), [this, columnIndex](const GDriveApi::GDriveItem& a, const GDriveApi::GDriveItem& b) {
        if (columnIndex == 0) // Name
        {
            int cmp = _stricmp(a.name.c_str(), b.name.c_str());
            return m_sortAscending ? (cmp < 0) : (cmp > 0);
        }
        else if (columnIndex == 2) // Size
        {
            return m_sortAscending ? (a.size < b.size) : (a.size > b.size);
        }
        else if (columnIndex == 3 || columnIndex == 4) // Date/Time
        {
            int cmp = CompareFileTime(&a.modifiedTime, &b.modifiedTime);
            return m_sortAscending ? (cmp < 0) : (cmp > 0);
        }
        else if (columnIndex == 5) // Owner
        {
            int cmp = _stricmp(a.ownerName.c_str(), b.ownerName.c_str());
            return m_sortAscending ? (cmp < 0) : (cmp > 0);
        }
        return false;
    });

    PopulateResults(m_results);
}
