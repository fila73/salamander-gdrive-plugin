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

    // Initial values
    SetWindowTextA(m_hNamed, "*.*");
    SendMessage(m_hNamed, CB_ADDSTRING, 0, (LPARAM)"*.*");
    SendMessage(m_hNamed, CB_SETCURSEL, 0, 0);

    // Look in
    std::string entireDrive = LoadStr(IDS_FIND_LOOKIN_ALL_DRIVE);
    SendMessage(m_hLookIn, CB_ADDSTRING, 0, (LPARAM)entireDrive.c_str());
    SendMessage(m_hLookIn, CB_ADDSTRING, 0, (LPARAM)"gdrive:\\My Drive");
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

    SendMessage(m_hSubdir, BM_SETCHECK, BST_CHECKED, 0);
    SendMessage(m_hDocsOcr, BM_SETCHECK, BST_CHECKED, 0);

    EnableWindow(m_hContaining, FALSE);
    EnableWindow(m_hDocsOcr, FALSE);
    EnableWindow(m_hCase, FALSE);

    std::string email = GDriveAuth::AuthManager::GetInstance().GetTokens().accountEmail;
    char advBuf[256];
    snprintf(advBuf, sizeof(advBuf), LoadStr(IDS_FIND_ADVANCED_ALL), email.empty() ? "Google Drive" : email.c_str());
    SetWindowTextA(m_hAdvancedText, advBuf);

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

void CGDriveFindDialog::SearchWorker()
{
    GDriveApi::SearchOptions opts;

    char namedBuf[256] = {0};
    GetWindowTextA(m_hNamed, namedBuf, sizeof(namedBuf));
    opts.queryNamed = namedBuf;

    opts.searchSubdirs = (SendMessage(m_hSubdir, BM_GETCHECK, 0, 0) == BST_CHECKED);
    opts.searchContent = (SendMessage(m_hGrep, BM_GETCHECK, 0, 0) == BST_CHECKED);

    char containingBuf[256] = {0};
    GetWindowTextA(m_hContaining, containingBuf, sizeof(containingBuf));
    opts.queryContent = containingBuf;

    opts.caseSensitive = (SendMessage(m_hCase, BM_GETCHECK, 0, 0) == BST_CHECKED);
    opts.typeFilter = m_typeFilter;

    // Resolve folder scope
    int lookInSel = (int)SendMessage(m_hLookIn, CB_GETCURSEL, 0, 0);
    if (lookInSel > 0)
    {
        char lookInText[512] = {0};
        GetWindowTextA(m_hLookIn, lookInText, sizeof(lookInText));
        std::string path = lookInText;
        if (path.rfind("gdrive:", 0) == 0)
        {
            path = path.substr(7);
            std::replace(path.begin(), path.end(), '\\', '/');
        }

        std::string folderId, driveId;
        bool isShared = false;
        if (m_pFS && m_pFS->ResolveFolderIdForPath(path, folderId, driveId, isShared))
        {
            opts.folderScopeId = folderId;
            opts.driveId = driveId;
            opts.isSharedDrive = isShared;
        }
    }

    std::vector<GDriveApi::GDriveItem> results;
    std::string err;
    volatile bool cancelBool = false;

    // Search in thread
    bool ok = GDriveApi::ApiClient::GetInstance().SearchFiles(opts, results, &cancelBool, &err);

    {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        m_results = results;
    }

    if (m_hDlg && IsWindow(m_hDlg))
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
        std::string loc = item.parentPath.empty() ? (item.isFolder ? "/[Folder]" : "/[File]") : item.parentPath;
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

    if (SalamanderGeneral)
    {
        int failReason = 0;
        SalamanderGeneral->ChangePanelPathToPluginFS(m_panel, AssignedFSName, relPath.c_str(), NULL, -1, item.name.c_str());
    }

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
