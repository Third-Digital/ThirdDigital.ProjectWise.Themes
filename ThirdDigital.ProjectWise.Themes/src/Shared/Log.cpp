#include "Log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>

void Log(const wchar_t* format, ...) noexcept
{
    wchar_t message[1024]{};
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(message, _TRUNCATE, format, args);
    va_end(args);

    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t line[1200]{};
    _snwprintf_s(line, _TRUNCATE, L"%04u-%02u-%02u %02u:%02u:%02u.%03u [pid %lu tid %lu] %s\r\n", now.wYear, now.wMonth, now.wDay,
                 now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, GetCurrentProcessId(), GetCurrentThreadId(), message);
    OutputDebugStringW(line);

    wchar_t path[MAX_PATH + 1]{};
    const DWORD tempLength = GetTempPathW(MAX_PATH + 1, path);
    if (tempLength == 0 || tempLength > MAX_PATH)
    {
        return;
    }
    if (wcscat_s(path, L"ThirdDigital") != 0)
    {
        return;
    }
    CreateDirectoryW(path, nullptr);
    if (wcscat_s(path, L"\\projectwise-themes.log") != 0)
    {
        return;
    }

    char utf8[sizeof(line) * 3]{};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8), nullptr, nullptr);
    if (bytes <= 1)
    {
        return;
    }
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }
    DWORD written = 0;
    WriteFile(file, utf8, static_cast<DWORD>(bytes - 1), &written, nullptr);
    CloseHandle(file);
}
