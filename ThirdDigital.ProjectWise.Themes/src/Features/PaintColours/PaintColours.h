#pragma once
#include <windows.h>
// Drawing-time palette for supported native controls; cached objects remain unchanged.
bool InstallPaintColourHooks() noexcept;
bool IsPaintColourWindow(HWND window) noexcept;
void ApplyPaintColours(HWND window) noexcept;

// Bentley native Access Control host and its descendants.
bool IsAccessControlWindow(HWND window) noexcept;
