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

static std::wstring MakeUniqueLocalPath(const std::wstring& targetDir, const std::wstring& originalName)
{
    size_t dotPos = originalName.rfind(L'.');
    std::wstring base = (dotPos != std::wstring::npos) ? originalName.substr(0, dotPos) : originalName;
    std::wstring ext = (dotPos != std::wstring::npos) ? originalName.substr(dotPos) : L"";

    int counter = 1;
    while (true)
    {
        std::wstring candidate = targetDir;
        if (!candidate.empty() && candidate.back() != L'\\') candidate += L"\\";
        candidate += base + L" (" + std::to_wstring(counter) + L")" + ext;

        DWORD dw = GetFileAttributesW(candidate.c_str());
        if (dw == INVALID_FILE_ATTRIBUTES)
        {
            return candidate;
        }
        counter++;
    }
}

static void PrecalculateLocalItems(const std::wstring& localPath, bool isDir, int& totalCount, int64_t& totalBytes)
{
    if (!isDir)
    {
        totalCount++;
        HANDLE hF = CreateFileW(localPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (hF != INVALID_HANDLE_VALUE)
        {
            LARGE_INTEGER li;
            if (GetFileSizeEx(hF, &li))
            {
                totalBytes += li.QuadPart;
            }
            CloseHandle(hF);
        }
        return;
    }

    std::wstring searchPattern = localPath;
    if (!searchPattern.empty() && searchPattern.back() != L'\\') searchPattern += L"\\";
    searchPattern += L"*";

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;

            std::wstring subPath = localPath;
            if (!subPath.empty() && subPath.back() != L'\\') subPath += L"\\";
            subPath += fd.cFileName;

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                PrecalculateLocalItems(subPath, true, totalCount, totalBytes);
            }
            else
            {
                totalCount++;
                LARGE_INTEGER li;
                li.LowPart = fd.nFileSizeLow;
                li.HighPart = fd.nFileSizeHigh;
                totalBytes += li.QuadPart;
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
}

BOOL WINAPI CPluginFS::CopyOrMoveFromDiskToFS(BOOL copy, int mode, const char* fsName, HWND parent,
                                              const char* sourcePath, SalEnumSelection2 next,
                                              void* nextParam, int selectedFiles, int selectedDirs,
                                              char* targetPath, BOOL* cancelOrHandlePath)
{
    if (cancelOrHandlePath) *cancelOrHandlePath = FALSE;

    if (mode == 1)
    {
        // Pre-fill target path for Salamander Copy dialog
        if (targetPath)
        {
            std::string fsPrefix = std::string(fsName && *fsName ? fsName : AssignedFSName) + ":\\";
            std::string dest = fsPrefix;
            if (!m_currentPath.empty() && m_currentPath != "/")
            {
                std::string rel = m_currentPath;
                if (rel[0] == '/') rel = rel.substr(1);
                for (char& c : rel) { if (c == '/') c = '\\'; }
                dest += rel;
            }
            else
            {
                dest += "My Drive";
            }
            std::string ansiDest = GDriveHttp::HttpClient::Utf8ToAnsi(dest);
            strncpy(targetPath, ansiDest.c_str(), 2 * MAX_PATH - 1);
            targetPath[2 * MAX_PATH - 1] = '\0';
        }
        SalamanderGeneral->SalPathAppend(targetPath, "*.*", 2 * MAX_PATH);
        return TRUE; // Return TRUE so Salamander uses this path in standard Copy/Move dialog
    }
    if (mode == 4)
    {
        return FALSE;
    }

    m_batchConflictAction.reset();

    std::string uploadPath = m_currentPath;
    if (targetPath && *targetPath)
    {
        std::string tp = targetPath;
        size_t colon = tp.find(':');
        if (colon != std::string::npos) tp = tp.substr(colon + 1);
        std::replace(tp.begin(), tp.end(), '\\', '/');

        // Strip trailing wildcard masks like /*.*, /*, /?*
        size_t lastSlash = tp.rfind('/');
        if (lastSlash != std::string::npos)
        {
            std::string lastComp = tp.substr(lastSlash + 1);
            if (lastComp.find('*') != std::string::npos || lastComp.find('?') != std::string::npos)
            {
                tp = tp.substr(0, lastSlash);
            }
        }

        while (tp.size() > 1 && tp.back() == '/') tp.pop_back();
        if (tp.empty() || tp == "/") tp = "/";
        else if (tp[0] != '/') tp = "/" + tp;
        uploadPath = tp;
    }

    if (uploadPath.empty() || uploadPath == "/" || _stricmp(uploadPath.c_str(), "/") == 0)
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_CANNOT_UPLOAD_ROOT), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        if (cancelOrHandlePath) *cancelOrHandlePath = TRUE;
        if (parent) { SetForegroundWindow(parent); SetFocus(parent); }
        return FALSE;
    }

    if (_stricmp(uploadPath.c_str(), "/Shared with me") == 0)
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_CANNOT_CREATE_DIR_SHARED_WITH_ME), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        if (cancelOrHandlePath) *cancelOrHandlePath = TRUE;
        if (parent) { SetForegroundWindow(parent); SetFocus(parent); }
        return FALSE;
    }

    if (_stricmp(uploadPath.c_str(), "/Shared Drives") == 0)
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_CANNOT_CREATE_DIR_SHARED_DRIVES), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        if (cancelOrHandlePath) *cancelOrHandlePath = TRUE;
        if (parent) { SetForegroundWindow(parent); SetFocus(parent); }
        return FALSE;
    }

    std::string targetFolderId;
    std::string targetDriveId;
    bool targetIsShared = false;
    if (!ResolveFolderIdForPath(uploadPath, targetFolderId, targetDriveId, targetIsShared) || targetFolderId.empty())
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_PATH_NOT_FOUND), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
        if (cancelOrHandlePath) *cancelOrHandlePath = TRUE;
        if (parent) { SetForegroundWindow(parent); SetFocus(parent); }
        return FALSE;
    }

    std::wstring wSourcePath = GDriveHttp::HttpClient::AnsiToWide(sourcePath ? sourcePath : "");
    if (!wSourcePath.empty() && wSourcePath.back() != L'\\')
        wSourcePath += L"\\";

    const char* itemName = NULL;
    int isDir = 0;
    CQuadWord fileSize;
    DWORD attr = 0;
    FILETIME lastWriteTime = {0, 0};
    int enumError = 0;

    // Pass 1: Pre-calculate total files and total bytes from local filesystem
    int totalItems = 0;
    int64_t totalBatchBytes = 0;

    while (next(parent, 0, &itemName, &isDir, &fileSize, &attr, &lastWriteTime, nextParam, &enumError) != NULL)
    {
        if (!itemName || !*itemName) continue;
        if (strcmp(itemName, ".") == 0 || strcmp(itemName, "..") == 0)
            continue;

        std::wstring itemLocalPath = wSourcePath + GDriveHttp::HttpClient::AnsiToWide(itemName);
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(itemLocalPath.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            itemLocalPath = wSourcePath + fd.cFileName;
            FindClose(hFind);
        }

        PrecalculateLocalItems(itemLocalPath, isDir != 0, totalItems, totalBatchBytes);
    }

    // Reset enumerator to beginning for pass 2
    next(parent, -1, NULL, NULL, NULL, NULL, NULL, nextParam, NULL);

    BOOL overallSuccess = TRUE;

    CTransferProgressDialog progressDlg(parent, true, "", 0);
    progressDlg.SetTotalBatch(totalItems > 0 ? totalItems : 1, totalBatchBytes);
    bool progressStarted = false;

    // Pass 2: Perform the actual upload
    while (next(parent, 0, &itemName, &isDir, &fileSize, &attr, &lastWriteTime, nextParam, &enumError) != NULL)
    {
        if (!itemName || !*itemName) continue;
        if (strcmp(itemName, ".") == 0 || strcmp(itemName, "..") == 0)
            continue;

        if (progressDlg.IsCancelled())
        {
            overallSuccess = FALSE;
            break;
        }

        std::wstring itemLocalPath = wSourcePath + GDriveHttp::HttpClient::AnsiToWide(itemName);
        
        // Resolve long filename from disk in case itemName is DOS 8.3 short name
        std::string fileName;
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(itemLocalPath.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            fileName = GDriveHttp::HttpClient::WideToUtf8(fd.cFileName);
            itemLocalPath = wSourcePath + fd.cFileName;
            FindClose(hFind);
        }
        else
        {
            fileName = GDriveHttp::HttpClient::AnsiToUtf8(itemName);
        }

        if (!progressStarted)
        {
            progressDlg.Start();
            progressStarted = true;
        }

        if (isDir)
        {
            if (!UploadFolderRecursive(itemLocalPath, fileName, targetFolderId, parent, &progressDlg))
            {
                overallSuccess = FALSE;
                break;
            }
        }
        else
        {
            if (!UploadSingleItem(itemLocalPath, fileName, targetFolderId, parent, &progressDlg))
            {
                overallSuccess = FALSE;
                break;
            }
        }

        if (!copy && overallSuccess)
        {
            // If Move operation, delete source file or directory after successful upload
            if (isDir)
            {
                SHFILEOPSTRUCTW fileOp = {0};
                fileOp.wFunc = FO_DELETE;
                std::wstring doubleNullTerm = itemLocalPath;
                doubleNullTerm.push_back(L'\0');
                fileOp.pFrom = doubleNullTerm.c_str();
                fileOp.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
                SHFileOperationW(&fileOp);
            }
            else
            {
                DeleteFileW(itemLocalPath.c_str());
            }
        }
    }

    if (progressStarted)
    {
        progressDlg.Stop();
    }

    SalamanderGeneral->RefreshPanelPath(PANEL_SOURCE);
    SalamanderGeneral->RefreshPanelPath(PANEL_TARGET);

    if (parent)
    {
        SetForegroundWindow(parent);
        SetFocus(parent);
    }

    if (!overallSuccess)
    {
        if (cancelOrHandlePath) *cancelOrHandlePath = TRUE;
    }

    return TRUE;
}

