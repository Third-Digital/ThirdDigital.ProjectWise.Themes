#pragma once

#include "Shared/ActivePalette.h"

// Paints the Win32 menu bar of top-level windows owned by the calling (UI) thread in the palette's
// colours through the undocumented UAH menu messages. Call only after the window's creation has
// returned. A null palette leaves the bar to Windows and redraws it. A fault disables this slice only.
void ApplyMenuBar(HWND window, const PaletteResources* resources) noexcept;
