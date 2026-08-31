// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "gdrive_fs.h"
#include "gdrive_auth.h"
#include "gdrive_api.h"
#include "gdrive_cache.h"
#include "gdrive_http.h"
#include "gdrive_log.h"
#include "dialogs.h"

BOOL WINAPI CPluginFS::QuickRename(const char* fsName, int mode, HWND parent, CFileData& file,
                                   BOOL isDir, char* newName, BOOL& cancel)
{
    cancel = FALSE;
    if (mode == 1)
        return FALSE; // request the standard dialog

    if (!newName || !*newName || !file.Name) return FALSE;

    if (m_currentPath.empty() || m_currentPath == "/" || _stricmp(m_currentPath.c_str(), "/") == 0)
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_CANNOT_RENAME_ROOT), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }

    std::string fileAnsiName = file.Name;
    std::string fileUtf8Name = GDriveHttp::HttpClient::AnsiToUtf8(file.Name);

    const GDriveApi::GDriveItem* pTarget = FindItemByPanelName(file.Name);
    if (pTarget && !pTarget->canRename)
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_NO_PERMISSION_RENAME), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    std::string fileId = pTarget ? pTarget->id : "";

    if (fileId.empty())
    {
        std::string fullPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + fileAnsiName;
        auto it = m_pathToIdCache.find(fullPath);
        if (it != m_pathToIdCache.end())
        {
            fileId = it->second;
        }
        else
        {
            fullPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + fileUtf8Name;
            it = m_pathToIdCache.find(fullPath);
            if (it != m_pathToIdCache.end()) fileId = it->second;
        }
    }

    if (fileId.empty())
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_PATH_NOT_FOUND), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
        return FALSE;
    }

    std::string utf8NewName = GDriveHttp::HttpClient::AnsiToUtf8(newName);
    std::string err;
    if (!GDriveApi::ApiClient::GetInstance().RenameItem(fileId, utf8NewName, &err))
    {
        SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
        return FALSE;
    }

    std::string oldSubPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + fileAnsiName;
    std::string newSubPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + newName;
    m_pathToIdCache.erase(oldSubPath);
    m_pathToIdCache[newSubPath] = fileId;

    std::string oldUtf8SubPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + fileUtf8Name;
    std::string newUtf8SubPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + utf8NewName;
    m_pathToIdCache.erase(oldUtf8SubPath);
    m_pathToIdCache[newUtf8SubPath] = fileId;

    for (auto& it : m_cachedItems)
    {
        if (it.id == fileId)
        {
            it.name = utf8NewName;
            break;
        }
    }

    GDriveCache::CacheManager::GetInstance().RenameItem(m_currentFolderId, fileId, utf8NewName);

    SalamanderGeneral->RefreshPanelPath(PANEL_SOURCE);
    SalamanderGeneral->PostRefreshPanelFS(this);

    return TRUE;
}

void WINAPI CPluginFS::AcceptChangeOnPathNotification(const char* fsName, const char* path, BOOL includingSubdirs)
{
}

BOOL WINAPI CPluginFS::CreateDir(const char* fsName, int mode, HWND parent, char* newName, BOOL& cancel)
{
    cancel = FALSE;
    if (mode == 1)
        return FALSE; // request standard dialog

    if (!newName || !*newName) return FALSE;

    if (m_currentPath.empty() || m_currentPath == "/" || _stricmp(m_currentPath.c_str(), "/") == 0)
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_CANNOT_CREATE_DIR_ROOT), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }

    if (_stricmp(m_currentPath.c_str(), "/Shared with me") == 0)
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_CANNOT_CREATE_DIR_SHARED_WITH_ME), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }

    if (_stricmp(m_currentPath.c_str(), "/Shared Drives") == 0)
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_CANNOT_CREATE_DIR_SHARED_DRIVES), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }

    if (!ResolveCurrentFolderId() || m_currentFolderId.empty())
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_PATH_NOT_FOUND), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
        return FALSE;
    }

    std::string utf8NewName = GDriveHttp::HttpClient::AnsiToUtf8(newName);
    GDriveApi::GDriveItem newItem;
    std::string err;
    if (!GDriveApi::ApiClient::GetInstance().CreateFolder(m_currentFolderId, utf8NewName, newItem, &err))
    {
        SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
        return FALSE;
    }

    std::string subPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + newName;
    m_pathToIdCache[subPath] = newItem.id;
    std::string utf8SubPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + utf8NewName;
    m_pathToIdCache[utf8SubPath] = newItem.id;

    m_cachedItems.push_back(newItem);
    GDriveCache::CacheManager::GetInstance().AddOrUpdateItem(m_currentFolderId, newItem);

    SalamanderGeneral->RefreshPanelPath(PANEL_SOURCE);
    SalamanderGeneral->PostRefreshPanelFS(this);
    return TRUE;
}

