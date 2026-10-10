#include "ContextMenus.h"

#include "Shared/Log.h"

#include <algorithm>
#include <vector>

namespace
{
    DWORD RealBuildNumber() noexcept
    {
        using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        auto rtlGetVersion = ntdll != nullptr ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
        OSVERSIONINFOW info{sizeof(info)};
        return rtlGetVersion != nullptr && rtlGetVersion(&info) == 0 ? info.dwBuildNumber : 0;
    }

    enum class PreferredAppMode
    {
        Default = 0,
        ForceDark = 2,
    };

    using SetPreferredAppModeFn = int(WINAPI*)(int);
    using AllowDarkModeForWindowFn = bool(WINAPI*)(HWND, bool);
    using FlushMenuThemesFn = void(WINAPI*)();

    enum class State
    {
        Unresolved,
        Ready,
        Disabled,
    };

    State g_state = State::Unresolved;
    SetPreferredAppModeFn g_setPreferredAppMode = nullptr;
    AllowDarkModeForWindowFn g_allowDarkModeForWindow = nullptr;
    FlushMenuThemesFn g_flushMenuThemes = nullptr;
    std::vector<HWND> g_allowed;
}

namespace ContextMenuSeams
{
    BuildNumber buildNumber = RealBuildNumber;
    GetProc getProcAddress = ::GetProcAddress;
}

bool InitializeContextMenus() noexcept
{
    if (g_state != State::Unresolved)
    {
        return g_state == State::Ready;
    }
    g_state = State::Disabled;
    const DWORD build = ContextMenuSeams::buildNumber();
    if (build < 18362)
    {
        Log(L"context menus: build %lu below 18362; left stock", build);
        return false;
    }
    HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (uxtheme == nullptr)
    {
        return false;
    }
    auto setMode = reinterpret_cast<SetPreferredAppModeFn>(ContextMenuSeams::getProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
    auto allow = reinterpret_cast<AllowDarkModeForWindowFn>(ContextMenuSeams::getProcAddress(uxtheme, MAKEINTRESOURCEA(133)));
    auto flush = reinterpret_cast<FlushMenuThemesFn>(ContextMenuSeams::getProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
    if (setMode == nullptr || allow == nullptr || flush == nullptr)
    {
        Log(L"context menus: uxtheme ordinals unavailable; left stock");
        return false;
    }
    g_setPreferredAppMode = setMode;
    g_allowDarkModeForWindow = allow;
    g_flushMenuThemes = flush;
    g_state = State::Ready;
    return true;
}

void ApplyContextMenus(bool dark) noexcept
{
    if (g_state != State::Ready)
    {
        return;
    }
    __try
    {
        g_setPreferredAppMode(static_cast<int>(dark ? PreferredAppMode::ForceDark : PreferredAppMode::Default));
        g_flushMenuThemes();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_state = State::Disabled;
    }
}

void AllowContextMenuDarkMode(HWND window, bool dark) noexcept
{
    if (g_state != State::Ready || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId() ||
        (GetWindowLongW(window, GWL_STYLE) & WS_CHILD) != 0)
    {
        return;
    }
    try
    {
        const auto found = std::find(g_allowed.begin(), g_allowed.end(), window);
        if (dark && found == g_allowed.end())
        {
            g_allowed.push_back(window);
            g_allowDarkModeForWindow(window, true);
        }
        else if (!dark && found != g_allowed.end())
        {
            g_allowed.erase(found);
            g_allowDarkModeForWindow(window, false);
        }
    }
    catch (...)
    {
    }
}
