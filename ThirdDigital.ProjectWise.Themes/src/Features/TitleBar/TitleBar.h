#pragma once

#include "Shared/ActivePalette.h"

// Dark palettes on Windows 11 (build 22000+) set DWM dark mode and caption, border and text colours on
// top-level windows owned by the UI thread. Anything else resets only windows this module changed.
void ApplyTitleBar(HWND window, const PaletteResources* resources) noexcept;

void ForgetTitleBar(HWND window) noexcept;
