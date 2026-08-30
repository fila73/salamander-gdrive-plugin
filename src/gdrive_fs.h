// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "gdrive_api.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <optional>

#define TOP_INDEX_MEM_SIZE 30

inline constexpr const char* kMyDriveDir = "My Drive";
inline constexpr const char* kSharedDrivesDir = "Shared Drives";
inline constexpr const char* kSharedWithMeDir = "Shared with me";
inline constexpr const char* kStarredDir = "Starred";
inline constexpr const char* kRecentDir = "Recent";
inline constexpr const char* kTrashDir = "Trash";

struct CTopIndexMem
{
    char Path[MAX_PATH];
    int TopIndexes[TOP_INDEX_MEM_SIZE];
    int TopIndexesCount;

    CTopIndexMem()
    {
        Path[0] = 0;
        TopIndexesCount = 0;
    }

    void Push(const char* path, int topIndex)
    {
        const char* s = path + strlen(path);
        if (s > path && *(s - 1) == '\\')
            s--;
        BOOL ok;
        if (s == path)
            ok = FALSE;
        else
        {
            if (s > path && *s == '\\')
                s--;
            while (s > path && *s != '\\')
                s--;

            int l = (int)strlen(Path);
            if (l > 0 && Path[l - 1] == '\\')
                l--;
            ok = (s - path == l) && (_strnicmp(path, Path, l) == 0);
        }

        if (ok)
        {
            if (TopIndexesCount == TOP_INDEX_MEM_SIZE)
            {
                for (int i = 0; i < TOP_INDEX_MEM_SIZE - 1; i++)
                    TopIndexes[i] = TopIndexes[i + 1];
                TopIndexesCount--;
            }
            lstrcpynA(Path, path, MAX_PATH);
            TopIndexes[TopIndexesCount++] = topIndex;
        }
        else
        {
            lstrcpynA(Path, path, MAX_PATH);
            TopIndexesCount = 1;
            TopIndexes[0] = topIndex;
        }
    }

    BOOL FindAndPop(const char* path, int& topIndex)
    {
        if (TopIndexesCount > 0 && _stricmp(Path, path) == 0)
        {
            topIndex = TopIndexes[--TopIndexesCount];
            if (TopIndexesCount > 0)
            {
                char* s = Path + strlen(Path);
                if (s > Path && *(s - 1) == '\\')
                    s--;
                while (s > Path && *s != '\\')
                    s--;
                if (s > Path && *s == '\\')
                    *s = 0;
                else
                    Path[0] = 0;
            }
            else
                Path[0] = 0;
            return TRUE;
        }
        return FALSE;
    }
};

class CPluginFS : public CPluginFSInterfaceAbstract
{
public:
    CTopIndexMem TopIndexMem;

    CPluginFS(const char* fsName);
    virtual ~CPluginFS();

    virtual BOOL WINAPI GetCurrentPath(char* userPart) override;
    virtual BOOL WINAPI GetFullName(CFileData& file, int isDir, char* buf, int bufSize) override;
    virtual BOOL WINAPI GetFullFSPath(HWND parent, const char* fsName, char* path, int pathSize, BOOL& success) override;
    virtual BOOL WINAPI GetRootPath(char* userPart) override;

    virtual BOOL WINAPI IsCurrentPath(int currentFSNameIndex, int fsNameIndex, const char* userPart) override;
    virtual BOOL WINAPI IsOurPath(int currentFSNameIndex, int fsNameIndex, const char* userPart) override;

    virtual BOOL WINAPI ChangePath(int currentFSNameIndex, char* fsName, int fsNameIndex,
                                   const char* userPart, char* cutFileName, BOOL* pathWasCut,
                                   BOOL forceRefresh, int mode) override;
    virtual BOOL WINAPI ListCurrentPath(CSalamanderDirectoryAbstract* dir,
                                        CPluginDataInterfaceAbstract*& pluginData,
                                        int& iconsType, BOOL forceRefresh) override;
    virtual BOOL WINAPI TryCloseOrDetach(BOOL forceClose, BOOL canDetach, BOOL& detach, int reason) override;
    virtual void WINAPI Event(int event, DWORD param) override;
    virtual void WINAPI ReleaseObject(HWND parent) override;

