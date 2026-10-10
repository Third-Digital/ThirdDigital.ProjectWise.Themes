#include <windows.h>

#include <aawindms.h>

#include "Features/ThemePicker/ThemeMenu.h"
#include "Features/ThemeSwitch.h"
#include "Shared/Log.h"

#include <cwchar>
#include <mutex>

namespace
{
    // AAMODULE_EXPLORER also covers the Windows Extension, so the host executable is checked as well.
    bool IsExplorerHost() noexcept
    {
        wchar_t path[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            return false;
        }
        const wchar_t* name = wcsrchr(path, L'\\');
        return _wcsicmp(name != nullptr ? name + 1 : path, L"pwc.exe") == 0;
    }

    void Initialise(ULONG applicationMask)
    {
        Log(L"CustomInitialize: thread %lu mask 0x%08lX", GetCurrentThreadId(), applicationMask);
        if ((applicationMask & AAMODULE_EXPLORER) == 0)
        {
            Log(L"application mask is not ProjectWise Explorer; module inactive");
            return;
        }
        if (!IsExplorerHost())
        {
            Log(L"host is not pwc.exe; module inactive");
            return;
        }
        // Hooks, timers and window procedures point into this module, so it must never unload.
        HMODULE self = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                reinterpret_cast<LPCWSTR>(&IsExplorerHost), &self))
        {
            Log(L"module could not be pinned (%lu); module inactive", GetLastError());
            return;
        }
        if (GetModuleHandleW(L"dmawin.dll") != nullptr)
        {
            RegisterThemeMenu();
        }
        else
        {
            Log(L"dmawin.dll not loaded; Tools menu picker skipped");
        }
        StartTheming();
    }

    void InitialiseCpp(ULONG applicationMask) noexcept
    {
        try
        {
            Initialise(applicationMask);
        }
        catch (...)
        {
            Log(L"initialisation failed; stock appearance restored");
            RestoreStock();
        }
    }

    void RestoreAfterFault() noexcept
    {
        __try
        {
            Log(L"initialisation fault; stock appearance restored");
            RestoreStock();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    // Kept free of objects with destructors so it can hold the SEH guard; std::call_once must always see
    // this return normally.
    void InitialiseGuarded(ULONG applicationMask) noexcept
    {
        __try
        {
            InitialiseCpp(applicationMask);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            RestoreAfterFault();
        }
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

extern "C" LONG WINAPI CustomInitialize(ULONG applicationMask, LPVOID reserved)
{
    UNREFERENCED_PARAMETER(reserved);
    static std::once_flag once;
    try
    {
        std::call_once(once, [applicationMask] { InitialiseGuarded(applicationMask); });
    }
    catch (...)
    {
    }
    return IDOK;
}
