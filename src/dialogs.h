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

void OnConfiguration(HWND hParent);
void OnAbout(HWND hParent);
