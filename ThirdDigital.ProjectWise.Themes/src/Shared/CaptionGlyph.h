#pragma once

#include <windows.h>

// The largest square centred in `rect`.
RECT CentredSquare(const RECT& rect) noexcept;

// Paints the caption-button glyph `state` (DFCS_CAPTION* plus DFCS_PUSHED) centred in `rect` of `dc`
// in `colour`, leaving the pixels around the glyph untouched.
bool DrawCaptionGlyph(HDC dc, const RECT& rect, UINT state, COLORREF colour) noexcept;

// The window's small icon (WM_GETICON, then its class icons), or null. Same-thread windows only.
HICON WindowSmallIcon(HWND window) noexcept;
