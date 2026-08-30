// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

class CCommonDialog : public CDialog
{
public:
    CCommonDialog(HINSTANCE hInstance, int resID, HWND hParent, CObjectOrigin origin = ooStandard);
    CCommonDialog(HINSTANCE hInstance, int resID, int helpID, HWND hParent, CObjectOrigin origin = ooStandard);

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    virtual void NotifDlgJustCreated();
};

class CCommonPropSheetPage : public CPropSheetPage
{
public:
    CCommonPropSheetPage(TCHAR* title, HINSTANCE modul, int resID,
                         DWORD flags = 0, HICON icon = NULL,
                         CObjectOrigin origin = ooStatic)
        : CPropSheetPage(title, modul, resID, flags, icon, origin) {}
    CCommonPropSheetPage(TCHAR* title, HINSTANCE modul, int resID, UINT helpID,
                         DWORD flags = 0, HICON icon = NULL,
                         CObjectOrigin origin = ooStatic)
        : CPropSheetPage(title, modul, resID, helpID, flags, icon, origin) {}

protected:
    virtual void NotifDlgJustCreated();
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

class CConfigPageGeneral : public CCommonPropSheetPage
{
public:
    CConfigPageGeneral();

    virtual void Transfer(CTransferInfo& ti) override;
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    void RefreshAccountsList();
};

class CConfigPageCache : public CCommonPropSheetPage
{
public:
    CConfigPageCache();

    virtual void Transfer(CTransferInfo& ti) override;
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    void UpdateCacheStatus();
};

class CConfigDialog : public CPropertyDialog
{
protected:
    CConfigPageGeneral PageGeneral;
    CConfigPageCache PageCache;

public:
    CConfigDialog(HWND parent);
};

class CCalcSizeProgressDialog : public CCommonDialog
{
public:
    CCalcSizeProgressDialog(HWND hParent, const char* itemName, const std::string& folderId,
                            const std::string& driveId, bool isSharedDrive);

    bool Run();

    int64_t GetTotalBytes() const { return m_totalBytes; }
    int GetTotalFiles() const { return m_totalFiles; }
    int GetTotalDirs() const { return m_totalDirs; }
    bool WasCancelled() const { return m_cancelled; }
    const std::string& GetItemName() const { return m_itemName; }

    static std::string FormatSize(int64_t bytes);
    static std::string FormatNumber(int64_t num);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    std::string m_itemName;
    std::string m_folderId;
    std::string m_driveId;
    bool m_isSharedDrive;

    int64_t m_totalBytes;
    int m_totalFiles;
    int m_totalDirs;
    bool m_cancelled;

    void ProcessMessages();
    void UpdateUI(const std::string& currentScanningFolder);
};

class CTransferProgressDialog : public CCommonDialog
{
public:
    CTransferProgressDialog(HWND hParent, bool isUpload, const std::string& fileName, int64_t totalBytes);
    ~CTransferProgressDialog();

    bool Start();
    void Stop();
    void SetCurrentFile(const std::string& fileName, int64_t totalBytes);
    void SetActionLabel(int strResId);

    bool OnProgress(int64_t bytesTransferred, int64_t totalBytes);
    bool IsCancelled() const { return m_cancelled; }
    void Cancel() { m_cancelled = true; }

    static std::string FormatSize(int64_t bytes);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    bool m_isUpload;
    int m_actionResId;
    std::string m_fileName;
    int64_t m_totalBytes;
    bool m_cancelled;
    DWORD m_startTick;
    DWORD m_lastUpdateTick;

    void ProcessMessages();
    void UpdateUI(int64_t bytesTransferred, int64_t totalBytes);
};

enum class ConflictAction
{
    Overwrite,
    KeepBoth,
    Skip,
    Cancel
};

enum class OverwriteScope
{
    All,
    Newest,
    Oldest
};

class COverwriteConflictDialog : public CCommonDialog
{
public:
    COverwriteConflictDialog(HWND hParent, bool isUpload,
                             const std::string& srcName, int64_t srcSize,
                             const std::string& dstName, int64_t dstSize,
                             int duplicateCount = 1);

    ConflictAction GetAction() const { return m_action; }
    OverwriteScope GetOverwriteScope() const { return m_overwriteScope; }
    bool IsApplyToAll() const { return m_applyToAll; }

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    bool m_isUpload;
    std::string m_srcName;
    int64_t m_srcSize;
    std::string m_dstName;
    int64_t m_dstSize;
    int m_duplicateCount;

    ConflictAction m_action;
    OverwriteScope m_overwriteScope;
    bool m_applyToAll;
};

void OnConfiguration(HWND hParent);
void OnAbout(HWND hParent);
