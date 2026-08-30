// SPDX-FileCopyrightText: 2026 fila73 & Ondrej Kotas
// SPDX-FileContributor: Dark Mode host policy and theme integration based on fork by Ondrej Kotas (KRtkovo-eu-AI)
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "gdrivedarkmode.h"

namespace GDriveDarkMode
{

static ThemeColors g_theme;
static bool g_themeInitialized = false;

static void CleanupBrushes()
{
    if (g_theme.brushBgMain) { DeleteObject(g_theme.brushBgMain); g_theme.brushBgMain = NULL; }
    if (g_theme.brushBgAlternate) { DeleteObject(g_theme.brushBgAlternate); g_theme.brushBgAlternate = NULL; }
    if (g_theme.brushBgSelected) { DeleteObject(g_theme.brushBgSelected); g_theme.brushBgSelected = NULL; }
    if (g_theme.brushEditBg) { DeleteObject(g_theme.brushEditBg); g_theme.brushEditBg = NULL; }
    if (g_theme.brushToolbarBg) { DeleteObject(g_theme.brushToolbarBg); g_theme.brushToolbarBg = NULL; }
    if (g_theme.brushStatusBg) { DeleteObject(g_theme.brushStatusBg); g_theme.brushStatusBg = NULL; }
    if (g_theme.penGrid) { DeleteObject(g_theme.penGrid); g_theme.penGrid = NULL; }
}

void InitTheme()
{
    CleanupBrushes();

    bool dark = IsDarkMode();

    if (dark)
    {
        g_theme.bgMain        = RGB(32, 32, 32);
        g_theme.bgAlternate   = RGB(38, 38, 38);
        g_theme.bgSelected    = RGB(60, 80, 110);
        g_theme.textMain      = RGB(225, 225, 225);
        g_theme.textDimmed    = RGB(140, 140, 140);
        g_theme.textSelected  = RGB(255, 255, 255);
        g_theme.gridLine      = RGB(48, 48, 48);
        g_theme.headerBg      = RGB(45, 45, 45);
        g_theme.headerText    = RGB(220, 220, 220);
        g_theme.editBg        = RGB(28, 28, 28);
        g_theme.editText      = RGB(230, 230, 230);
        g_theme.statusBg      = RGB(40, 40, 40);
        g_theme.statusText    = RGB(200, 200, 200);
        g_theme.toolbarBg     = RGB(45, 45, 45);
    }
    else
    {
        g_theme.bgMain        = RGB(255, 255, 255);
        g_theme.bgAlternate   = RGB(248, 249, 250);
        g_theme.bgSelected    = RGB(204, 232, 255);
        g_theme.textMain      = RGB(0, 0, 0);
        g_theme.textDimmed    = RGB(100, 100, 100);
        g_theme.textSelected  = RGB(0, 0, 0);
        g_theme.gridLine      = RGB(230, 230, 230);
        g_theme.headerBg      = RGB(240, 240, 240);
        g_theme.headerText    = RGB(0, 0, 0);
        g_theme.editBg        = RGB(255, 255, 255);
        g_theme.editText      = RGB(0, 0, 0);
        g_theme.statusBg      = RGB(240, 240, 240);
        g_theme.statusText    = RGB(0, 0, 0);
        g_theme.toolbarBg     = RGB(245, 245, 245);
    }

    g_theme.brushBgMain       = CreateSolidBrush(g_theme.bgMain);
    g_theme.brushBgAlternate  = CreateSolidBrush(g_theme.bgAlternate);
    g_theme.brushBgSelected   = CreateSolidBrush(g_theme.bgSelected);
    g_theme.brushEditBg       = CreateSolidBrush(g_theme.editBg);
    g_theme.brushToolbarBg    = CreateSolidBrush(g_theme.toolbarBg);
    g_theme.brushStatusBg     = CreateSolidBrush(g_theme.statusBg);
    g_theme.penGrid           = CreatePen(PS_SOLID, 1, g_theme.gridLine);

    g_themeInitialized = true;
}

void ReleaseTheme()
{
    CleanupBrushes();
    g_themeInitialized = false;
}

bool IsDarkMode()
{
    return PluginDarkMode_ShouldUseDark() != FALSE;
}

const ThemeColors& GetTheme()
{
    if (!g_themeInitialized)
        InitTheme();
    return g_theme;
}

void ApplyWindowTheme(HWND hwnd)
{
    if (!hwnd) return;
    if (IsDarkMode())
    {
        PluginDarkMode_ApplyTitleBar(hwnd);
        ApplyDialogControlsTheme(hwnd);
    }
}

static LRESULT CALLBACK GroupBoxSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (uMsg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (hdc)
        {
            RECT rc;
            GetClientRect(hwnd, &rc);

            InitTheme();

            // Background
            FillRect(hdc, &rc, g_theme.brushBgMain);

            char text[256] = {0};
            GetWindowTextA(hwnd, text, sizeof(text));
            HFONT hFont = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
            HFONT hOldFont = NULL;
            if (hFont) hOldFont = (HFONT)SelectObject(hdc, hFont);

            SIZE textSize = {0, 0};
            if (text[0] != '\0')
            {
                GetTextExtentPoint32A(hdc, text, (int)strlen(text), &textSize);
            }

            int topOffset = (textSize.cy > 0) ? (textSize.cy / 2) : 8;
            RECT frameRc = rc;
            frameRc.top += topOffset;

            // Draw groupbox frame
            HPEN hPen = CreatePen(PS_SOLID, 1, RGB(70, 70, 70));
            HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
            HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));

