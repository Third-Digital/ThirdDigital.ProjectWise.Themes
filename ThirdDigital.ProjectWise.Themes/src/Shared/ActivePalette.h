#pragma once

#include "Domain/SystemColourMapping.h"

#include <array>

struct PaletteResources
{
    const ColourScheme* scheme = nullptr;
    SystemColourPalette palette;
    std::array<HBRUSH, COLOR_MENUBAR + 1> brushes{};

    // Same colours as `brushes` but distinct handles: comctl32 controls treat a WM_CTLCOLOR* brush equal to
    // GetSysColorBrush(role) as "not customised" and paint their visual-style fill instead.
    std::array<HBRUSH, COLOR_MENUBAR + 1> controlBrushes{};

    HBRUSH Brush(int index) const noexcept
    {
        return At(brushes, index);
    }

    HBRUSH ControlBrush(int index) const noexcept
    {
        return At(controlBrushes, index);
    }

private:
    static HBRUSH At(const std::array<HBRUSH, COLOR_MENUBAR + 1>& set, int index) noexcept
    {
        if (index < 0 || index >= static_cast<int>(set.size()))
        {
            return nullptr;
        }
        return set[static_cast<std::size_t>(index)];
    }
};

// UI thread only. Returns the cached resources for a scheme from the scheme table, or null if any
// brush could not be created. Published resources are never freed, so detours may keep using them.
const PaletteResources* PreparePalette(const ColourScheme& scheme) noexcept;

const PaletteResources* ActivePalette() noexcept;
void PublishPalette(const PaletteResources* resources) noexcept;

namespace PaletteSeams
{
    using CreateBrush = HBRUSH(WINAPI*)(COLORREF);
    using DeleteGdiObject = BOOL(WINAPI*)(HGDIOBJ);
    extern CreateBrush createSolidBrush;
    extern DeleteGdiObject deleteObject;
}
