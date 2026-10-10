#pragma once

#include "Shared/ActivePalette.h"

// Paints the caption and frame of MDI child windows owned by the calling (UI) thread in the palette's
// colours; DWM caption attributes do not reach child windows. Window rectangles and hit-testing stay
// with Windows. Call only after the window's creation has returned. A null palette leaves the frame to
// Windows and redraws it. A fault disables this slice only.
void ApplyChildCaption(HWND window, const PaletteResources* resources) noexcept;

bool IsMdiChild(HWND window) noexcept;