BOOL WINAPI CPluginFS::Delete(const char* fsName, int mode, HWND parent, int panel,
                              int selectedFiles, int selectedDirs, BOOL& cancelOrError)
{
    cancelOrError = FALSE;

    if (m_currentPath.empty() || m_currentPath == "/" || _stricmp(m_currentPath.c_str(), "/") == 0 ||
        _stricmp(m_currentPath.c_str(), "/Shared Drives") == 0 || _stricmp(m_currentPath.c_str(), "/Shared with me") == 0)
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_CANNOT_DELETE_ROOT), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        cancelOrError = TRUE;
        return FALSE;
    }

    bool shiftPressed = (GetKeyState(VK_SHIFT) < 0);

    std::vector<std::pair<std::string, std::string>> itemsToDelete; // name, id

    if (selectedFiles == 0 && selectedDirs == 0)
    {
        BOOL isDir = FALSE;
        const CFileData* f = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
        if (f && strcmp(f->Name, "..") != 0)
        {
            const GDriveApi::GDriveItem* target = FindItemByPanelName(f->Name);
            if (target)
            {
                itemsToDelete.emplace_back(f->Name, target->id);
            }
            else
            {
                std::string fullPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + std::string(f->Name);
                auto it = m_pathToIdCache.find(fullPath);
                if (it != m_pathToIdCache.end()) itemsToDelete.emplace_back(f->Name, it->second);
            }
        }
    }
    else
    {
        int index = 0;
        BOOL isDir = FALSE;
        const CFileData* f = NULL;
        while ((f = SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir)) != NULL)
        {
            if (strcmp(f->Name, "..") == 0) continue;
            const GDriveApi::GDriveItem* target = FindItemByPanelName(f->Name);
            if (target)
            {
                itemsToDelete.emplace_back(f->Name, target->id);
            }
            else
            {
                std::string fullPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + std::string(f->Name);
                auto it = m_pathToIdCache.find(fullPath);
                if (it != m_pathToIdCache.end()) itemsToDelete.emplace_back(f->Name, it->second);
            }
        }
    }

    if (itemsToDelete.empty())
    {
        return TRUE;
    }

    char confirmMsg[512] = {0};
    if (itemsToDelete.size() == 1)
    {
        snprintf(confirmMsg, sizeof(confirmMsg),
                 shiftPressed ? LoadStr(IDS_CONFIRM_DELETE_PERM_SINGLE) : LoadStr(IDS_CONFIRM_DELETE_TRASH_SINGLE),
                 itemsToDelete[0].first.c_str());
    }
    else
    {
        snprintf(confirmMsg, sizeof(confirmMsg),
                 shiftPressed ? LoadStr(IDS_CONFIRM_DELETE_PERM_MULTI) : LoadStr(IDS_CONFIRM_DELETE_TRASH_MULTI),
                 (int)itemsToDelete.size());
    }

    if (SalamanderGeneral->SalMessageBox(parent, confirmMsg, LoadStr(IDS_PLUGINNAME),
                                         MB_YESNOCANCEL | (shiftPressed ? MB_ICONWARNING : MB_ICONQUESTION)) != IDYES)
    {
        cancelOrError = TRUE;
        if (parent)
        {
            SetForegroundWindow(parent);
            SetFocus(parent);
        }
        return FALSE;
    }

    CTransferProgressDialog progressDlg(parent, false, "", (int64_t)itemsToDelete.size());
    progressDlg.SetActionLabel(IDS_TRANSFER_DELETING);
    bool progressStarted = false;
    if (itemsToDelete.size() > 1)
    {
        progressDlg.Start();
        progressStarted = true;
    }

    int deletedCount = 0;
    for (const auto& [name, id] : itemsToDelete)
    {
        if (progressDlg.IsCancelled())
        {
            cancelOrError = TRUE;
            break;
        }

        const GDriveApi::GDriveItem* target = FindItemByPanelName(name.c_str());
        if (target)
        {
            if (shiftPressed && !target->canDelete)
            {
                SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_NO_PERMISSION_DELETE), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
                continue;
            }
            else if (!shiftPressed && !target->canTrash && !target->canDelete)
            {
                SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_NO_PERMISSION_DELETE), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
                continue;
            }
        }

        if (progressStarted)
        {
            progressDlg.SetCurrentFile(name, (int64_t)itemsToDelete.size());
            progressDlg.OnProgress(deletedCount, (int64_t)itemsToDelete.size());
        }

        std::string err;
        bool ok = shiftPressed ? GDriveApi::ApiClient::GetInstance().DeleteItem(id, &err)
                                : GDriveApi::ApiClient::GetInstance().TrashItem(id, &err);
        if (!ok)
        {
            if (progressStarted) progressDlg.Stop();
            SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            cancelOrError = TRUE;
            break;
        }

        deletedCount++;
        std::string subPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + name;
        m_pathToIdCache.erase(subPath);

        for (auto it = m_cachedItems.begin(); it != m_cachedItems.end(); ++it)
        {
            if (it->id == id)
            {
                m_cachedItems.erase(it);
                break;
            }
        }

        GDriveCache::CacheManager::GetInstance().RemoveItem(m_currentFolderId, id);
    }

    if (progressStarted)
    {
        progressDlg.Stop();
    }

    SalamanderGeneral->RefreshPanelPath(panel);
    if (parent)
    {
        SetForegroundWindow(parent);
        SetFocus(parent);
    }
    return !cancelOrError;
}

