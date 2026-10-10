#pragma once

#include <windows.h>
#include <uxtheme.h>

#include <MinHook.h>

// UI thread only. The palette must stay unpublished until this returns true. Once installed the hooks stay
// for the life of the process and pass through whenever no palette is published. A failure after any hook
// was enabled leaves MinHook initialised (trampolines stay reachable) and disables further attempts.
bool InstallSystemColourHooks() noexcept;

DWORD OriginalGetSysColor(int index) noexcept;
HBRUSH OriginalGetSysColorBrush(int index) noexcept;

namespace HookSeams
{
    using Initialize = MH_STATUS(WINAPI*)();
    using CreateHookApiEx = MH_STATUS(WINAPI*)(LPCWSTR, LPCSTR, LPVOID, LPVOID*, LPVOID*);
    using EnableHook = MH_STATUS(WINAPI*)(LPVOID);
    using DisableHook = MH_STATUS(WINAPI*)(LPVOID);
    using Uninitialize = MH_STATUS(WINAPI*)();

    extern Initialize initialize;
    extern CreateHookApiEx createHookApiEx;
    extern EnableHook enableHook;
    extern DisableHook disableHook;
    extern Uninitialize uninitialize;
}
