// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef __GDRIVE_DIALOG_FIND_H
#define __GDRIVE_DIALOG_FIND_H

#include "precomp.h"
#include "gdrive_api.h"
#include "gdrive_fs.h"
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

class CGDriveFindDialog
{
public:
    CGDriveFindDialog(HWND hParent, CPluginFS* pFS, int panel, const std::string& currentPath, const std::string& currentFolderId);
    ~CGDriveFindDialog();

    bool ShowModal();

private:
    static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
    INT_PTR HandleMessage(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void OnInitDialog(HWND hDlg);
    void OnCommand(HWND hDlg, int id, HWND hCtrl, UINT codeNotify);
    void OnSize(HWND hDlg, UINT state, int cx, int cy);
    void OnGetMinMaxInfo(HWND hDlg, LPMINMAXINFO lpMMI);
    void OnDestroy(HWND hDlg);

    void StartSearch();
    void StopSearch();
    void SearchWorker();

    void PopulateResults(const std::vector<GDriveApi::GDriveItem>& items);
    void UpdateControlsState();
    void FocusSelectedItem();
    void ViewSelectedItem();
    void OpenWebSelectedItem();
    void CopyLinkSelectedItem();
    void SortResults(int columnIndex);

    HWND m_hDlg = NULL;
    HWND m_hParent = NULL;
    CPluginFS* m_pFS = nullptr;
    int m_panel = 0;
    std::string m_initialPath;
    std::string m_initialFolderId;

    HWND m_hNamed = NULL;
    HWND m_hLookIn = NULL;
    HWND m_hSubdir = NULL;
    HWND m_hGrep = NULL;
    HWND m_hContaining = NULL;
    HWND m_hDocsOcr = NULL;
    HWND m_hCase = NULL;
    HWND m_hAdvanced = NULL;
    HWND m_hAdvancedText = NULL;
    HWND m_hFoundCount = NULL;
    HWND m_hResultsList = NULL;
    HWND m_hStatus = NULL;
    HWND m_hBtnFindNow = NULL;
    HWND m_hBtnStop = NULL;
    HWND m_hBtnFocus = NULL;
    HWND m_hBtnView = NULL;
    HWND m_hBtnOpenWeb = NULL;
    HWND m_hBtnCopyLink = NULL;

    std::vector<GDriveApi::GDriveItem> m_results;
    std::mutex m_resultsMutex;
    std::thread m_searchThread;
    std::atomic<bool> m_isSearching{false};
    std::atomic<bool> m_cancelRequested{false};
    uint64_t m_searchStartTick = 0;

    int m_sortColumn = 0;
    bool m_sortAscending = true;
    int m_typeFilter = 0; // 0: All

    POINT m_minSize = {500, 360};
    bool m_initialLayoutDone = false;
    RECT m_origClientRect = {0, 0, 0, 0};
    RECT m_rcNamed = {0, 0, 0, 0};
    RECT m_rcLookIn = {0, 0, 0, 0};
    RECT m_rcContaining = {0, 0, 0, 0};
    RECT m_rcFindNow = {0, 0, 0, 0};
    RECT m_rcStop = {0, 0, 0, 0};
    RECT m_rcBrowse = {0, 0, 0, 0};
    RECT m_rcLine1 = {0, 0, 0, 0};
    RECT m_rcLine2 = {0, 0, 0, 0};
    RECT m_rcFoundCount = {0, 0, 0, 0};
    RECT m_rcResultsList = {0, 0, 0, 0};
    RECT m_rcStatus = {0, 0, 0, 0};
};

#endif // __GDRIVE_DIALOG_FIND_H
