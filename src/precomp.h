// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <bcrypt.h>

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <memory>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#define SALSDK_COMPATIBLE_WITH_VER 103

#include "versinfo.rh2"

#include "spl_com.h"
#include "spl_base.h"
#include "spl_arc.h"
#include "spl_gen.h"
#include "spl_fs.h"
#include "spl_menu.h"
#include "spl_thum.h"
#include "spl_view.h"
#include "spl_vers.h"
#include "spl_gui.h"

#include "dbg.h"
#include "mhandles.h"
#include "arraylt.h"
#define ENABLE_PROPERTYDIALOG
#include "winliblt.h"
#include "auxtools.h"
#include "plugindarkmode.h"

#include "gdrive.rh"
#include "gdrive.rh2"
#include "lang/lang.rh"

#include "gdrivedarkmode.h"
#include "gdrive_json.h"
#include "gdrive_http.h"
#include "gdrive_auth.h"
#include "gdrive_api.h"
#include "dialogs.h"
#include "gdrive_fs.h"
#include "gdrive.h"
