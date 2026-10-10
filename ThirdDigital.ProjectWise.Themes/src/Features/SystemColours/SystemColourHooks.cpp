#include "SystemColourHooks.h"

#include "Shared/ActivePalette.h"
#include "Shared/Log.h"

#include <iterator>

namespace HookSeams
{
    Initialize initialize = MH_Initialize;
    CreateHookApiEx createHookApiEx = MH_CreateHookApiEx;
    EnableHook enableHook = MH_EnableHook;
    DisableHook disableHook = MH_DisableHook;
    Uninitialize uninitialize = MH_Uninitialize;
}

namespace
{
    using GetSysColorFn = DWORD(WINAPI*)(int);
    using GetSysColorBrushFn = HBRUSH(WINAPI*)(int);
    using GetThemeSysColorFn = COLORREF(WINAPI*)(HTHEME, int);
    using GetThemeSysColorBrushFn = HBRUSH(WINAPI*)(HTHEME, int);

    GetSysColorFn g_getSysColor = nullptr;
    GetSysColorBrushFn g_getSysColorBrush = nullptr;
    GetThemeSysColorFn g_getThemeSysColor = nullptr;
    GetThemeSysColorBrushFn g_getThemeSysColorBrush = nullptr;

    enum class State
    {
        NotInstalled,
        Installed,
        PassThrough,
    };

    State g_state = State::NotInstalled;

    // Detours: no allocation, locks, logging or calls into other detoured entry points. Both uxtheme
    // functions take COLOR_* IDs (TMT_* IDs are rejected by the real functions), so only COLOR_* is mapped.

    DWORD WINAPI DetourGetSysColor(int index)
    {
        if (const PaletteResources* p = ActivePalette())
        {
            if (const auto colour = p->palette.At(index))
            {
                return *colour;
            }
        }
        return g_getSysColor(index);
    }

    HBRUSH WINAPI DetourGetSysColorBrush(int index)
    {
        if (const PaletteResources* p = ActivePalette())
        {
            HBRUSH brush = p->Brush(index);
            LOGBRUSH info{};
            // GetObjectType can report cached type bits for a deleted handle. Query the object itself.
            if (brush != nullptr && GetObjectW(brush, sizeof(info), &info) == sizeof(info))
            {
                return brush;
            }
        }
        return g_getSysColorBrush(index);
    }

    COLORREF WINAPI DetourGetThemeSysColor(HTHEME theme, int index)
    {
        if (const PaletteResources* p = ActivePalette())
        {
            if (const auto colour = p->palette.At(index))
            {
                return *colour;
            }
        }
        return g_getThemeSysColor(theme, index);
    }

    // The caller owns and deletes the returned brush, as with the real function.
    HBRUSH WINAPI DetourGetThemeSysColorBrush(HTHEME theme, int index)
    {
        if (const PaletteResources* p = ActivePalette())
        {
            if (const auto colour = p->palette.At(index))
            {
                if (HBRUSH brush = CreateSolidBrush(*colour))
                {
                    return brush;
                }
            }
        }
        return g_getThemeSysColorBrush(theme, index);
    }

    struct HookSpec
    {
        const wchar_t* module;
        const char* name;
        void* detour;
        void** original;
    };

    const HookSpec kHooks[] = {
        {L"user32.dll", "GetSysColor", reinterpret_cast<void*>(DetourGetSysColor), reinterpret_cast<void**>(&g_getSysColor)},
        {L"user32.dll", "GetSysColorBrush", reinterpret_cast<void*>(DetourGetSysColorBrush), reinterpret_cast<void**>(&g_getSysColorBrush)},
        {L"uxtheme.dll", "GetThemeSysColor", reinterpret_cast<void*>(DetourGetThemeSysColor), reinterpret_cast<void**>(&g_getThemeSysColor)},
        {L"uxtheme.dll", "GetThemeSysColorBrush", reinterpret_cast<void*>(DetourGetThemeSysColorBrush),
         reinterpret_cast<void**>(&g_getThemeSysColorBrush)},
    };
}

bool InstallSystemColourHooks() noexcept
{
    if (g_state == State::Installed)
    {
        return true;
    }
    if (g_state == State::PassThrough)
    {
        return false;
    }

    if (LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) == nullptr)
    {
        Log(L"colour hooks: uxtheme.dll could not be loaded (%lu)", GetLastError());
        return false;
    }
    const MH_STATUS initialised = HookSeams::initialize();
    if (initialised != MH_OK)
    {
        Log(L"colour hooks: MH_Initialize failed (%d)", static_cast<int>(initialised));
        return false;
    }

    void* targets[std::size(kHooks)]{};
    for (std::size_t i = 0; i < std::size(kHooks); ++i)
    {
        const HookSpec& hook = kHooks[i];
        const MH_STATUS created = HookSeams::createHookApiEx(hook.module, hook.name, hook.detour, hook.original, &targets[i]);
        if (created != MH_OK)
        {
            Log(L"colour hooks: creating %S failed (%d)", hook.name, static_cast<int>(created));
            HookSeams::uninitialize();
            return false;
        }
    }

    // Enabled one at a time; the palette is still unpublished, so a partly enabled set only passes through.
    for (std::size_t i = 0; i < std::size(kHooks); ++i)
    {
        const MH_STATUS enabled = HookSeams::enableHook(targets[i]);
        if (enabled != MH_OK)
        {
            Log(L"colour hooks: enabling %S failed (%d); hooks disabled for this process", kHooks[i].name, static_cast<int>(enabled));
            // A thread may already be inside a trampoline, so MinHook is never uninitialised from here on.
            HookSeams::disableHook(MH_ALL_HOOKS);
            g_state = State::PassThrough;
            return false;
        }
    }

    g_state = State::Installed;
    Log(L"colour hooks installed");
    return true;
}

DWORD OriginalGetSysColor(int index) noexcept
{
    return g_state == State::Installed ? g_getSysColor(index) : ::GetSysColor(index);
}

HBRUSH OriginalGetSysColorBrush(int index) noexcept
{
    return g_state == State::Installed ? g_getSysColorBrush(index) : ::GetSysColorBrush(index);
}
