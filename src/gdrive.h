// SPDX-FileCopyrightText: 2026 Open Salamander Authors & Red Salamander Authors
// SPDX-FileContributor: Ported to Open Salamander framework by fila73
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "spl_base.h"
#include "spl_gen.h"
#include "spl_gui.h"
#include "spl_fs.h"
#include "spl_menu.h"

extern const char* PluginNameEN;
extern const char* PluginNameShort;

extern HINSTANCE DLLInstance;
extern HINSTANCE HLanguage;

extern CSalamanderGeneralAbstract* SalamanderGeneral;
extern CSalamanderGUIAbstract* SalamanderGUI;
extern CSalamanderDebugAbstract* SalamanderDebug;
extern int SalamanderVersion;

extern unsigned char* LowerCase;
extern unsigned char* UpperCase;

extern char AssignedFSName[MAX_PATH];
extern int AssignedFSNameLen;

// Configuration variables
extern std::string CfgClientId;
extern std::string CfgClientSecret;
extern BOOL CfgIncludeSharedDrives;

char* LoadStr(int resID);

class CPluginInterfaceForMenuExt : public CPluginInterfaceForMenuExtAbstract
{
public:
    virtual DWORD WINAPI GetMenuItemState(int id, DWORD eventMask) override;
    virtual BOOL WINAPI ExecuteMenuItem(CSalamanderForOperationsAbstract* salamander, HWND parent,
                                        int id, DWORD eventMask) override;
    virtual BOOL WINAPI HelpForMenuItem(HWND parent, int id) override;
    virtual void WINAPI BuildMenu(HWND parent, CSalamanderBuildMenuAbstract* salamander) override;
};

class CPluginInterface : public CPluginInterfaceAbstract
{
public:
    virtual void WINAPI About(HWND parent) override;
    virtual BOOL WINAPI Release(HWND parent, BOOL force) override;
    virtual void WINAPI LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry) override;
    virtual void WINAPI SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry) override;
    virtual void WINAPI Configuration(HWND parent) override;
    virtual void WINAPI Connect(HWND parent, CSalamanderConnectAbstract* salamander) override;

    virtual void WINAPI ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData) override {}
    virtual CPluginInterfaceForArchiverAbstract* WINAPI GetInterfaceForArchiver() override { return NULL; }
    virtual CPluginInterfaceForViewerAbstract* WINAPI GetInterfaceForViewer() override { return NULL; }
    virtual CPluginInterfaceForMenuExtAbstract* WINAPI GetInterfaceForMenuExt() override;
    virtual CPluginInterfaceForFSAbstract* WINAPI GetInterfaceForFS() override;
    virtual CPluginInterfaceForThumbLoaderAbstract* WINAPI GetInterfaceForThumbLoader() override { return NULL; }

    virtual void WINAPI Event(int event, DWORD param) override {}
    virtual void WINAPI ClearHistory(HWND parent) override {}
    virtual void WINAPI AcceptChangeOnPathNotification(const char* path, BOOL includingSubdirs) override {}
    virtual void WINAPI PasswordManagerEvent(HWND parent, int event) override {}
};

extern CPluginInterface PluginInterface;
extern CPluginInterfaceForMenuExt InterfaceForMenuExt;