BOOL WINAPI CPluginFS::ChangeAttributes(const char* fsName, HWND parent, int panel,
                                        int selectedFiles, int selectedDirs)
{
    return FALSE;
}

void WINAPI CPluginFS::ShowProperties(const char* fsName, HWND parent, int panel,
                                      int selectedFiles, int selectedDirs)
{
    BOOL isDir = FALSE;
    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
    int index = 0;
    const CFileData* f = focused ? SalamanderGeneral->GetPanelFocusedItem(panel, &isDir)
                                 : SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);
    if (!f) return;

    char info[1024] = {0};
    uint64_t size = f->Size.Value;
    snprintf(info, sizeof(info), LoadStr(IDS_PROPERTIES_FMT),
             f->Name, isDir ? "Folder" : "File", (unsigned long long)size, m_currentPath.c_str());

    SalamanderGeneral->SalMessageBox(parent, info, LoadStr(IDS_PROPERTIES_TITLE), MB_OK | MB_ICONINFORMATION);
}

void WINAPI CPluginFS::ContextMenu(const char* fsName, HWND parent, int menuX, int menuY, int type,
                                    int panel, int selectedFiles, int selectedDirs)
{
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    BOOL isDir = FALSE;
    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
    int index = 0;
    const CFileData* f = focused ? SalamanderGeneral->GetPanelFocusedItem(panel, &isDir)
                                 : SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);

    GDriveApi::GDriveItem targetItem;
    bool hasTarget = false;
    if (f && f->Name && f->Name[0] && strcmp(f->Name, "..") != 0)
    {
        const GDriveApi::GDriveItem* pItem = FindItemByPanelName(f->Name);
        if (pItem)
        {
            targetItem = *pItem;
            hasTarget = true;
        }
    }

    bool isInsideTrash = (_stricmp(m_currentPath.c_str(), "/Trash") == 0);

    if (isInsideTrash)
    {
        if (hasTarget)
        {
            AppendMenuA(hMenu, MF_STRING, CM_RESTORE_TRASH, LoadStr(IDS_MENU_RESTORE_TRASH));
            AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
        }
        AppendMenuA(hMenu, MF_STRING, CM_EMPTY_TRASH, LoadStr(IDS_MENU_EMPTY_TRASH));
    }
    else
    {
        if (hasTarget)
        {
            if (!targetItem.webViewLink.empty())
            {
                AppendMenuA(hMenu, MF_STRING, CM_OPEN_IN_BROWSER, LoadStr(IDS_MENU_OPEN_IN_BROWSER));
                AppendMenuA(hMenu, MF_STRING, CM_COPY_LINK, LoadStr(IDS_MENU_COPY_LINK));
                AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
            }

            UINT starFlags = targetItem.canEdit ? MF_STRING : (MF_STRING | MF_GRAYED | MF_DISABLED);
            if (targetItem.isStarred)
            {
                AppendMenuA(hMenu, starFlags, CM_REMOVE_STAR, LoadStr(IDS_MENU_REMOVE_STAR));
            }
            else
            {
                AppendMenuA(hMenu, starFlags, CM_ADD_STAR, LoadStr(IDS_MENU_ADD_STAR));
            }
            AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
        }

        AppendMenuA(hMenu, MF_STRING, CM_CALC_SIZE, LoadStr(IDS_MENU_CALC_SIZE));
    }

    int cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
                             menuX, menuY, 0, parent, NULL);
    DestroyMenu(hMenu);

    if (cmd == CM_CALC_SIZE)
    {
        CalculateFolderSize(parent, panel);
    }
    else if (cmd == CM_OPEN_IN_BROWSER && hasTarget && !targetItem.webViewLink.empty())
    {
        ShellExecuteA(NULL, "open", targetItem.webViewLink.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    else if (cmd == CM_COPY_LINK && hasTarget && !targetItem.webViewLink.empty())
    {
        if (OpenClipboard(parent))
        {
            EmptyClipboard();
            HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, targetItem.webViewLink.length() + 1);
            if (hGlob)
            {
                char* p = (char*)GlobalLock(hGlob);
                if (p)
                {
                    strcpy(p, targetItem.webViewLink.c_str());
                    GlobalUnlock(hGlob);
                    SetClipboardData(CF_TEXT, hGlob);
                }
            }
            CloseClipboard();
            SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_LINK_COPIED), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONINFORMATION);
        }
    }
    else if ((cmd == CM_ADD_STAR || cmd == CM_REMOVE_STAR) && hasTarget)
    {
        bool makeStarred = (cmd == CM_ADD_STAR);
        std::string err;
        if (GDriveApi::ApiClient::GetInstance().SetStarred(targetItem.id, makeStarred, &err))
        {
            GDriveCache::CacheManager::GetInstance().SetStarStatus(targetItem.id, makeStarred);
            SalamanderGeneral->RefreshPanelPath(panel);
        }
        else
        {
            SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
        }
    }
    else if (cmd == CM_RESTORE_TRASH && hasTarget)
    {
        std::string err;
        if (GDriveApi::ApiClient::GetInstance().RestoreFromTrash(targetItem.id, &err))
        {
            GDriveCache::CacheManager::GetInstance().RemoveItem("trash_root", targetItem.id);
            SalamanderGeneral->RefreshPanelPath(panel);
        }
        else
        {
            SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
        }
    }
    else if (cmd == CM_EMPTY_TRASH)
    {
        if (SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_CONFIRM_EMPTY_TRASH), LoadStr(IDS_PLUGINNAME),
                                             MB_YESNO | MB_ICONWARNING) == IDYES)
        {
            std::string err;
            if (GDriveApi::ApiClient::GetInstance().EmptyTrash(&err))
            {
                GDriveCache::CacheManager::GetInstance().InvalidateFolder("trash_root");
                SalamanderGeneral->RefreshPanelPath(panel);
            }
            else
            {
                SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            }
        }
    }
}