    virtual DWORD WINAPI GetSupportedServices() override;

    virtual BOOL WINAPI GetChangeDriveOrDisconnectItem(const char* fsName, char*& title,
                                                       HICON& icon, BOOL& destroyIcon) override;
    virtual HICON WINAPI GetFSIcon(BOOL& destroyIcon) override;
    virtual void WINAPI GetDropEffect(const char* srcFSPath, const char* tgtFSPath,
                                      DWORD allowedEffects, DWORD keyState,
                                      DWORD* dropEffect) override;
    virtual void WINAPI GetFSFreeSpace(CQuadWord* retValue) override;
    virtual BOOL WINAPI GetNextDirectoryLineHotPath(const char* text, int pathLen, int& offset) override;
    virtual void WINAPI CompleteDirectoryLineHotPath(char* path, int pathBufSize) override;
    virtual BOOL WINAPI GetPathForMainWindowTitle(const char* fsName, int mode, char* buf, int bufSize) override;
    virtual void WINAPI ShowInfoDialog(const char* fsName, HWND parent) override;
    virtual BOOL WINAPI ExecuteCommandLine(HWND parent, char* command, int& selFrom, int& selTo) override;
    virtual BOOL WINAPI QuickRename(const char* fsName, int mode, HWND parent, CFileData& file,
                                    BOOL isDir, char* newName, BOOL& cancel) override;
    virtual void WINAPI AcceptChangeOnPathNotification(const char* fsName, const char* path,
                                                       BOOL includingSubdirs) override;
    virtual BOOL WINAPI CreateDir(const char* fsName, int mode, HWND parent,
                                  char* newName, BOOL& cancel) override;
    virtual void WINAPI ViewFile(const char* fsName, HWND parent,
                                 CSalamanderForViewFileOnFSAbstract* salamander,
                                 CFileData& file) override;
    virtual BOOL WINAPI Delete(const char* fsName, int mode, HWND parent, int panel,
                               int selectedFiles, int selectedDirs, BOOL& cancelOrError) override;
    virtual BOOL WINAPI CopyOrMoveFromFS(BOOL copy, int mode, const char* fsName, HWND parent,
                                         int panel, int selectedFiles, int selectedDirs,
                                         char* targetPath, BOOL& operationMask,
                                         BOOL& cancelOrHandlePath, HWND dropTarget) override;
    virtual BOOL WINAPI CopyOrMoveFromDiskToFS(BOOL copy, int mode, const char* fsName, HWND parent,
                                               const char* sourcePath, SalEnumSelection2 next,
                                               void* nextParam, int selectedFiles, int selectedDirs,
                                               char* targetPath, BOOL* cancelOrHandlePath) override;
    virtual BOOL WINAPI ChangeAttributes(const char* fsName, HWND parent, int panel,
                                         int selectedFiles, int selectedDirs) override;
    virtual void WINAPI ShowProperties(const char* fsName, HWND parent, int panel,
                                       int selectedFiles, int selectedDirs) override;

    virtual void WINAPI ContextMenu(const char* fsName, HWND parent, int menuX, int menuY, int type,
                                    int panel, int selectedFiles, int selectedDirs) override;
    virtual BOOL WINAPI HandleMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* plResult) override;
    virtual BOOL WINAPI OpenFindDialog(const char* fsName, int panel) override;
    virtual void WINAPI OpenActiveFolder(const char* fsName, HWND parent) override;
    virtual void WINAPI GetAllowedDropEffects(int mode, const char* tgtFSPath, DWORD* allowedEffects) override;
    virtual BOOL WINAPI GetNoItemsInPanelText(char* textBuf, int textBufSize) override;
    virtual void WINAPI ShowSecurityInfo(HWND parent) override;

    void CalculateFolderSize(HWND parent, int panel);
    void OnSpacePressedOnFolder(int panel, const CFileData* f);

    const std::string& GetCurrentPathStr() const { return m_currentPath; }

