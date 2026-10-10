#include "StatusBar.h"

#include "Features/SystemColours/SystemColourHooks.h"
#include "Shared/Log.h"

#include <MinHook.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <vssym32.h>

#include <algorithm>
#include <cwchar>
#include <vector>

namespace
{
    constexpr UINT_PTR kSubclassId = 0x54445053;
    constexpr wchar_t kClassPrefix[] = L"Afx:StatusBar";

    using SetTextColorFn = COLORREF(WINAPI*)(HDC, COLORREF);
    using DrawThemeBackgroundFn = HRESULT(WINAPI*)(HTHEME, HDC, int, int, LPCRECT, LPCRECT);
    using DrawThemeTextFn = HRESULT(WINAPI*)(HTHEME, HDC, int, int, LPCWSTR, int, DWORD, DWORD, LPCRECT);

    enum class State
    {
        NotInstalled,
        Installed,
        Disabled,
    };

    State g_state = State::NotInstalled;
    SetTextColorFn g_setTextColor = nullptr;
    DrawThemeBackgroundFn g_drawThemeBackground = nullptr;
    DrawThemeTextFn g_drawThemeText = nullptr;
    COLORREF g_stockGrayText = 0;
    thread_local int t_paintDepth = 0;

    // UI thread only.
    std::vector<HWND> g_windows;

    // Detour: no allocation, locks or logging. Only text set while a subclassed status bar paints is mapped.
    COLORREF WINAPI DetourSetTextColor(HDC dc, COLORREF colour)
    {
        if (t_paintDepth > 0)
        {
            if (const PaletteResources* p = ActivePalette())
            {
                if (colour == *p->palette.At(COLOR_GRAYTEXT) || colour == g_stockGrayText)
                {
                    colour = *p->palette.At(COLOR_BTNTEXT);
                }
            }
        }
        return g_setTextColor(dc, colour);
    }

    // MFC's Office 2003 / VS 2005 managers fill the bar and its panes from the uxtheme STATUS class, which the
    // colour hooks cannot reach. Detour rules as above.
    HRESULT WINAPI DetourDrawThemeBackground(HTHEME theme, HDC dc, int part, int state, LPCRECT rect, LPCRECT clip)
    {
        if (t_paintDepth > 0 && rect != nullptr && (part == 0 || part == SP_PANE))
        {
            if (const PaletteResources* p = ActivePalette())
            {
                HBRUSH brush = p->Brush(COLOR_3DFACE);
                if (brush != nullptr && GetObjectType(brush) == OBJ_BRUSH)
                {
                    RECT area = *rect;
                    if (clip == nullptr || IntersectRect(&area, rect, clip))
                    {
                        FillRect(dc, &area, brush);
                    }
                    return S_OK;
                }
            }
        }
        return g_drawThemeBackground(theme, dc, part, state, rect, clip);
    }

    // The comctl32 status bar draws pane text from the STATUS theme colour, which the colour hooks cannot reach.
    // Detour rules as above.
    HRESULT WINAPI DetourDrawThemeText(HTHEME theme, HDC dc, int part, int state, LPCWSTR text, int length, DWORD flags, DWORD flags2, LPCRECT rect)
    {
        if (t_paintDepth > 0 && rect != nullptr)
        {
            if (const PaletteResources* p = ActivePalette())
            {
                DTTOPTS options{sizeof(options)};
                options.dwFlags = DTT_TEXTCOLOR;
                options.crText = *p->palette.At(COLOR_BTNTEXT);
                RECT area = *rect;
                return DrawThemeTextEx(theme, dc, part, state, text, length, flags, &area, &options);
            }
        }
        return g_drawThemeText(theme, dc, part, state, text, length, flags, flags2, rect);
    }

    // The depth must drop even when a fault unwinds through the paint, or SetTextColor stays mapped.
    LRESULT PaintWithDepth(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
    {
        ++t_paintDepth;
        __try
        {
            return DefSubclassProc(window, message, wParam, lParam);
        }
        __finally
        {
            --t_paintDepth;
        }
    }

    LRESULT CALLBACK StatusBarProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR)
    {
        if (message == WM_PAINT || message == WM_PRINTCLIENT)
        {
            return PaintWithDepth(window, message, wParam, lParam);
        }
        if (message == WM_NCDESTROY)
        {
            RemoveWindowSubclass(window, StatusBarProc, id);
            g_windows.erase(std::remove(g_windows.begin(), g_windows.end(), window), g_windows.end());
        }
        return DefSubclassProc(window, message, wParam, lParam);
    }
}

bool InstallStatusBarHook() noexcept
{
    if (g_state != State::NotInstalled)
    {
        return g_state == State::Installed;
    }
    g_state = State::Disabled;
    g_stockGrayText = OriginalGetSysColor(COLOR_GRAYTEXT);
    // All three hooks or none, for this slice only.
    void* text = nullptr;
    void* background = nullptr;
    void* themeText = nullptr;
    if (MH_CreateHookApiEx(L"gdi32.dll", "SetTextColor", reinterpret_cast<void*>(DetourSetTextColor), reinterpret_cast<void**>(&g_setTextColor), &text) != MH_OK)
    {
        Log(L"status bar: SetTextColor hook could not be created; left stock");
        return false;
    }
    if (MH_CreateHookApiEx(L"uxtheme.dll", "DrawThemeBackground", reinterpret_cast<void*>(DetourDrawThemeBackground),
                           reinterpret_cast<void**>(&g_drawThemeBackground), &background) != MH_OK)
    {
        MH_RemoveHook(text);
        Log(L"status bar: DrawThemeBackground hook could not be created; left stock");
        return false;
    }
    if (MH_CreateHookApiEx(L"uxtheme.dll", "DrawThemeText", reinterpret_cast<void*>(DetourDrawThemeText), reinterpret_cast<void**>(&g_drawThemeText),
                           &themeText) != MH_OK)
    {
        MH_RemoveHook(text);
        MH_RemoveHook(background);
        Log(L"status bar: DrawThemeText hook could not be created; left stock");
        return false;
    }
    if (MH_EnableHook(text) != MH_OK || MH_EnableHook(background) != MH_OK || MH_EnableHook(themeText) != MH_OK)
    {
        // Possibly entered already, so the trampolines stay allocated.
        MH_DisableHook(text);
        MH_DisableHook(background);
        MH_DisableHook(themeText);
        Log(L"status bar: hooks could not be enabled; left stock");
        return false;
    }
    g_state = State::Installed;
    return true;
}

bool IsStatusBarClass(HWND window) noexcept
{
    wchar_t name[64]{};
    return GetClassNameW(window, name, ARRAYSIZE(name)) > 0 &&
           (wcsncmp(name, kClassPrefix, ARRAYSIZE(kClassPrefix) - 1) == 0 || _wcsicmp(name, STATUSCLASSNAMEW) == 0);
}

void ApplyStatusBar(HWND window, const PaletteResources* resources) noexcept
{
    if (g_state != State::Installed || resources == nullptr || window == nullptr || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId() ||
        !IsStatusBarClass(window))
    {
        return;
    }
    try
    {
        if (std::find(g_windows.begin(), g_windows.end(), window) != g_windows.end())
        {
            return;
        }
        g_windows.reserve(g_windows.size() + 1);
        if (SetWindowSubclass(window, StatusBarProc, kSubclassId, 0))
        {
            g_windows.push_back(window);
            InvalidateRect(window, nullptr, TRUE);
        }
    }
    catch (...)
    {
    }
}
