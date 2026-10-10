#pragma once

#include <windows.h>

// Windows' own dark Win32 menus through undocumented uxtheme ordinals (#133 AllowDarkModeForWindow,
// #135 SetPreferredAppMode, #136 FlushMenuThemes). Resolved all-or-nothing on build 18362 or later; if
// anything is missing every call below is a no-op. UI thread only.
bool InitializeContextMenus() noexcept;

void ApplyContextMenus(bool dark) noexcept;

// Applies to top-level windows of this process owned by the UI thread; false only undoes windows this
// slice changed.
void AllowContextMenuDarkMode(HWND window, bool dark) noexcept;

namespace ContextMenuSeams
{
    using BuildNumber = DWORD (*)() noexcept;
    using GetProc = FARPROC(WINAPI*)(HMODULE, LPCSTR);
    extern BuildNumber buildNumber;
    extern GetProc getProcAddress;
}
