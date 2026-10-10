#include "ExplorerSurface.h"

#include <windows.h>

namespace
{
    struct ClassName
    {
        std::wstring_view name;
        ExplorerSurface surface;
    };

    constexpr ClassName kClasses[] = {
        {L"#32770", ExplorerSurface::Dialog},
        {L"Static", ExplorerSurface::Static},
        {L"Edit", ExplorerSurface::Edit},
        {L"RichEdit20W", ExplorerSurface::Edit},
        {L"RichEdit20A", ExplorerSurface::Edit},
        {L"RICHEDIT50W", ExplorerSurface::Edit},
        {L"ListBox", ExplorerSurface::ListBox},
        {L"ComboLBox", ExplorerSurface::ListBox},
        {L"ComboBox", ExplorerSurface::ComboBox},
        {L"Button", ExplorerSurface::Button},
        {L"SysListView32", ExplorerSurface::ListView},
        {L"SysTreeView32", ExplorerSurface::TreeView},
        {L"SysTabControl32", ExplorerSurface::Tab},
        {L"ScrollBar", ExplorerSurface::ScrollBar},
        {L"tooltips_class32", ExplorerSurface::Tooltip},
    };

    constexpr wchar_t Lower(wchar_t c) noexcept
    {
        return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c - L'A' + L'a') : c;
    }

    // Window class names are case-insensitive.
    bool SameClass(std::wstring_view a, std::wstring_view b) noexcept
    {
        if (a.size() != b.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            if (Lower(a[i]) != Lower(b[i]))
            {
                return false;
            }
        }
        return true;
    }
}

ExplorerSurface ClassifyWindowClass(std::wstring_view className) noexcept
{
    for (const ClassName& entry : kClasses)
    {
        if (SameClass(entry.name, className))
        {
            return entry.surface;
        }
    }
    return ExplorerSurface::Unknown;
}

SurfaceColours ColoursFor(ExplorerSurface surface) noexcept
{
    switch (surface)
    {
    case ExplorerSurface::Dialog:
    case ExplorerSurface::Button:
    case ExplorerSurface::Tab:
        return {COLOR_3DFACE, COLOR_BTNTEXT};
    case ExplorerSurface::Static:
        return {COLOR_3DFACE, COLOR_WINDOWTEXT};
    case ExplorerSurface::Edit:
    case ExplorerSurface::ListBox:
    case ExplorerSurface::ComboBox:
    case ExplorerSurface::ListView:
    case ExplorerSurface::TreeView:
        return {COLOR_WINDOW, COLOR_WINDOWTEXT};
    case ExplorerSurface::ScrollBar:
        return {COLOR_SCROLLBAR, kNoColour};
    case ExplorerSurface::Tooltip:
        return {COLOR_INFOBK, COLOR_INFOTEXT};
    case ExplorerSurface::Unknown:
        break;
    }
    return {kNoColour, kNoColour};
}

const wchar_t* DarkThemeClassFor(ExplorerSurface surface, bool dark) noexcept
{
    if (!dark)
    {
        return nullptr;
    }
    switch (surface)
    {
    case ExplorerSurface::ListView:
        return L"DarkMode_DarkTheme";
    case ExplorerSurface::TreeView:
    case ExplorerSurface::ScrollBar:
    case ExplorerSurface::Tooltip:
        return L"DarkMode_Explorer";
    case ExplorerSurface::Edit:
    case ExplorerSurface::ComboBox:
        return L"DarkMode_CFD";
    default:
        return nullptr;
    }
}