            RoundRect(hdc, frameRc.left, frameRc.top, frameRc.right, frameRc.bottom, 4, 4);

            SelectObject(hdc, hOldBrush);
            SelectObject(hdc, hOldPen);
            DeleteObject(hPen);

            // Draw label
            if (text[0] != '\0')
            {
                int textHeight = (textSize.cy > 0 ? textSize.cy : 16);
                RECT textBgRc = { rc.left + 8, 0, rc.left + 8 + textSize.cx + 8, textHeight };
                FillRect(hdc, &textBgRc, g_theme.brushBgMain);

                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, g_theme.textMain);
                RECT drawRc = { rc.left + 12, 0, rc.left + 12 + textSize.cx, textHeight };
                DrawTextA(hdc, text, -1, &drawRc, DT_LEFT | DT_SINGLELINE);
            }

            if (hOldFont) SelectObject(hdc, hOldFont);
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    else if (uMsg == WM_ERASEBKGND)
    {
        return 1;
    }
    else if (uMsg == WM_NCDESTROY)
    {
        RemoveWindowSubclass(hwnd, GroupBoxSubclassProc, uIdSubclass);
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK TabControlSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (uMsg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (hdc)
        {
            RECT rcClient;
            GetClientRect(hwnd, &rcClient);

            InitTheme();

            // Fill entire tab strip background
            FillRect(hdc, &rcClient, g_theme.brushBgMain);

            int tabCount = TabCtrl_GetItemCount(hwnd);
            int curSel = TabCtrl_GetCurSel(hwnd);

            HFONT hFont = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
            HFONT hOldFont = NULL;
            if (hFont) hOldFont = (HFONT)SelectObject(hdc, hFont);

            HBRUSH hBrushActive = CreateSolidBrush(RGB(48, 48, 48));
            HBRUSH hBrushInactive = CreateSolidBrush(RGB(32, 32, 32));
            HPEN hPenBorder = CreatePen(PS_SOLID, 1, RGB(65, 65, 65));
            HPEN hPenActiveBorder = CreatePen(PS_SOLID, 1, RGB(90, 90, 90));
            HPEN hPenAccent = CreatePen(PS_SOLID, 2, RGB(0, 120, 215));

            for (int i = 0; i < tabCount; ++i)
            {
                RECT rcItem;
                if (!TabCtrl_GetItemRect(hwnd, i, &rcItem))
                    continue;

                char text[128] = {0};
                TCITEMA tci;
                memset(&tci, 0, sizeof(tci));
                tci.mask = TCIF_TEXT;
                tci.pszText = text;
                tci.cchTextMax = sizeof(text);
                TabCtrl_GetItem(hwnd, i, &tci);

                bool isSelected = (i == curSel);

                if (isSelected)
                {
                    // Draw active tab background
                    FillRect(hdc, &rcItem, hBrushActive);

                    // Draw active tab border
                    HPEN hOldPen = (HPEN)SelectObject(hdc, hPenActiveBorder);
                    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
                    Rectangle(hdc, rcItem.left, rcItem.top, rcItem.right, rcItem.bottom + 1);
                    SelectObject(hdc, hOldBrush);

                    // Top blue accent line
                    SelectObject(hdc, hPenAccent);
                    MoveToEx(hdc, rcItem.left + 1, rcItem.top + 1, NULL);
                    LineTo(hdc, rcItem.right - 1, rcItem.top + 1);
                    SelectObject(hdc, hOldPen);

                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, RGB(255, 255, 255));
                    DrawTextA(hdc, text, -1, &rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                else
                {
                    // Inactive tab
                    FillRect(hdc, &rcItem, hBrushInactive);

                    HPEN hOldPen = (HPEN)SelectObject(hdc, hPenBorder);
                    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
                    Rectangle(hdc, rcItem.left, rcItem.top, rcItem.right, rcItem.bottom);
                    SelectObject(hdc, hOldBrush);
                    SelectObject(hdc, hOldPen);

                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, RGB(180, 180, 180));
                    DrawTextA(hdc, text, -1, &rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
            }

            DeleteObject(hBrushActive);
            DeleteObject(hBrushInactive);
            DeleteObject(hPenBorder);
            DeleteObject(hPenActiveBorder);
            DeleteObject(hPenAccent);

            if (hOldFont) SelectObject(hdc, hOldFont);

            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    else if (uMsg == WM_ERASEBKGND)
    {
        return 1;
    }
    else if (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONUP || uMsg == WM_RBUTTONDOWN)
    {
        LRESULT res = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        InvalidateRect(hwnd, NULL, TRUE);
        UpdateWindow(hwnd);
        return res;
    }
    else if (uMsg == WM_NCDESTROY)
    {
        RemoveWindowSubclass(hwnd, TabControlSubclassProc, uIdSubclass);
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

static BOOL CALLBACK EnumChildProc(HWND hwndChild, LPARAM lParam)
{
    char className[64] = {0};
    GetClassNameA(hwndChild, className, sizeof(className));

    if (_stricmp(className, "Edit") == 0)
    {
        SetWindowTheme(hwndChild, L"DarkMode_CFD", NULL);
    }
    else if (_stricmp(className, "ComboBox") == 0)
    {
        SetWindowTheme(hwndChild, L"DarkMode_CFD", NULL);
    }
    else if (_stricmp(className, "Button") == 0)
    {
        LONG style = GetWindowLong(hwndChild, GWL_STYLE);
        if ((style & BS_TYPEMASK) == BS_GROUPBOX)
        {
            SetWindowSubclass(hwndChild, GroupBoxSubclassProc, 1, 0);
        }
        else
        {
            SetWindowTheme(hwndChild, L"DarkMode_Explorer", NULL);
        }
    }
    else if (_stricmp(className, "msctls_progress32") == 0)
    {
        SendMessage(hwndChild, PBM_SETBARCOLOR, 0, (LPARAM)RGB(0, 120, 215));
        SendMessage(hwndChild, PBM_SETBKCOLOR, 0, (LPARAM)RGB(45, 45, 45));
    }
    else if (_stricmp(className, "SysTabControl32") == 0)
    {
        SetWindowSubclass(hwndChild, TabControlSubclassProc, 1, 0);
    }
    else if (_stricmp(className, "SysListView32") == 0)
    {
        ApplyListViewTheme(hwndChild);
    }
    return TRUE;
}

void ApplyDialogControlsTheme(HWND hwndDlg)
{
    if (!hwndDlg) return;
    if (IsDarkMode())
    {
        EnumChildWindows(hwndDlg, EnumChildProc, 0);
    }
}

BOOL HandleDialogColors(UINT uMsg, WPARAM wParam, LPARAM lParam, INT_PTR* pResult)
{
    if (!IsDarkMode()) return FALSE;

    InitTheme();

    switch (uMsg)
    {
    case WM_CTLCOLORDLG:
        if (pResult) *pResult = (INT_PTR)g_theme.brushBgMain;
        return TRUE;

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, g_theme.textMain);
        SetBkColor(hdc, g_theme.bgMain);
        if (pResult) *pResult = (INT_PTR)g_theme.brushBgMain;
        return TRUE;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, g_theme.editText);
        SetBkColor(hdc, g_theme.editBg);
        if (pResult) *pResult = (INT_PTR)g_theme.brushEditBg;
        return TRUE;
    }

    case WM_CTLCOLORLISTBOX:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, g_theme.textMain);
        SetBkColor(hdc, g_theme.bgMain);
        if (pResult) *pResult = (INT_PTR)g_theme.brushBgMain;
        return TRUE;
    }

    case WM_CTLCOLORBTN:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, g_theme.textMain);
        SetBkColor(hdc, g_theme.bgMain);
        if (pResult) *pResult = (INT_PTR)g_theme.brushBgMain;
        return TRUE;
    }
    }

    return FALSE;
}

void ApplyListViewTheme(HWND hwndList)
{
    if (!hwndList) return;

    InitTheme();
    ListView_SetBkColor(hwndList, g_theme.bgMain);
    ListView_SetTextBkColor(hwndList, g_theme.bgMain);
    ListView_SetTextColor(hwndList, g_theme.textMain);

    if (IsDarkMode())
    {
        SetWindowTheme(hwndList, L"DarkMode_Explorer", NULL);
    }
    else
    {
        SetWindowTheme(hwndList, L"Explorer", NULL);
    }
}

void ApplyEditTheme(HWND hwndEdit)
{
    if (!hwndEdit) return;
    if (IsDarkMode())
    {
        SetWindowTheme(hwndEdit, L"DarkMode_CFD", NULL);
    }
    else
    {
        SetWindowTheme(hwndEdit, L"Explorer", NULL);
    }
}

void ApplyStatusBarTheme(HWND hwndStatus)
{
    if (!hwndStatus) return;
    if (IsDarkMode())
    {
        SetWindowTheme(hwndStatus, L"DarkMode_Explorer", NULL);
    }
    else
    {
        SetWindowTheme(hwndStatus, NULL, NULL);
    }
}

} // namespace GDriveDarkMode
