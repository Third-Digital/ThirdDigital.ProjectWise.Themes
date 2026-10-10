#pragma once

#include <string_view>

enum class ExplorerSurface
{
    Unknown,
    Dialog,
    Static,
    Edit,
    ListBox,
    ComboBox,
    Button,
    ListView,
    TreeView,
    Tab,
    ScrollBar,
    Tooltip,
};

// System-colour indices (COLOR_*) resolved through the active palette; kNoColour when the surface has no such role.
struct SurfaceColours
{
    int background;
    int text;
};

constexpr int kNoColour = -1;

ExplorerSurface ClassifyWindowClass(std::wstring_view className) noexcept;
SurfaceColours ColoursFor(ExplorerSurface surface) noexcept;

// Null means leave the window's theme association alone.
const wchar_t* DarkThemeClassFor(ExplorerSurface surface, bool dark) noexcept;
