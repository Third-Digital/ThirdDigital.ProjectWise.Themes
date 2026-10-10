#include "CaptionGlyph.h"

#include <algorithm>
#include <cwchar>

RECT CentredSquare(const RECT& rect) noexcept
{
    const int size = (std::min)(rect.right - rect.left, rect.bottom - rect.top);
    RECT square{0, 0, size, size};
    OffsetRect(&square, rect.left + (rect.right - rect.left - size) / 2, rect.top + (rect.bottom - rect.top - size) / 2);
    return square;
}

bool DrawCaptionGlyph(HDC dc, const RECT& rect, UINT state, COLORREF colour) noexcept
{
    // The Marlett characters DrawFrameControl draws its caption glyphs from. DrawFrameControl itself paints
    // stock colours, and calling it for DFC_CAPTION leaves themed MDI child frames drawn classic.
    wchar_t glyph = 0;
    switch (state & 0x000F)
    {
    case DFCS_CAPTIONCLOSE:
        glyph = L'r';
        break;
    case DFCS_CAPTIONMIN:
        glyph = L'0';
        break;
    case DFCS_CAPTIONMAX:
        glyph = L'1';
        break;
    case DFCS_CAPTIONRESTORE:
        glyph = L'2';
        break;
    default:
        return false;
    }
    const int size = (std::min)(rect.right - rect.left, rect.bottom - rect.top);
    if (size <= 0)
    {
        return false;
    }
    LOGFONTW logFont{};
    // Matches the glyph size Windows draws on a button of this size.
    logFont.lfHeight = size * 9 / 8;
    logFont.lfCharSet = SYMBOL_CHARSET;
    logFont.lfQuality = NONANTIALIASED_QUALITY;
    wcscpy_s(logFont.lfFaceName, L"Marlett");
    HFONT font = CreateFontIndirectW(&logFont);
    if (font == nullptr)
    {
        return false;
    }
    RECT area = rect;
    if ((state & DFCS_PUSHED) != 0)
    {
        OffsetRect(&area, 1, 1);
    }
    HGDIOBJ previousFont = SelectObject(dc, font);
    const int mode = SetBkMode(dc, TRANSPARENT);
    const COLORREF previous = SetTextColor(dc, colour);
    const bool drawn = DrawTextW(dc, &glyph, 1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP | DT_NOPREFIX) != 0;
    SetTextColor(dc, previous);
    SetBkMode(dc, mode);
    SelectObject(dc, previousFont);
    DeleteObject(font);
    return drawn;
}

HICON WindowSmallIcon(HWND window) noexcept
{
    if (window == nullptr || !IsWindow(window) || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId())
    {
        return nullptr;
    }
    auto icon = reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, ICON_SMALL2, 0));
    if (icon == nullptr)
    {
        icon = reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICONSM));
    }
    if (icon == nullptr)
    {
        icon = reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICON));
    }
    return icon;
}
