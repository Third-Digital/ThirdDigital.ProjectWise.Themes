#include "ThemeSwitch.h"

#include "Features/ContextMenus/ContextMenus.h"
#include "Features/ControlColours/ControlSubclass.h"
#include "Features/MenuBar/MenuBar.h"
#include "Features/Persistence/ThemeSettings.h"
#include "Features/StatusBar/StatusBar.h"
#include "Features/PaintColours/PaintColours.h"
#include "Features/SystemColours/SystemColourHooks.h"
#include "Features/TitleBar/TitleBar.h"
#include "Shared/ActivePalette.h"
#include "Shared/Log.h"

#include <vector>

namespace
{
    DWORD g_uiThread = 0;
    UINT_PTR g_refreshTimer = 0;

    thread_local bool t_inTransition = false;
    thread_local bool t_hasPending = false;
    thread_local const ColourScheme* t_pending = nullptr;

    constexpr UINT kRefreshFlags = RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN;

    BOOL CALLBACK CollectProcessWindow(HWND window, LPARAM list)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid == GetCurrentProcessId())
        {
            reinterpret_cast<std::vector<HWND>*>(list)->push_back(window);
        }
        return TRUE;
    }

    // Windows of other threads only get posted notifications: SetWindowTheme, subclassing and DWM calls
    // send messages synchronously, and a stalled thread would block Explorer's UI thread.
    bool RefreshWindows(bool themeClassChanged) noexcept
    {
        try
        {
            const PaletteResources* p = ActivePalette();
            std::vector<HWND> topLevel;
            EnumWindows(CollectProcessWindow, reinterpret_cast<LPARAM>(&topLevel));
            for (HWND top : topLevel)
            {
                if (!IsWindow(top))
                {
                    continue;
                }
                if (GetWindowThreadProcessId(top, nullptr) != GetCurrentThreadId())
                {
                    SendNotifyMessageW(top, WM_SYSCOLORCHANGE, 0, 0);
                    if (themeClassChanged)
                    {
                        SendNotifyMessageW(top, WM_THEMECHANGED, 0, 0);
                    }
                    RedrawWindow(top, nullptr, nullptr, kRefreshFlags);
                    continue;
                }

                ApplyWindowTree(top, p);
                DWORD_PTR ignored = 0;
                SendMessageTimeoutW(top, WM_SYSCOLORCHANGE, 0, 0, SMTO_ABORTIFHUNG, 1000, &ignored);
                if (themeClassChanged)
                {
                    SendMessageTimeoutW(top, WM_THEMECHANGED, 0, 0, SMTO_ABORTIFHUNG, 1000, &ignored);
                }
                RedrawWindow(top, nullptr, nullptr, kRefreshFlags);
            }
            return true;
        }
        catch (...)
        {
            Log(L"theme switch: refreshing windows failed");
            return false;
        }
    }

    void RestoreStockGuarded() noexcept
    {
        __try
        {
            RestoreStock();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    // A5 rollback for a structured exception: the transition state must be cleared, or every later
    // request would be deferred behind a transition that never finishes.
    void RecoverFromFault() noexcept
    {
        t_inTransition = false;
        t_hasPending = false;
        RestoreStockGuarded();
        Log(L"theme switch: fault during theme switch; stock appearance restored");
    }

    void CALLBACK DeferredRefresh(HWND, UINT, UINT_PTR id, DWORD) noexcept
    {
        KillTimer(nullptr, id);
        if (id == g_refreshTimer)
        {
            g_refreshTimer = 0;
        }
        __try
        {
            if (!RefreshWindows(true))
            {
                RestoreStock();
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            RecoverFromFault();
        }
    }

    bool Apply(const ColourScheme& scheme, bool deferRefresh) noexcept
    {
        const PaletteResources* before = ActivePalette();
        const PaletteResources* p = PreparePalette(scheme);
        if (p == nullptr)
        {
            Log(L"theme switch: palette for '%s' could not be prepared", scheme.name);
            RestoreStock();
            return false;
        }
        if (!InstallSystemColourHooks() || !InstallControlHook(GetCurrentThreadId()))
        {
            RestoreStock();
            return false;
        }
        SetDeferredFaultHandler(RecoverFromFault);
        InstallStatusBarHook();
        InstallPaintColourHooks();
        PublishPalette(p);
        InitializeContextMenus();
        ApplyContextMenus(p->palette.isDark);
        const bool themeClassChanged = before == nullptr || before->palette.isDark != p->palette.isDark;
        if (deferRefresh)
        {
            g_refreshTimer = SetTimer(nullptr, 0, 0, DeferredRefresh);
            if (g_refreshTimer == 0 && !RefreshWindows(themeClassChanged))
            {
                RestoreStock();
                return false;
            }
        }
        else if (!RefreshWindows(themeClassChanged))
        {
            RestoreStock();
            return false;
        }
        Log(L"theme switch: '%s' active", scheme.name);
        return true;
    }

    bool Transition(const ColourScheme* scheme) noexcept
    {
        if (!SaveSelection(scheme))
        {
            Log(L"theme switch: preference could not be saved; selection unchanged");
            return false;
        }
        if (scheme == nullptr)
        {
            const bool wasThemed = ActivePalette() != nullptr;
            PublishPalette(nullptr);
            ApplyContextMenus(false);
            RefreshWindows(wasThemed);
            Log(L"theme switch: None");
            return true;
        }
        return Apply(*scheme, false);
    }

    // Kept free of objects with destructors so it can hold the SEH guard.
    bool GuardedTransition(const ColourScheme* scheme, bool& faulted) noexcept
    {
        __try
        {
            return Transition(scheme);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            faulted = true;
            RecoverFromFault();
            return false;
        }
    }
}

bool StartTheming() noexcept
{
    g_uiThread = GetCurrentThreadId();
    const ColourScheme* saved = ReadSelection();
    if (saved == nullptr)
    {
        Log(L"theme switch: no saved selection; stock appearance");
        return false;
    }
    return Apply(*saved, true);
}

bool Activate(const ColourScheme& scheme) noexcept
{
    return Apply(scheme, false);
}

bool Switch(const ColourScheme* scheme) noexcept
{
    if (g_uiThread == 0 || GetCurrentThreadId() != g_uiThread)
    {
        Log(L"theme switch: request from thread %lu ignored (UI thread %lu)", GetCurrentThreadId(), g_uiThread);
        return false;
    }
    if (t_inTransition)
    {
        t_pending = scheme;
        t_hasPending = true;
        return true;
    }
    t_inTransition = true;
    bool faulted = false;
    bool result = GuardedTransition(scheme, faulted);
    while (!faulted && t_hasPending)
    {
        t_hasPending = false;
        result = GuardedTransition(t_pending, faulted);
    }
    t_inTransition = false;
    return result;
}

void ApplyToExistingWindows(bool themeClassChanged) noexcept
{
    if (!RefreshWindows(themeClassChanged))
    {
        RestoreStock();
    }
}

void RestoreStock() noexcept
{
    PublishPalette(nullptr);
    ApplyContextMenus(false);
    RefreshWindows(true);
}
