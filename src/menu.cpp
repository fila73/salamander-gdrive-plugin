#include "precomp.h"
#include "gdrive.h"
#include "gdrive_auth.h"
#include "dialogs.h"
#include "gdrive_fs.h"
#include "gdrive_log.h"

CPluginInterfaceForMenuExt InterfaceForMenuExt;

static std::string s_pendingFocusPath;
static std::string s_pendingFocusName;
static int s_pendingFocusPanel = PANEL_SOURCE;

void CPluginInterfaceForMenuExt::PostFocusTarget(int panel, const std::string& path, const std::string& name)
{
    s_pendingFocusPanel = panel;
    s_pendingFocusPath = path;
    s_pendingFocusName = name;
    GDriveLog::Log("[FOCUS] PostFocusTarget: panel=%d, path='%s', name='%s'", panel, path.c_str(), name.c_str());
    if (SalamanderGeneral)
    {
        SalamanderGeneral->PostMenuExtCommand(CM_FIND_FOCUS_TARGET, TRUE);
    }
}

DWORD WINAPI CPluginInterfaceForMenuExt::GetMenuItemState(int id, DWORD eventMask)
{
    DWORD state = MENU_ITEM_STATE_ENABLED;
    bool isAuth = GDriveAuth::AuthManager::GetInstance().IsAuthenticated();

    if (id == CM_DISCONNECT && !isAuth)
    {
        state = 0; // Disabled
    }

    return state;
}

BOOL WINAPI CPluginInterfaceForMenuExt::ExecuteMenuItem(CSalamanderForOperationsAbstract* salamander,
                                                        HWND parent, int id, DWORD eventMask)
{
    switch (id)
    {
    case CM_FIND_FOCUS_TARGET:
    {
        if (!s_pendingFocusPath.empty())
        {
            int failReason = 0;
            GDriveLog::Log("[FOCUS] Calling ChangePanelPathToPluginFS: panel=%d, FS='%s', path='%s', focusName='%s'",
                           s_pendingFocusPanel, AssignedFSName, s_pendingFocusPath.c_str(), s_pendingFocusName.c_str());
            BOOL res = SalamanderGeneral->ChangePanelPathToPluginFS(s_pendingFocusPanel, AssignedFSName,
                                                                    s_pendingFocusPath.c_str(), &failReason, -1,
                                                                    s_pendingFocusName.c_str(), TRUE, TRUE);
            GDriveLog::Log("[FOCUS] ChangePanelPathToPluginFS returned %d, failReason=%d", res, failReason);
            s_pendingFocusPath.clear();
            s_pendingFocusName.clear();

            HWND hMain = SalamanderGeneral->GetMainWindowHWND();
            if (hMain && IsWindow(hMain))
            {
                SetForegroundWindow(hMain);
            }
        }
        return TRUE;
    }

    case CM_OPEN_GDRIVE:
    {
        int failReason = 0;
        SalamanderGeneral->ChangePanelPathToPluginFS(PANEL_SOURCE, AssignedFSName, "\\", &failReason);
        (void)failReason;
        return TRUE;
    }

    case CM_CALC_SIZE:
    {
        CPluginFSInterfaceAbstract* activeFS = SalamanderGeneral->GetPanelPluginFS(PANEL_SOURCE);
        if (activeFS && CPluginFS::IsOurFS(activeFS))
        {
            CPluginFS* fs = static_cast<CPluginFS*>(activeFS);
            fs->CalculateFolderSize(parent, PANEL_SOURCE);
        }
        return TRUE;
    }

    case CM_DISCONNECT:
        SalamanderGeneral->DisconnectFSFromPanel(parent, PANEL_SOURCE);
        return FALSE;

    case CM_CONFIG:
        PluginInterface.Configuration(parent);
        return TRUE;

    case CM_ABOUT:
        PluginInterface.About(parent);
        return TRUE;

    default:
        return FALSE;
    }
}

BOOL WINAPI CPluginInterfaceForMenuExt::HelpForMenuItem(HWND parent, int id)
{
    return FALSE;
}

void WINAPI CPluginInterfaceForMenuExt::BuildMenu(HWND parent, CSalamanderBuildMenuAbstract* salamander)
{
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_CONNECT), 0, CM_OPEN_GDRIVE, FALSE,
                            MENU_EVENT_TRUE, 0, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_CALC_SIZE), 0, CM_CALC_SIZE, FALSE,
                            MENU_EVENT_TRUE, MENU_EVENT_THIS_PLUGIN_FS, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_DISCONNECT), 0, CM_DISCONNECT, FALSE,
                            MENU_EVENT_TRUE, MENU_EVENT_THIS_PLUGIN_FS, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, NULL, 0, 0, FALSE, 0, 0, MENU_SKILLLEVEL_ALL); // separator
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_CONFIG), 0, CM_CONFIG, FALSE,
                            MENU_EVENT_TRUE, 0, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_ABOUT), 0, CM_ABOUT, FALSE,
                            MENU_EVENT_TRUE, 0, MENU_SKILLLEVEL_ALL);
}
