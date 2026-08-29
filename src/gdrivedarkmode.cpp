// SPDX-FileCopyrightText: 2026 Open Salamander Authors, Red Salamander Authors & Ondrej Kotas
// SPDX-FileContributor: Dark Mode host policy and theme integration based on fork by Ondrej Kotas (KRtkovo-eu-AI)
// SPDX-FileContributor: Ported to Open Salamander framework by fila73
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
    if (IsDarkMode())
    {
        PluginDarkMode_ApplyTitleBar(hwnd);
    }
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