public:
    struct CaseInsensitiveCompare {
        bool operator()(const std::string& a, const std::string& b) const {
            return _stricmp(a.c_str(), b.c_str()) < 0;
        }
    };

private:
    std::string m_fsName;
    std::string m_currentPath; // e.g. "", "/My Drive", "/My Drive/Folder", "/Shared Drives", "/Shared Drives/TeamA"
    std::string m_currentFolderId; // Google Drive folder ID for m_currentPath
    std::string m_currentDriveId; // Shared drive ID if inside shared drive
    bool m_isSharedDrive = false;

    std::vector<GDriveApi::GDriveItem> m_cachedItems;
    std::map<std::string, std::string, CaseInsensitiveCompare> m_pathToIdCache;
    std::string m_lastErrorPath;

    const GDriveApi::GDriveItem* FindItemByPanelName(const char* panelName) const;
    bool ResolveCurrentFolderId();
    bool ResolveFolderIdForPath(const std::string& path, std::string& folderId, std::string& driveId, bool& isShared);
    bool DownloadSingleItem(const GDriveApi::GDriveItem& item, const std::wstring& targetDir, HWND parent, class CTransferProgressDialog* pProgressDlg = nullptr);
    bool DownloadFolderRecursive(const GDriveApi::GDriveItem& folder, const std::wstring& targetDir, HWND parent, class CTransferProgressDialog* pProgressDlg = nullptr, const std::set<std::string>* pPrecalculatedFolderIds = nullptr);
    bool UploadSingleItem(const std::wstring& localPath, const std::string& fileName, const std::string& parentFolderId, HWND parent, class CTransferProgressDialog* pProgressDlg = nullptr);
    bool UploadFolderRecursive(const std::wstring& localDirPath, const std::string& dirName, const std::string& parentFolderId, HWND parent, class CTransferProgressDialog* pProgressDlg = nullptr);

    std::optional<ConflictAction> m_batchConflictAction;

public:
    static std::string ExtractIdSuffix(const std::string& id);
    static std::string ExtractSuffixFromDisambiguatedName(const std::string& name);
    static std::string GetBaseDisplayName(const GDriveApi::GDriveItem& item);
    static std::map<std::string, std::string> ComputeDisplayNames(const std::vector<GDriveApi::GDriveItem>& items);
};

class CPluginInterfaceForFS : public CPluginInterfaceForFSAbstract
{
public:
    virtual CPluginFSInterfaceAbstract* WINAPI OpenFS(const char* fsName, int fsNameIndex) override;
    virtual void WINAPI CloseFS(CPluginFSInterfaceAbstract* fs) override;

    virtual void WINAPI ExecuteChangeDriveMenuItem(int panel) override;
    virtual BOOL WINAPI ChangeDriveMenuItemContextMenu(HWND parent, int panel, int x, int y,
                                                       CPluginFSInterfaceAbstract* pluginFS,
                                                       const char* pluginFSName, int pluginFSNameIndex,
                                                       BOOL isDetachedFS, BOOL& refreshMenu,
                                                       BOOL& closeMenu, int& postCmd, void*& postCmdParam) override;
    virtual void WINAPI ExecuteChangeDrivePostCommand(int panel, int postCmd, void* postCmdParam) override;
    virtual void WINAPI ExecuteOnFS(int panel, CPluginFSInterfaceAbstract* pluginFS,
                                    const char* pluginFSName, int pluginFSNameIndex,
                                    CFileData& file, int isDir) override;
    virtual BOOL WINAPI DisconnectFS(HWND parent, BOOL isInPanel, int panel,
                                     CPluginFSInterfaceAbstract* pluginFS,
                                     const char* pluginFSName, int pluginFSNameIndex) override;

    virtual void WINAPI ConvertPathToInternal(const char* fsName, int fsNameIndex, char* fsUserPart) override {}
    virtual void WINAPI ConvertPathToExternal(const char* fsName, int fsNameIndex, char* fsUserPart) override {}
    virtual void WINAPI EnsureShareExistsOnServer(int panel, const char* server, const char* share) override {}
};

extern CPluginInterfaceForFS InterfaceForFS;