BOOL WINAPI CPluginFS::HandleMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* plResult)
{
    return FALSE;
}

void WINAPI CPluginFS::ViewFile(const char* fsName, HWND parent,
                                CSalamanderForViewFileOnFSAbstract* salamander,
                                CFileData& file)
{
    if (!file.Name || !salamander) return;

    const GDriveApi::GDriveItem* targetItem = FindItemByPanelName(file.Name);
    if (!targetItem) return;

    std::string uniqueFileName = std::string(fsName ? fsName : "gdrive") + ":" + m_currentPath + "/" + targetItem->id + "_" + targetItem->name;
    if (targetItem->isGoogleDoc && !targetItem->exportExtension.empty())
        uniqueFileName += targetItem->exportExtension;

    BOOL fileExists = FALSE;
    const char* tmpFileName = salamander->AllocFileNameInCache(parent, uniqueFileName.c_str(), file.Name, NULL, fileExists);
    if (!tmpFileName) return;

    BOOL newFileOK = FALSE;
    CQuadWord newFileSize(0, 0);

    if (!fileExists)
    {
        std::wstring wTempPath = GDriveHttp::HttpClient::AnsiToWide(tmpFileName);
        std::string err;
        newFileOK = GDriveApi::ApiClient::GetInstance().DownloadFile(*targetItem, wTempPath, nullptr, nullptr, &err);
        if (!newFileOK)
        {
            salamander->FreeFileNameInCache(uniqueFileName.c_str(), FALSE, FALSE, newFileSize, NULL, FALSE, TRUE);
            SalamanderGeneral->SalMessageBox(parent, err.empty() ? LoadStr(IDS_DOWNLOAD_FAILED) : err.c_str(),
                                             LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            return;
        }

        HANDLE hF = CreateFileW(wTempPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (hF != INVALID_HANDLE_VALUE)
        {
            DWORD dwHigh = 0;
            DWORD dwLow = GetFileSize(hF, &dwHigh);
            newFileSize = CQuadWord(dwLow, dwHigh);
            CloseHandle(hF);
        }
    }

    HANDLE fileLock = NULL;
    BOOL fileLockOwner = FALSE;
    salamander->OpenViewer(parent, tmpFileName, &fileLock, &fileLockOwner);
    salamander->FreeFileNameInCache(uniqueFileName.c_str(), fileExists, newFileOK, newFileSize, fileLock, fileLockOwner, FALSE);
}

void CPluginFS::CalculateFolderSize(HWND parent, int panel)
{
    int selectedFiles = 0;
    int selectedDirs = 0;
    SalamanderGeneral->GetPanelSelection(panel, &selectedFiles, &selectedDirs);

    std::vector<std::string> folderNamesToCalc;
    if (selectedDirs > 0)
    {
        int itIdx = 0;
        BOOL isDir = FALSE;
        const CFileData* item = NULL;
        while ((item = SalamanderGeneral->GetPanelSelectedItem(panel, &itIdx, &isDir)) != NULL)
        {
            if (isDir && strcmp(item->Name, "..") != 0)
            {
                folderNamesToCalc.push_back(item->Name);
            }
        }
    }

    if (folderNamesToCalc.empty())
    {
        BOOL isDir = FALSE;
        const CFileData* f = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
        if (!f) return;

        if (!isDir)
        {
            char msg[512];
            std::string szStr = CCalcSizeProgressDialog::FormatSize(f->Size.Value);
            std::string numStr = CCalcSizeProgressDialog::FormatNumber(f->Size.Value);
            _snprintf_s(msg, _TRUNCATE, LoadStr(IDS_CALC_RESULT_FMT), f->Name, 0, 1, szStr.c_str(), numStr.c_str());
            SalamanderGeneral->SalMessageBox(parent, msg, LoadStr(IDS_CALC_TITLE), MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (strcmp(f->Name, "..") == 0)
        {
            std::string curName = m_currentPath;
            if (curName.empty() || curName == "/")
                curName = "Google Drive (Root)";
            else
            {
                size_t p = curName.rfind('/');
                if (p != std::string::npos) curName = curName.substr(p + 1);
            }

            std::string folderId = m_currentFolderId.empty() ? "root" : m_currentFolderId;
            CCalcSizeProgressDialog dlg(parent, curName.c_str(), folderId, m_currentDriveId, m_isSharedDrive);
            dlg.Run();
            return;
        }

        folderNamesToCalc.push_back(f->Name);
    }

    std::map<std::string, int64_t> calcResults;
    for (const auto& folderName : folderNamesToCalc)
    {
        std::string folderId;
        const GDriveApi::GDriveItem* targetItem = FindItemByPanelName(folderName.c_str());
        if (targetItem)
        {
            folderId = targetItem->id;
        }

        if (folderId.empty())
        {
            if (_stricmp(folderName.c_str(), "My Drive") == 0 || _stricmp(folderName.c_str(), LoadStr(IDS_MY_DRIVE)) == 0)
            {
                folderId = "root";
            }
            else if (_stricmp(folderName.c_str(), "Shared with me") == 0 || _stricmp(folderName.c_str(), LoadStr(IDS_SHARED_WITH_ME)) == 0)
            {
                folderId = "shared_with_me_root";
            }
        }

        if (folderId.empty()) continue;

        CCalcSizeProgressDialog dlg(parent, folderName.c_str(), folderId, m_currentDriveId, m_isSharedDrive);
        if (dlg.Run())
        {
            calcResults[folderName] = dlg.GetTotalBytes();
        }
        else if (dlg.WasCancelled())
        {
            break;
        }
    }

    if (!calcResults.empty())
    {
        int itIdx = 0;
        BOOL itemIsDir = FALSE;
        const CFileData* item = NULL;
        while ((item = SalamanderGeneral->GetPanelItem(panel, &itIdx, &itemIsDir)) != NULL)
        {
            if (itemIsDir)
            {
                auto itRes = calcResults.find(item->Name);
                if (itRes != calcResults.end())
                {
                    CFileData* nonConst = const_cast<CFileData*>(item);
                    nonConst->Size.Value = itRes->second;
                    nonConst->SizeValid = 1;
                    nonConst->Dirty = 1;
                }
            }
        }
        SalamanderGeneral->RepaintChangedItems(panel);
    }
}

void CPluginFS::OnSpacePressedOnFolder(int panel, const CFileData* f)
{
    if (!f || !f->Name || strcmp(f->Name, "..") == 0) return;

    std::string folderId;
    std::string folderName = f->Name;
    const GDriveApi::GDriveItem* targetItem = FindItemByPanelName(f->Name);
    if (targetItem)
    {
        folderId = targetItem->id;
    }

    if (folderId.empty())
    {
        if (_stricmp(folderName.c_str(), "My Drive") == 0 || _stricmp(folderName.c_str(), LoadStr(IDS_MY_DRIVE)) == 0)
            folderId = "root";
        else if (_stricmp(folderName.c_str(), "Shared with me") == 0 || _stricmp(folderName.c_str(), LoadStr(IDS_SHARED_WITH_ME)) == 0)
            folderId = "shared_with_me_root";
    }

    int itIdx = 0;
    BOOL itemIsDir = FALSE;
    const CFileData* item = NULL;
    const CFileData* targetF = NULL;
    const CFileData* nextF = NULL;
    bool foundCurrent = false;

    while ((item = SalamanderGeneral->GetPanelItem(panel, &itIdx, &itemIsDir)) != NULL)
    {
        if (!foundCurrent)
        {
            if (item == f || (item->Name && strcmp(item->Name, folderName.c_str()) == 0))
            {
                targetF = item;
                foundCurrent = true;
            }
        }
        else
        {
            nextF = item;
            break;
        }
    }

    if (!targetF) targetF = f;

    // Toggle selection on the folder
    BOOL newSelected = !targetF->Selected;
    SalamanderGeneral->SelectPanelItem(panel, targetF, newSelected);

    // If size is in cache, display it directly
    int64_t folderSize = 0;
    int files = 0, dirs = 0;
    if (!folderId.empty() &&
        (GDriveCache::CacheManager::GetInstance().GetFolderSize(folderId, folderSize) ||
         GDriveCache::CacheManager::GetInstance().ComputeFolderSizeFromCache(folderId, folderSize, files, dirs)))
    {
        GDriveLog::Log("[SPACE] Folder '%s' (ID: %s) -> Found in cache: %lld B. Displaying in panel directly.",
                       folderName.c_str(), folderId.c_str(), (long long)folderSize);

        CFileData* nonConst = const_cast<CFileData*>(targetF);
        nonConst->Size.Value = folderSize;
        nonConst->SizeValid = 1;
        nonConst->Dirty = 1;
    }
    else if (!folderId.empty())
    {
        GDriveLog::Log("[SPACE] Folder '%s' (ID: %s) -> Not in size cache. Launching calculation dialog.",
                       folderName.c_str(), folderId.c_str());
        HWND hMain = SalamanderGeneral->GetMainWindowHWND();
        CCalcSizeProgressDialog dlg(hMain, folderName.c_str(), folderId, m_currentDriveId, m_isSharedDrive);
        if (dlg.Run())
        {
            folderSize = dlg.GetTotalBytes();
            CFileData* nonConst = const_cast<CFileData*>(targetF);
            nonConst->Size.Value = folderSize;
            nonConst->SizeValid = 1;
            nonConst->Dirty = 1;
        }
    }

    // Advance focus to next item
    if (nextF)
    {
        SalamanderGeneral->SetPanelFocusedItem(panel, nextF, FALSE);
    }

    SalamanderGeneral->RepaintChangedItems(panel);
}
