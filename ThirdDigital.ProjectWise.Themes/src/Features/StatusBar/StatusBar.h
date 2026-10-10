#pragma once

#include "Shared/ActivePalette.h"

// MFC status bars draw disabled panes with GRAYTEXT on a uxtheme STATUS background. While one of this
// module's subclassed status bars (class names starting "Afx:StatusBar") is painting, detours turn
// GRAYTEXT into BTNTEXT and fill the STATUS background and panes with 3DFACE. Install after the colour
// hooks; a failure disables this slice only. UI thread only.
bool InstallStatusBarHook() noexcept;

// Call only after the window's creation has returned.
void ApplyStatusBar(HWND window, const PaletteResources* resources) noexcept;

bool IsStatusBarClass(HWND window) noexcept;
