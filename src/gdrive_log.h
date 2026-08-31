// SPDX-FileCopyrightText: 2026 fila73
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <cstdint>

namespace GDriveLog
{
    void Log(const char* fmt, ...);
    std::wstring GetLogFilePath();
    void SetFileLoggingEnabled(bool enabled);
    bool IsFileLoggingEnabled();
}
