#pragma once

#include "ColourScheme.h"

#include <array>
#include <optional>

struct SystemColourPalette
{
    std::array<std::optional<COLORREF>, COLOR_MENUBAR + 1> colours{};
    bool isDark = false;

    std::optional<COLORREF> At(int index) const noexcept
    {
        if (index < 0 || index >= static_cast<int>(colours.size()))
        {
            return std::nullopt;
        }
        return colours[static_cast<std::size_t>(index)];
    }
};

SystemColourPalette BuildPalette(const ColourScheme& scheme) noexcept;

// Channel-wise sRGB interpolation from `from` (weight 0) to `to` (weight 1), rounded half away from zero.
COLORREF Mix(COLORREF from, COLORREF to, double weight) noexcept;

double RelativeLuminance(COLORREF colour) noexcept;
double Contrast(COLORREF first, COLORREF second) noexcept;
