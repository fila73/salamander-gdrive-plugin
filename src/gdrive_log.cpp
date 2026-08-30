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
    std::lock_guard<std::mutex> lock(s_logMutex);
    std::wstring logPath = GetLogFilePath();
    if (logPath.empty()) return;

    char buf[2048] = {0};
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &t);

    char timeStr[64];
    std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &tm);

    char finalLine[2200];
    snprintf(finalLine, sizeof(finalLine), "[%s] %s\n", timeStr, buf);

    OutputDebugStringA(finalLine);

    FILE* f = _wfopen(logPath.c_str(), L"a, ccs=UTF-8");
    if (f)
    {
        fputs(finalLine, f);
        fclose(f);
    }
}

} // namespace GDriveLog
