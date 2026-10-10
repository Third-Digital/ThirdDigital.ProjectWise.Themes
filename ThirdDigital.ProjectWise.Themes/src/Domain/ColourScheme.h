#pragma once

#include <windows.h>

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

enum class Ansi : std::size_t
{
    Black,
    Red,
    Green,
    Yellow,
    Blue,
    Purple,
    Cyan,
    White,
    BrightBlack,
    BrightRed,
    BrightGreen,
    BrightYellow,
    BrightBlue,
    BrightPurple,
    BrightCyan,
    BrightWhite,
};

struct ColourScheme
{
    const wchar_t* name;
    COLORREF background;
    COLORREF foreground;
    COLORREF cursor;
    std::array<COLORREF, 16> ansi;

    constexpr COLORREF Colour(Ansi index) const noexcept
    {
        return ansi[static_cast<std::size_t>(index)];
    }
};

std::span<const ColourScheme> Schemes() noexcept;

// Exact, case-sensitive match: the registry stores the name exactly as shown in the menu.
const ColourScheme* FindScheme(std::wstring_view name) noexcept;
