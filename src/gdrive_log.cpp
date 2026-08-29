// SPDX-FileCopyrightText: 2026 fila73
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "gdrive_log.h"
#include <windows.h>
#include <shlobj.h>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <chrono>
#include <ctime>

namespace GDriveLog
{
static std::mutex s_logMutex;

std::wstring GetLogFilePath()
{
    wchar_t appData[MAX_PATH] = {0};
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData)))
    {
        return L"";
    }

    std::wstring dir = std::wstring(appData) + L"\\Open Salamander\\plugins\\gdrive";
    CreateDirectoryW((std::wstring(appData) + L"\\Open Salamander").c_str(), NULL);
    CreateDirectoryW((std::wstring(appData) + L"\\Open Salamander\\plugins").c_str(), NULL);
    CreateDirectoryW(dir.c_str(), NULL);

    return dir + L"\\gdrive_debug.log";
}

void Log(const char* fmt, ...)
{
    // Logging disabled for release
    (void)fmt;
}

} // namespace GDriveLog
