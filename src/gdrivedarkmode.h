// SPDX-FileCopyrightText: 2026 fila73 & Ondrej Kotas
// SPDX-FileContributor: Dark Mode host policy and theme integration based on fork by Ondrej Kotas (KRtkovo-eu-AI)
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>
#include <commctrl.h>

namespace GDriveDarkMode
{

struct ThemeColors
{
    COLORREF bgMain;
    COLORREF bgAlternate;
    COLORREF bgSelected;
    COLORREF textMain;
    COLORREF textDimmed;
    COLORREF textSelected;
    COLORREF gridLine;
    COLORREF headerBg;
    COLORREF headerText;
    COLORREF editBg;
    COLORREF editText;
    COLORREF statusBg;
    COLORREF statusText;
    COLORREF toolbarBg;
    HBRUSH brushBgMain;
    HBRUSH brushBgAlternate;
    HBRUSH brushBgSelected;
    HBRUSH brushEditBg;
    HBRUSH brushToolbarBg;
    HBRUSH brushStatusBg;
    HPEN penGrid;
};

void InitTheme();
void ReleaseTheme();
bool IsDarkMode();
const ThemeColors& GetTheme();

void ApplyWindowTheme(HWND hwnd);
void ApplyListViewTheme(HWND hwndList);
void ApplyEditTheme(HWND hwndEdit);
void ApplyStatusBarTheme(HWND hwndStatus);

} // namespace GDriveDarkMode