bool CPluginFS::DownloadSingleItem(const GDriveApi::GDriveItem& item, const std::wstring& targetDir, HWND parent, CTransferProgressDialog* pProgressDlg)
{
    std::string fileName = item.name;
    if (item.isGoogleDoc && !item.exportExtension.empty())
    {
        if (fileName.length() < item.exportExtension.length() ||
            fileName.compare(fileName.length() - item.exportExtension.length(), item.exportExtension.length(), item.exportExtension) != 0)
        {
            fileName += item.exportExtension;
        }
    }

    std::wstring wFileName = GDriveHttp::HttpClient::Utf8ToWide(fileName);
    if (CfgSanitizeInvalidChars)
    {
        wFileName = GDriveHttp::HttpClient::SanitizeFileNameForLocalFsW(wFileName, (wchar_t)CfgSanitizeChar);
    }

    std::wstring localPath = targetDir;
    if (!localPath.empty() && localPath.back() != L'\\')
        localPath += L"\\";
    localPath += wFileName;

    // Check if local target file already exists
    DWORD dwAttr = GetFileAttributesW(localPath.c_str());
    if (dwAttr != INVALID_FILE_ATTRIBUTES && !(dwAttr & FILE_ATTRIBUTE_DIRECTORY))
    {
        ConflictAction act;
        if (m_batchConflictAction.has_value())
        {
            act = *m_batchConflictAction;
        }
        else
        {
            int64_t localSize = 0;
            HANDLE hLocal = CreateFileW(localPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            if (hLocal != INVALID_HANDLE_VALUE)
            {
                LARGE_INTEGER li;
                if (GetFileSizeEx(hLocal, &li)) localSize = li.QuadPart;
                CloseHandle(hLocal);
            }

            std::string localDisplayName = GDriveHttp::HttpClient::WideToUtf8(wFileName);
            COverwriteConflictDialog dlg(parent, false, fileName, item.size, localDisplayName, localSize);
            dlg.Execute();
            act = dlg.GetAction();
            if (dlg.IsApplyToAll())
            {
                m_batchConflictAction = act;
            }
        }

        if (act == ConflictAction::Cancel)
        {
            if (pProgressDlg) pProgressDlg->Cancel();
            return false;
        }
        if (act == ConflictAction::Skip)
        {
            return true; // Skip file
        }
        if (act == ConflictAction::KeepBoth)
        {
            localPath = MakeUniqueLocalPath(targetDir, wFileName);
        }
        // If ConflictAction::Overwrite -> proceed with original localPath
    }

    std::unique_ptr<CTransferProgressDialog> localProgress;
    CTransferProgressDialog* activeDlg = pProgressDlg;
    if (!activeDlg)
    {
        localProgress = std::make_unique<CTransferProgressDialog>(parent, false, fileName, item.size);
        localProgress->Start();
        activeDlg = localProgress.get();
    }
    else
    {
        activeDlg->SetCurrentFile(fileName, item.size);
    }

    GDriveHttp::ProgressCallback progressCb = [activeDlg](int64_t bytesTransferred, int64_t totalBytes) -> bool {
        if (activeDlg)
        {
            return activeDlg->OnProgress(bytesTransferred, totalBytes);
        }
        return true;
    };

    bool cancelFlag = false;
    std::string err;
    bool success = GDriveApi::ApiClient::GetInstance().DownloadFile(item, localPath, progressCb, &cancelFlag, &err);

    if (localProgress)
    {
        localProgress->Stop();
    }
    else if (success && activeDlg)
    {
        activeDlg->OnFileCompleted(item.size);
    }

    if (!success && !err.empty() && (!activeDlg || !activeDlg->IsCancelled()))
    {
        SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
    }
    return success;
}

bool CPluginFS::DownloadFolderRecursive(const GDriveApi::GDriveItem& folder, const std::wstring& targetDir, HWND parent, CTransferProgressDialog* pProgressDlg, const std::set<std::string>* pPrecalculatedFolderIds)
{
    std::wstring wFolderName = GDriveHttp::HttpClient::Utf8ToWide(folder.name);
    if (CfgSanitizeInvalidChars)
    {
        wFolderName = GDriveHttp::HttpClient::SanitizeFileNameForLocalFsW(wFolderName, (wchar_t)CfgSanitizeChar);
    }

    std::wstring subDir = targetDir;
    if (!subDir.empty() && subDir.back() != L'\\')
        subDir += L"\\";
    subDir += wFolderName;

    CreateDirectoryW(subDir.c_str(), NULL);

    std::vector<GDriveApi::GDriveItem> children;
    std::string err;
    if (!GDriveApi::ApiClient::GetInstance().ListFolder(folder.id, folder.driveId, folder.isSharedDrive, children, &err))
    {
        return false;
    }

    bool isPrecalculated = (pPrecalculatedFolderIds && pPrecalculatedFolderIds->find(folder.id) != pPrecalculatedFolderIds->end());
    if (pProgressDlg && !isPrecalculated)
    {
        int newItemsCount = 0;
        int64_t newBytes = 0;
        for (const auto& child : children)
        {
            newItemsCount++;
            if (!child.isFolder)
                newBytes += child.size;
        }
        // Replace this 1 folder slot with count of items inside it
        pProgressDlg->AddBatchItems(newItemsCount > 0 ? (newItemsCount - 1) : 0, newBytes);
    }

    for (const auto& child : children)
    {
        if (pProgressDlg && pProgressDlg->IsCancelled())
            return false;

        if (child.isFolder)
        {
            if (!DownloadFolderRecursive(child, subDir, parent, pProgressDlg, pPrecalculatedFolderIds))
                return false;
        }
        else
        {
            if (!DownloadSingleItem(child, subDir, parent, pProgressDlg))
                return false;
        }
    }
    return true;
}

bool CPluginFS::UploadSingleItem(const std::wstring& localPath, const std::string& fileName, const std::string& parentFolderId, HWND parent, CTransferProgressDialog* pProgressDlg)
{
    LARGE_INTEGER localFileSize = {0};
    HANDLE hF = CreateFileW(localPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hF != INVALID_HANDLE_VALUE)
    {
        GetFileSizeEx(hF, &localFileSize);
        CloseHandle(hF);
    }

    // Check if items with this name already exist in target GDrive folder
    std::vector<GDriveApi::GDriveItem> existingItems;
    if (parentFolderId == m_currentFolderId && !m_cachedItems.empty())
    {
        existingItems = m_cachedItems;
    }
    else if (!GDriveCache::CacheManager::GetInstance().GetFolder(parentFolderId, existingItems))
    {
        std::string listErr;
        if (GDriveApi::ApiClient::GetInstance().ListFolder(parentFolderId, "", false, existingItems, &listErr))
        {
            GDriveCache::CacheManager::GetInstance().PutFolder(parentFolderId, existingItems);
        }
    }

    std::vector<const GDriveApi::GDriveItem*> matchingItems;
    std::string ansiFileName = GDriveHttp::HttpClient::Utf8ToAnsi(fileName);
    for (const auto& it : existingItems)
    {
        if (!it.isFolder && (_stricmp(it.name.c_str(), fileName.c_str()) == 0 ||
                             _stricmp(it.name.c_str(), ansiFileName.c_str()) == 0))
        {
            matchingItems.push_back(&it);
        }
    }

    if (!matchingItems.empty())
    {
        ConflictAction act;
        OverwriteScope scope = OverwriteScope::All;
        if (m_batchConflictAction.has_value())
        {
            act = *m_batchConflictAction;
        }
        else
        {
            const GDriveApi::GDriveItem* refItem = matchingItems[0];
            COverwriteConflictDialog dlg(parent, true, fileName, localFileSize.QuadPart,
                                         refItem->name, refItem->size, (int)matchingItems.size(), false);
            dlg.Execute();
            act = dlg.GetAction();
            scope = dlg.GetOverwriteScope();
            if (dlg.IsApplyToAll())
            {
                m_batchConflictAction = act;
            }
        }

        if (act == ConflictAction::Cancel)
        {
            if (pProgressDlg) pProgressDlg->Cancel();
            return false;
        }
        if (act == ConflictAction::Skip)
        {
            return true; // Skip file
        }
        if (act == ConflictAction::Overwrite)
        {
            std::vector<std::string> idsToTrash;
            if (matchingItems.size() == 1 || scope == OverwriteScope::All)
            {
                for (const auto* item : matchingItems)
                {
                    idsToTrash.push_back(item->id);
                }
            }
            else
            {
                auto sortedItems = matchingItems;
                std::sort(sortedItems.begin(), sortedItems.end(), [](const GDriveApi::GDriveItem* a, const GDriveApi::GDriveItem* b) {
                    return CompareFileTime(&a->modifiedTime, &b->modifiedTime) < 0;
                });
                if (scope == OverwriteScope::Newest)
                {
                    idsToTrash.push_back(sortedItems.back()->id);
                }
                else if (scope == OverwriteScope::Oldest)
                {
                    idsToTrash.push_back(sortedItems.front()->id);
                }
            }

            for (const auto& id : idsToTrash)
            {
                std::string trashErr;
                GDriveApi::ApiClient::GetInstance().TrashItem(id, &trashErr);
                GDriveCache::CacheManager::GetInstance().RemoveItem(parentFolderId, id);
                if (parentFolderId == m_currentFolderId)
                {
                    for (auto it = m_cachedItems.begin(); it != m_cachedItems.end(); ++it)
                    {
                        if (it->id == id)
                        {
                            m_cachedItems.erase(it);
                            break;
                        }
                    }
                }
            }
        }
        // If ConflictAction::KeepBoth -> proceed to upload new file without trashing existing item
    }

    std::unique_ptr<CTransferProgressDialog> localProgress;
    CTransferProgressDialog* activeDlg = pProgressDlg;
    if (!activeDlg)
    {
        localProgress = std::make_unique<CTransferProgressDialog>(parent, true, fileName, localFileSize.QuadPart);
        localProgress->Start();
        activeDlg = localProgress.get();
    }
    else
    {
        activeDlg->SetCurrentFile(fileName, localFileSize.QuadPart);
    }

    GDriveHttp::ProgressCallback progressCb = [activeDlg](int64_t bytesTransferred, int64_t totalBytes) -> bool {
        if (activeDlg)
        {
            return activeDlg->OnProgress(bytesTransferred, totalBytes);
        }
        return true;
    };

    bool cancelFlag = false;
    GDriveApi::GDriveItem newItem;
    std::string err;
    bool success = GDriveApi::ApiClient::GetInstance().UploadFile(parentFolderId, localPath, fileName, "", progressCb, &cancelFlag, newItem, &err);

    if (localProgress)
    {
        localProgress->Stop();
    }
    else if (success && activeDlg)
    {
        activeDlg->OnFileCompleted(localFileSize.QuadPart);
    }

    if (!success)
    {
        if (!err.empty() && (!activeDlg || !activeDlg->IsCancelled()))
        {
            SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
        }
        return false;
    }

    std::string subPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + fileName;
    m_pathToIdCache[subPath] = newItem.id;

    std::string ansiName = GDriveHttp::HttpClient::Utf8ToAnsi(fileName);
    std::string ansiSubPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + ansiName;
    m_pathToIdCache[ansiSubPath] = newItem.id;

    if (parentFolderId == m_currentFolderId)
    {
        m_cachedItems.push_back(newItem);
    }

    GDriveCache::CacheManager::GetInstance().AddOrUpdateItem(parentFolderId, newItem);

    return true;
}

bool CPluginFS::UploadFolderRecursive(const std::wstring& localDirPath, const std::string& dirName, const std::string& parentFolderId, HWND parent, CTransferProgressDialog* pProgressDlg)
{
    if (pProgressDlg && pProgressDlg->IsCancelled())
        return false;

    // Check if folder(s) with this name already exist under parentFolderId
    std::vector<GDriveApi::GDriveItem> existingItems;
    if (parentFolderId == m_currentFolderId && !m_cachedItems.empty())
    {
        existingItems = m_cachedItems;
    }
    else if (!GDriveCache::CacheManager::GetInstance().GetFolder(parentFolderId, existingItems))
    {
        std::string listErr;
        if (GDriveApi::ApiClient::GetInstance().ListFolder(parentFolderId, "", false, existingItems, &listErr))
        {
            GDriveCache::CacheManager::GetInstance().PutFolder(parentFolderId, existingItems);
        }
    }

    std::vector<const GDriveApi::GDriveItem*> matchingFolders;
    std::string ansiDirName = GDriveHttp::HttpClient::Utf8ToAnsi(dirName);
    for (const auto& it : existingItems)
    {
        if (it.isFolder && (_stricmp(it.name.c_str(), dirName.c_str()) == 0 ||
                            _stricmp(it.name.c_str(), ansiDirName.c_str()) == 0))
        {
            matchingFolders.push_back(&it);
        }
    }

    std::string activeFolderId;

    if (!matchingFolders.empty())
    {
        ConflictAction act;
        OverwriteScope scope = OverwriteScope::All;
        if (m_batchConflictAction.has_value())
        {
            act = *m_batchConflictAction;
        }
        else
        {
            const GDriveApi::GDriveItem* refItem = matchingFolders[0];
            COverwriteConflictDialog dlg(parent, true, dirName, -1,
                                         refItem->name, -1, (int)matchingFolders.size(), true);
            dlg.Execute();
            act = dlg.GetAction();
            scope = dlg.GetOverwriteScope();
            if (dlg.IsApplyToAll())
            {
                m_batchConflictAction = act;
            }
        }

        if (act == ConflictAction::Cancel)
        {
            if (pProgressDlg) pProgressDlg->Cancel();
            return false;
        }
        if (act == ConflictAction::Skip)
        {
            return true; // Skip this folder and all its contents
        }
        if (act == ConflictAction::Overwrite)
        {
            // Overwrite/Merge: use existing folder, and if duplicate folders exist, trash extra duplicates according to scope
            const GDriveApi::GDriveItem* chosenFolder = matchingFolders[0];
            if (matchingFolders.size() > 1)
            {
                auto sortedFolders = matchingFolders;
                std::sort(sortedFolders.begin(), sortedFolders.end(), [](const GDriveApi::GDriveItem* a, const GDriveApi::GDriveItem* b) {
                    return CompareFileTime(&a->modifiedTime, &b->modifiedTime) < 0;
                });

                if (scope == OverwriteScope::Newest)
                {
                    chosenFolder = sortedFolders.back();
                }
                else if (scope == OverwriteScope::Oldest)
                {
                    chosenFolder = sortedFolders.front();
                }

                for (const auto* fld : matchingFolders)
                {
                    if (fld->id != chosenFolder->id)
                    {
                        std::string trashErr;
                        GDriveApi::ApiClient::GetInstance().TrashItem(fld->id, &trashErr);
                        GDriveCache::CacheManager::GetInstance().RemoveItem(parentFolderId, fld->id);
                        if (parentFolderId == m_currentFolderId)
                        {
                            for (auto it = m_cachedItems.begin(); it != m_cachedItems.end(); ++it)
                            {
                                if (it->id == fld->id)
                                {
                                    m_cachedItems.erase(it);
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            activeFolderId = chosenFolder->id;
        }
        // If ConflictAction::KeepBoth -> proceed to create a new folder
    }

    if (activeFolderId.empty())
    {
        GDriveApi::GDriveItem newFolder;
        std::string err;
        if (!GDriveApi::ApiClient::GetInstance().CreateFolder(parentFolderId, dirName, newFolder, &err))
        {
            if (!err.empty() && (!pProgressDlg || !pProgressDlg->IsCancelled()))
            {
                SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            }
            return false;
        }

        activeFolderId = newFolder.id;
        GDriveCache::CacheManager::GetInstance().AddOrUpdateItem(parentFolderId, newFolder);
        if (parentFolderId == m_currentFolderId)
        {
            m_cachedItems.push_back(newFolder);
        }
    }

    std::string folderSubPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + dirName;
    m_pathToIdCache[folderSubPath] = activeFolderId;
    std::string ansiFolderSubPath = (m_currentPath == "/" ? "" : m_currentPath) + "/" + ansiDirName;
    m_pathToIdCache[ansiFolderSubPath] = activeFolderId;

    std::wstring searchPattern = localDirPath;
    if (!searchPattern.empty() && searchPattern.back() != L'\\')
        searchPattern += L"\\";
    searchPattern += L"*";

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (pProgressDlg && pProgressDlg->IsCancelled())
            {
                FindClose(hFind);
                return false;
            }

            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;

            std::wstring itemLocalPath = localDirPath;
            if (!itemLocalPath.empty() && itemLocalPath.back() != L'\\')
                itemLocalPath += L"\\";
            itemLocalPath += fd.cFileName;

            std::string itemUtf8Name = GDriveHttp::HttpClient::WideToUtf8(fd.cFileName);

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (!UploadFolderRecursive(itemLocalPath, itemUtf8Name, activeFolderId, parent, pProgressDlg))
                {
                    FindClose(hFind);
                    return false;
                }
            }
            else
            {
                if (!UploadSingleItem(itemLocalPath, itemUtf8Name, activeFolderId, parent, pProgressDlg))
                {
                    FindClose(hFind);
                    return false;
                }
            }
        } while (FindNextFileW(hFind, &fd));

        FindClose(hFind);
    }

    return true;
}

BOOL WINAPI CPluginFS::CopyOrMoveFromFS(BOOL copy, int mode, const char* fsName, HWND parent,
                                       int panel, int selectedFiles, int selectedDirs,
                                       char* targetPath, BOOL& operationMask,
                                       BOOL& cancelOrHandlePath, HWND dropTarget)
{
    operationMask = FALSE;
    cancelOrHandlePath = FALSE;

    if (mode == 1)
    {
        // Pre-fill target path from opposite panel if not already set
        if (targetPath && targetPath[0] == 0)
        {
            int otherPanel = (panel == PANEL_SOURCE) ? PANEL_TARGET : PANEL_SOURCE;
            SalamanderGeneral->GetPanelPath(otherPanel, targetPath, MAX_PATH, NULL, NULL, 0);
        }
        // Ask Salamander to show standard destination dialog
        return FALSE;
    }
    if (mode == 4)
    {
        return FALSE;
    }

    m_batchConflictAction.reset();

    std::string tpStr = targetPath ? targetPath : "";
    std::string prefix = std::string(fsName && *fsName ? fsName : AssignedFSName) + ":";
    bool isTargetGDrive = (_strnicmp(tpStr.c_str(), prefix.c_str(), prefix.length()) == 0 ||
                           _strnicmp(tpStr.c_str(), "gdrive:", 7) == 0);

    if (isTargetGDrive)
    {
        size_t colon = tpStr.find(':');
        if (colon != std::string::npos) tpStr = tpStr.substr(colon + 1);
        std::replace(tpStr.begin(), tpStr.end(), '\\', '/');
        while (tpStr.size() > 1 && tpStr.back() == '/') tpStr.pop_back();
        if (tpStr.empty() || tpStr == "/") tpStr = "/";
        else if (tpStr[0] != '/') tpStr = "/" + tpStr;

        std::string targetFolderId;
        std::string targetDriveId;
        bool targetIsShared = false;
        if (!ResolveFolderIdForPath(tpStr, targetFolderId, targetDriveId, targetIsShared) || targetFolderId.empty())
        {
            SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_CANNOT_UPLOAD_ROOT), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
            cancelOrHandlePath = TRUE;
            if (parent) { SetForegroundWindow(parent); SetFocus(parent); }
            return FALSE;
        }

        BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
        int index = 0;
        BOOL isDir = FALSE;
        BOOL overallSuccess = TRUE;

        CTransferProgressDialog progressDlg(parent, false, "", 0);
        progressDlg.SetActionLabel(copy ? IDS_TRANSFER_UPLOADING : IDS_TRANSFER_DELETING);
        bool progressStarted = false;

        while (true)
        {
            const CFileData* f = focused ? SalamanderGeneral->GetPanelFocusedItem(panel, &isDir)
                                         : SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);
            if (f == NULL) break;

            if (progressDlg.IsCancelled())
            {
                overallSuccess = FALSE;
                break;
            }

            const GDriveApi::GDriveItem* targetItem = FindItemByPanelName(f->Name);
            if (targetItem)
            {
                if (!progressStarted)
                {
                    progressDlg.Start();
                    progressStarted = true;
                }
                progressDlg.SetCurrentFile(f->Name, targetItem->size);

                std::string err;
                if (!copy)
                {
                    if (targetItem->id != targetFolderId)
                    {
                        bool ok = GDriveApi::ApiClient::GetInstance().MoveItem(targetItem->id, m_currentFolderId, targetFolderId, &err);
                        if (!ok)
                        {
                            SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
                            overallSuccess = FALSE;
                            break;
                        }
                        GDriveCache::CacheManager::GetInstance().RemoveItem(m_currentFolderId, targetItem->id);
                        GDriveCache::CacheManager::GetInstance().AddOrUpdateItem(targetFolderId, *targetItem);
                    }
                }
                else
                {
                    if (!targetItem->isFolder)
                    {
                        GDriveApi::GDriveItem copyResult;
                        bool ok = GDriveApi::ApiClient::GetInstance().CopyFile(targetItem->id, targetFolderId, targetItem->name, copyResult, &err);
                        if (!ok)
                        {
                            SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
                            overallSuccess = FALSE;
                            break;
                        }
                        GDriveCache::CacheManager::GetInstance().AddOrUpdateItem(targetFolderId, copyResult);
                    }
                }
            }

            if (focused) break;
        }

        if (progressStarted)
        {
            progressDlg.Stop();
        }

        SalamanderGeneral->RefreshPanelPath(panel);
        SalamanderGeneral->RefreshPanelPath((panel == PANEL_SOURCE) ? PANEL_TARGET : PANEL_SOURCE);

        if (parent)
        {
            SetForegroundWindow(parent);
            SetFocus(parent);
        }

        if (overallSuccess)
        {
            targetPath[0] = 0;
        }
        else
        {
            cancelOrHandlePath = TRUE;
        }
        return overallSuccess;
    }

    // Clean target directory path
    char target[2 * MAX_PATH] = {0};
    lstrcpyn(target, targetPath, 2 * MAX_PATH);
    char* lastBs = strrchr(target, '\\');
    char* comp = (lastBs != NULL) ? lastBs + 1 : target;
    if (strchr(comp, '*') != NULL || strchr(comp, '?') != NULL)
    {
        if (lastBs != NULL)
            *lastBs = 0;
        else
            target[0] = 0;
    }

    std::wstring wTargetDir = GDriveHttp::HttpClient::AnsiToWide(target);

    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
    int index = 0;
    BOOL isDir = FALSE;
    BOOL success = TRUE;

    std::vector<std::pair<std::string, std::string>> itemsToTrash; // name, id for Move operations

    int64_t totalBatchBytes = 0;
    int totalItems = 0;
    std::set<std::string> precalculatedFolderIds;

    auto inspectItemForBatch = [&](const CFileData* f) {
        if (!f) return;
        const GDriveApi::GDriveItem* item = FindItemByPanelName(f->Name);
        if (!item) return;
        if (!item->isFolder)
        {
            totalItems++;
            totalBatchBytes += item->size;
        }
        else
        {
            int64_t folderSize = 0;
            int filesCount = 0;
            int dirsCount = 0;
            if (GDriveCache::CacheManager::GetInstance().ComputeFolderSizeFromCache(item->id, folderSize, filesCount, dirsCount, &precalculatedFolderIds))
            {
                totalItems += filesCount;
                totalBatchBytes += folderSize;
            }
            else
            {
                totalItems++;
            }
        }
    };

    if (focused)
    {
        const CFileData* f = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
        inspectItemForBatch(f);
    }
    else
    {
        int tempIdx = 0;
        BOOL tempIsDir = FALSE;
        while (const CFileData* f = SalamanderGeneral->GetPanelSelectedItem(panel, &tempIdx, &tempIsDir))
        {
            inspectItemForBatch(f);
        }
    }

    CTransferProgressDialog progressDlg(parent, false, "", 0);
    progressDlg.SetTotalBatch(totalItems > 0 ? totalItems : 1, totalBatchBytes);
    bool progressStarted = false;

    while (true)
    {
        const CFileData* f = focused ? SalamanderGeneral->GetPanelFocusedItem(panel, &isDir)
                                     : SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);
        if (f == NULL)
            break;

        if (progressDlg.IsCancelled())
        {
            success = FALSE;
            break;
        }

        const GDriveApi::GDriveItem* targetItem = FindItemByPanelName(f->Name);

        if (targetItem)
        {
            if (!progressStarted)
            {
                progressDlg.Start();
                progressStarted = true;
            }

            if (targetItem->isFolder)
            {
                if (!DownloadFolderRecursive(*targetItem, wTargetDir, parent, &progressDlg, &precalculatedFolderIds))
                {
                    success = FALSE;
                    break;
                }
                else
                {
                    itemsToTrash.emplace_back(f->Name, targetItem->id);
                }
            }
            else
            {
                if (!DownloadSingleItem(*targetItem, wTargetDir, parent, &progressDlg))
                {
                    success = FALSE;
                    break;
                }
                else
                {
                    itemsToTrash.emplace_back(f->Name, targetItem->id);
                }
            }
        }

        if (focused)
            break;
    }

    if (progressStarted)
    {
        progressDlg.Stop();
    }

    // If Move operation and transfer succeeded, delete source items from Google Drive
    if (!copy && success && !itemsToTrash.empty())
    {
        for (const auto& [name, id] : itemsToTrash)
        {
            std::string trashErr;
            GDriveApi::ApiClient::GetInstance().TrashItem(id, &trashErr);

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

        SalamanderGeneral->RefreshPanelPath(panel);
    }

    if (parent)
    {
        SetForegroundWindow(parent);
        SetFocus(parent);
    }

    if (success)
    {
        targetPath[0] = 0;
        SalamanderGeneral->RefreshPanelPath((panel == PANEL_SOURCE) ? PANEL_TARGET : PANEL_SOURCE);
    }
    else
    {
        cancelOrHandlePath = TRUE;
    }

    return TRUE;
}
