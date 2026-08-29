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
    if (!fmt) return;

    char buffer[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // Format local timestamp
    SYSTEMTIME st;
    GetLocalTime(&st);
    char timeStr[64];
    snprintf(timeStr, sizeof(timeStr), "[%04d-%02d-%02d %02d:%02d:%02d.%03d] ",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    std::string fullMsg = std::string(timeStr) + buffer + "\n";

    OutputDebugStringA(fullMsg.c_str());

    std::lock_guard<std::mutex> lock(s_logMutex);
    std::wstring logPath = GetLogFilePath();
    if (!logPath.empty())
    {
        FILE* f = _wfopen(logPath.c_str(), L"a, ccs=UTF-8");
        if (f)
        {
            fputs(fullMsg.c_str(), f);
            fclose(f);
        }
    }
}

} // namespace GDriveLog
