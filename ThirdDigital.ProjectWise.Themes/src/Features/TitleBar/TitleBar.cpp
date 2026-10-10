#include "TitleBar.h"

#include "Features/ControlColours/ControlSubclass.h"

#include <dwmapi.h>

#include <algorithm>
#include <vector>

namespace
{
    struct Applied
    {
        HWND window;
        COLORREF border;
        COLORREF caption;
        COLORREF text;
    };

    // UI thread only.
    std::vector<Applied> g_changed;

    bool SupportsDwmColours() noexcept
    {
        static const bool supported = [] {
            using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
            HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
            auto rtlGetVersion = ntdll != nullptr ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
            OSVERSIONINFOW info{sizeof(info)};
            return rtlGetVersion != nullptr && rtlGetVersion(&info) == 0 && info.dwMajorVersion >= 10 && info.dwBuildNumber >= 22000;
        }();
        return supported;
    }

    bool Set(HWND window, DWORD attribute, DWORD value) noexcept
    {
        return SUCCEEDED(DwmSetWindowAttribute(window, attribute, &value, sizeof(value)));
    }

    Applied* Find(HWND window) noexcept
    {
        const auto found = std::find_if(g_changed.begin(), g_changed.end(), [window](const Applied& a) { return a.window == window; });
        return found != g_changed.end() ? &*found : nullptr;
    }

    bool Changed(HWND window) noexcept
    {
        return Find(window) != nullptr;
    }
}

void ApplyTitleBar(HWND window, const PaletteResources* resources) noexcept
{
    if (!OwnedByUiThread(window))
    {
        return;
    }
    const LONG style = GetWindowLongW(window, GWL_STYLE);
    if ((style & WS_CHILD) != 0)
    {
        return;
    }
    try
    {
        if (resources != nullptr && resources->palette.isDark && SupportsDwmColours() && (style & WS_CAPTION) == WS_CAPTION)
        {
            const SystemColourPalette& palette = resources->palette;
            const Applied wanted{window, *palette.At(COLOR_ACTIVEBORDER), *palette.At(COLOR_ACTIVECAPTION), *palette.At(COLOR_CAPTIONTEXT)};
            if (const Applied* current = Find(window);
                current != nullptr && current->border == wanted.border && current->caption == wanted.caption && current->text == wanted.text)
            {
                return;
            }
            g_changed.reserve(g_changed.size() + 1);
            bool applied = Set(window, DWMWA_USE_IMMERSIVE_DARK_MODE, TRUE);
            applied |= Set(window, DWMWA_BORDER_COLOR, wanted.border);
            applied |= Set(window, DWMWA_CAPTION_COLOR, wanted.caption);
            applied |= Set(window, DWMWA_TEXT_COLOR, wanted.text);
            if (Applied* current = Find(window))
            {
                *current = wanted;
            }
            else if (applied)
            {
                g_changed.push_back(wanted);
            }
        }
        else if (Changed(window))
        {
            ForgetTitleBar(window);
            Set(window, DWMWA_USE_IMMERSIVE_DARK_MODE, FALSE);
            Set(window, DWMWA_BORDER_COLOR, DWMWA_COLOR_DEFAULT);
            Set(window, DWMWA_CAPTION_COLOR, DWMWA_COLOR_DEFAULT);
            Set(window, DWMWA_TEXT_COLOR, DWMWA_COLOR_DEFAULT);
        }
    }
    catch (...)
    {
    }
}

void ForgetTitleBar(HWND window) noexcept
{
    g_changed.erase(std::remove_if(g_changed.begin(), g_changed.end(), [window](const Applied& a) { return a.window == window; }), g_changed.end());
}
