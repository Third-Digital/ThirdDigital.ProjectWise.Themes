#include "PaintColours.h"
#include "Features/DependencyColours/DependencyColours.h"
#include "Features/HtmlReports/HtmlReports.h"
#include "Shared/ActivePalette.h"
#include "Shared/Log.h"
#include <MinHook.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <cwchar>
#include <iterator>
#include <initializer_list>

namespace
{
    thread_local unsigned t_paintDepth = 0;
    thread_local bool t_drawing = false;
    thread_local HWND t_buttonPaint = nullptr;
    thread_local HWND t_accessPaint = nullptr;
    bool g_installed = false;
    bool g_attempted = false;
    constexpr UINT_PTR kSubclass = 0x54444744;
    decltype(&::FillRect) g_fillRect = nullptr;
    decltype(&::DrawThemeParentBackground) g_parentBackground = nullptr;
    decltype(&::DrawFrameControl) g_frameControl = nullptr;
    decltype(&::DrawThemeParentBackgroundEx) g_parentBackgroundEx = nullptr;
    decltype(&::DrawTextW) g_drawText = nullptr;
    decltype(&::DrawTextExW) g_drawTextEx = nullptr;
    decltype(&::TextOutW) g_textOut = nullptr;
    decltype(&::ExtTextOutW) g_extTextOut = nullptr;
    decltype(&::DrawTextA) g_drawTextA = nullptr;
    decltype(&::DrawTextExA) g_drawTextExA = nullptr;
    decltype(&::TextOutA) g_textOutA = nullptr;
    decltype(&::ExtTextOutA) g_extTextOutA = nullptr;
    decltype(&::PatBlt) g_patBlt = nullptr;
    decltype(&::Rectangle) g_rectangle = nullptr;

    bool ColourDisplayDc(HDC dc) noexcept
    {
        const DWORD type = GetObjectType(dc);
        if (type == OBJ_METADC || type == OBJ_ENHMETADC || GetDeviceCaps(dc, TECHNOLOGY) != DT_RASDISPLAY) return false;
        BITMAP bitmap{};
        if (GetObjectW(GetCurrentObject(dc, OBJ_BITMAP), sizeof(bitmap), &bitmap) == sizeof(bitmap) && bitmap.bmBitsPixel == 1 && bitmap.bmPlanes == 1) return false;
        return true;
    }

    const PaletteResources* DrawingPalette(HDC dc) noexcept
    {
        // Printer/metafile output is never themed, nor is unrelated rendering on other UI threads.
        return t_paintDepth != 0 && !t_drawing && ColourDisplayDc(dc) ? ActivePalette() : nullptr;
    }

    COLORREF MapColour(COLORREF colour, const PaletteResources& p, bool text) noexcept
    {
        if (!text)
            for (int role : {COLOR_WINDOW, COLOR_3DFACE, COLOR_3DSHADOW, COLOR_HIGHLIGHT})
                if (p.palette.At(role) == colour) return colour;
        if ((colour & 0xff000000) != 0 || GetRValue(colour) != GetGValue(colour) || GetRValue(colour) != GetBValue(colour))
            return colour; // Keep semantic/accent colours, palette indices and sentinels.
        if (text) return *p.palette.At(COLOR_WINDOWTEXT);
        if (colour == RGB(255, 255, 255)) return *p.palette.At(COLOR_WINDOW);
        if (GetRValue(colour) >= 160) return *p.palette.At(COLOR_3DFACE);
        return *p.palette.At(COLOR_3DSHADOW);
    }

    // Changes last for this drawing call only. In particular a brush created/cached before the theme,
    // or during it, retains its original colour when the user selects None or another palette.
    class DrawingState
    {
    public:
        DrawingState(HDC dc, const PaletteResources& p, bool objects = false) noexcept : dc_(dc)
        {
            t_drawing = true;
            text_ = GetTextColor(dc);
            background_ = GetBkColor(dc);
            SetTextColor(dc, MapColour(text_, p, true));
            SetBkColor(dc, MapColour(background_, p, false));
            if (objects)
            {
                LOGBRUSH brush{};
                HGDIOBJ current = GetCurrentObject(dc, OBJ_BRUSH);
                if (GetObjectW(current, sizeof(brush), &brush) == sizeof(brush) && brush.lbStyle == BS_SOLID)
                {
                    if (current == GetStockObject(DC_BRUSH)) brush.lbColor = GetDCBrushColor(dc);
                    const COLORREF colour = MapColour(brush.lbColor, p, false);
                    if (colour != brush.lbColor)
                    {
                        brush_ = CreateSolidBrush(colour);
                        if (brush_ != nullptr) oldBrush_ = SelectObject(dc, brush_);
                    }
                }
                LOGPEN pen{};
                if (GetObjectW(GetCurrentObject(dc, OBJ_PEN), sizeof(pen), &pen) == sizeof(pen) && pen.lopnStyle != PS_NULL)
                {
                    const COLORREF colour = MapColour(pen.lopnColor, p, false);
                    if (colour != pen.lopnColor)
                    {
                        pen_ = CreatePen(pen.lopnStyle, pen.lopnWidth.x, colour);
                        if (pen_ != nullptr) oldPen_ = SelectObject(dc, pen_);
                    }
                }
            }
        }
        ~DrawingState()
        {
            SetTextColor(dc_, text_);
            SetBkColor(dc_, background_);
            if (oldBrush_ != nullptr && oldBrush_ != HGDI_ERROR) SelectObject(dc_, oldBrush_);
            if (oldPen_ != nullptr && oldPen_ != HGDI_ERROR) SelectObject(dc_, oldPen_);
            if (brush_ != nullptr) DeleteObject(brush_);
            if (pen_ != nullptr) DeleteObject(pen_);
            t_drawing = false;
        }
        DrawingState(const DrawingState&) = delete;
        DrawingState& operator=(const DrawingState&) = delete;
    private:
        HDC dc_;
        COLORREF text_, background_;
        HBRUSH brush_ = nullptr;
        HPEN pen_ = nullptr;
        HGDIOBJ oldBrush_ = nullptr, oldPen_ = nullptr;
    };

    int WINAPI Fill(HDC dc, const RECT* rect, HBRUSH brush)
    {
        const auto* p = DrawingPalette(dc);
        if (p == nullptr) return g_fillRect(dc, rect, brush);
        LOGBRUSH info{};
        if (reinterpret_cast<ULONG_PTR>(brush) <= COLOR_MENUBAR + 1)
        {
            HBRUSH themed = p->Brush(static_cast<int>(reinterpret_cast<ULONG_PTR>(brush)) - 1);
            return g_fillRect(dc, rect, themed != nullptr ? themed : brush);
        }
        if (GetObjectW(brush, sizeof(info), &info) != sizeof(info) || info.lbStyle != BS_SOLID) return g_fillRect(dc, rect, brush);
        if (brush == GetStockObject(DC_BRUSH)) info.lbColor = GetDCBrushColor(dc);
        const COLORREF colour = MapColour(info.lbColor, *p, false);
        if (colour == info.lbColor) return g_fillRect(dc, rect, brush);
        HBRUSH temporary = CreateSolidBrush(colour);
        if (temporary == nullptr) return g_fillRect(dc, rect, brush);
        t_drawing = true;
        const int result = g_fillRect(dc, rect, temporary);
        t_drawing = false;
        DeleteObject(temporary);
        return result;
    }
    int WINAPI Text(HDC dc, LPCWSTR text, int count, LPRECT rect, UINT format)
    {
        if (const auto* p = DrawingPalette(dc)) { DrawingState state(dc, *p); return g_drawText(dc, text, count, rect, format); }
        return g_drawText(dc, text, count, rect, format);
    }
    int WINAPI TextEx(HDC dc, LPWSTR text, int count, LPRECT rect, UINT format, LPDRAWTEXTPARAMS params)
    {
        if (const auto* p = DrawingPalette(dc)) { DrawingState state(dc, *p); return g_drawTextEx(dc, text, count, rect, format, params); }
        return g_drawTextEx(dc, text, count, rect, format, params);
    }
    BOOL WINAPI PlainText(HDC dc, int x, int y, LPCWSTR text, int count)
    {
        if (const auto* p = DrawingPalette(dc)) { DrawingState state(dc, *p); return g_textOut(dc, x, y, text, count); }
        return g_textOut(dc, x, y, text, count);
    }
    BOOL WINAPI ExtText(HDC dc, int x, int y, UINT flags, const RECT* rect, LPCWSTR text, UINT count, const INT* dx)
    {
        // MFC uses empty opaque text calls for alternating cell fills. Do not remap
        // its temporary disabled-text bitmaps or normal text drawing.
        if (t_accessPaint != nullptr && !t_drawing && count == 0 && (flags & ETO_OPAQUE) != 0 && ColourDisplayDc(dc))
            if (const auto* p = ActivePalette()) { DrawingState state(dc, *p); return g_extTextOut(dc, x, y, flags, rect, text, count, dx); }
        if (const auto* p = DrawingPalette(dc)) { DrawingState state(dc, *p); return g_extTextOut(dc, x, y, flags, rect, text, count, dx); }
        return g_extTextOut(dc, x, y, flags, rect, text, count, dx);
    }
    int WINAPI AnsiText(HDC dc, LPCSTR text, int count, LPRECT rect, UINT format)
    {
        if (const auto* p = DrawingPalette(dc)) { DrawingState state(dc, *p); return g_drawTextA(dc, text, count, rect, format); }
        return g_drawTextA(dc, text, count, rect, format);
    }
    int WINAPI AnsiTextEx(HDC dc, LPSTR text, int count, LPRECT rect, UINT format, LPDRAWTEXTPARAMS params)
    {
        if (const auto* p = DrawingPalette(dc)) { DrawingState state(dc, *p); return g_drawTextExA(dc, text, count, rect, format, params); }
        return g_drawTextExA(dc, text, count, rect, format, params);
    }
    BOOL WINAPI AnsiPlainText(HDC dc, int x, int y, LPCSTR text, int count)
    {
        if (const auto* p = DrawingPalette(dc)) { DrawingState state(dc, *p); return g_textOutA(dc, x, y, text, count); }
        return g_textOutA(dc, x, y, text, count);
    }
    BOOL WINAPI AnsiExtText(HDC dc, int x, int y, UINT flags, const RECT* rect, LPCSTR text, UINT count, const INT* dx)
    {
        if (t_accessPaint != nullptr && !t_drawing && count == 0 && (flags & ETO_OPAQUE) != 0 && ColourDisplayDc(dc))
            if (const auto* p = ActivePalette()) { DrawingState state(dc, *p); return g_extTextOutA(dc, x, y, flags, rect, text, count, dx); }
        if (const auto* p = DrawingPalette(dc)) { DrawingState state(dc, *p); return g_extTextOutA(dc, x, y, flags, rect, text, count, dx); }
        return g_extTextOutA(dc, x, y, flags, rect, text, count, dx);
    }
    BOOL WINAPI Pattern(HDC dc, int x, int y, int w, int h, DWORD operation)
    {
        if (const auto* p = DrawingPalette(dc); p != nullptr && operation == PATCOPY)
        { DrawingState state(dc, *p, true); return g_patBlt(dc, x, y, w, h, operation); }
        return g_patBlt(dc, x, y, w, h, operation);
    }
    BOOL WINAPI Rect(HDC dc, int left, int top, int right, int bottom)
    {
        if (const auto* p = DrawingPalette(dc)) { DrawingState state(dc, *p, true); return g_rectangle(dc, left, top, right, bottom); }
        return g_rectangle(dc, left, top, right, bottom);
    }

    bool LegacyButtonPaint() noexcept
    {
        const auto* p = ActivePalette();
        return t_buttonPaint != nullptr && p != nullptr && p->palette.isDark &&
            (GetWindowLongW(t_buttonPaint, GWL_STYLE) & BS_TYPEMASK) == BS_OWNERDRAW;
    }
    BOOL WINAPI FrameControl(HDC dc, LPRECT rect, UINT type, UINT state)
    {
        const auto* palette = ActivePalette();
        if (t_accessPaint != nullptr && palette != nullptr && ColourDisplayDc(dc) && type == DFC_BUTTON && ((state & 0x1f) == DFCS_BUTTONCHECK || (state & 0x1f) == DFCS_BUTTON3STATE))
        {
            const HWND access = t_accessPaint;
            t_accessPaint = nullptr;
            const unsigned depth = t_paintDepth; t_paintDepth = 0;
            BOOL result = FALSE;
            __try
            {
                if (palette->palette.isDark)
                {
                    if (HTHEME dark = OpenThemeData(nullptr, L"DarkMode_Explorer::Button"))
                    {
                        int checkState = (state & DFCS_INACTIVE) != 0 ? CBS_UNCHECKEDDISABLED :
                            (state & DFCS_PUSHED) != 0 ? CBS_UNCHECKEDPRESSED : (state & DFCS_HOT) != 0 ? CBS_UNCHECKEDHOT : CBS_UNCHECKEDNORMAL;
                        if ((state & DFCS_CHECKED) != 0) checkState += (state & 0x1f) == DFCS_BUTTON3STATE ? 8 : 4;
                        result = SUCCEEDED(DrawThemeBackground(dark, dc, BP_CHECKBOX, checkState, rect, nullptr));
                        CloseThemeData(dark);
                    }
                }
                if (!result) result = g_frameControl(dc, rect, type, state);
            }
            __finally { t_accessPaint = access; t_paintDepth = depth; }
            return result;
        }
        if (LegacyButtonPaint() && type == DFC_BUTTON && (state & 0x1f) == DFCS_BUTTONPUSH && GetDeviceCaps(dc, TECHNOLOGY) == DT_RASDISPLAY)
        {
            if (HTHEME dark = OpenThemeData(t_buttonPaint, L"Button"))
            {
                const int buttonState = (state & DFCS_INACTIVE) != 0 ? PBS_DISABLED : (state & DFCS_PUSHED) != 0 ? PBS_PRESSED : (state & DFCS_HOT) != 0 ? PBS_HOT : PBS_NORMAL;
                const HRESULT result = DrawThemeBackground(dark, dc, BP_PUSHBUTTON, buttonState, rect, nullptr);
                CloseThemeData(dark);
                if (SUCCEEDED(result))
                {
                    if ((state & DFCS_ADJUSTRECT) != 0) InflateRect(rect, -2, -2);
                    return TRUE;
                }
            }
        }
        return g_frameControl(dc, rect, type, state);
    }
    bool PaintButtonBackground(HWND window, HDC dc, const RECT* bounds) noexcept
    {
        const auto* p = ActivePalette();
        if (p == nullptr || t_buttonPaint != window || GetDeviceCaps(dc, TECHNOLOGY) != DT_RASDISPLAY) return false;
        RECT client{};
        if (bounds == nullptr) { GetClientRect(window, &client); bounds = &client; }
        g_fillRect(dc, bounds, p->Brush(COLOR_3DFACE));
        return true;
    }
    HRESULT WINAPI ParentBackground(HWND window, HDC dc, const RECT* rect)
    {
        return PaintButtonBackground(window, dc, rect) ? S_OK : g_parentBackground(window, dc, rect);
    }
    HRESULT WINAPI ParentBackgroundEx(HWND window, HDC dc, DWORD flags, const RECT* rect)
    {
        return PaintButtonBackground(window, dc, rect) ? S_OK : g_parentBackgroundEx(window, dc, flags, rect);
    }

    bool PaintPlainLabel(HWND window, UINT message, WPARAM w, LPARAM l)
    {
        const auto* p = ActivePalette();
        if (p == nullptr || (message != WM_PAINT && message != WM_PRINTCLIENT && message != WM_PRINT)) return false;
        if (message == WM_PRINT && (l & PRF_CLIENT) == 0) return false;
        if (message != WM_PAINT && GetDeviceCaps(reinterpret_cast<HDC>(w), TECHNOLOGY) != DT_RASDISPLAY) return false;
        wchar_t name[32]{}, parentClass[32]{};
        GetClassNameW(window, name, ARRAYSIZE(name));
        GetClassNameW(GetParent(window), parentClass, ARRAYSIZE(parentClass));
        const LONG style = GetWindowLongW(window, GWL_STYLE);
        if (_wcsicmp(name, L"Static") != 0 || (style & SS_TYPEMASK) != SS_LEFTNOWORDWRAP || wcscmp(parentClass, L"#32770") != 0) return false;
        // Bentley's attribute labels bypass standard control-colour handling. Preserve their text,
        // font, bounds and single-line layout while drawing the two palette colours explicitly.
        wchar_t text[1024]{};
        if (GetWindowTextLengthW(window) >= static_cast<int>(ARRAYSIZE(text))) return false;
        GetWindowTextW(window, text, ARRAYSIZE(text));
        PAINTSTRUCT paint{};
        HDC dc = message == WM_PAINT ? BeginPaint(window, &paint) : reinterpret_cast<HDC>(w);
        if (dc == nullptr) return false;
        const int saved = SaveDC(dc);
        if (saved == 0) { if (message == WM_PAINT) EndPaint(window, &paint); return false; }
        RECT rect{}; GetClientRect(window, &rect);
        FillRect(dc, &rect, p->Brush(COLOR_3DFACE));
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
        if (font != nullptr) SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, *p->palette.At(IsWindowEnabled(window) ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT));
        UINT flags = DT_LEFT | DT_SINGLELINE | DT_EXPANDTABS;
        if ((style & SS_NOPREFIX) != 0) flags |= DT_NOPREFIX;
        if ((style & SS_CENTERIMAGE) != 0) flags |= DT_VCENTER;
        g_drawText(dc, text, -1, &rect, flags);
        if (saved != 0) RestoreDC(dc, saved);
        if (message == WM_PAINT) EndPaint(window, &paint);
        return true;
    }

    constexpr wchar_t kLegacyProcedure[] = L"ThirdDigital.ProjectWise.Themes.LegacyPaintProcedure";
    LRESULT CALLBACK LegacyPaintProc(HWND window, UINT message, WPARAM w, LPARAM l)
    {
        const auto original = reinterpret_cast<WNDPROC>(GetPropW(window, kLegacyProcedure));
        if (message == WM_NCDESTROY)
        {
            RemovePropW(window, kLegacyProcedure);
            return original != nullptr ? CallWindowProcW(original, window, message, w, l) : DefWindowProcW(window, message, w, l);
        }
        if (PaintPlainLabel(window, message, w, l)) return 0;
        const unsigned previousDepth = t_paintDepth;
        const HWND previousButton = t_buttonPaint;
        if ((GetWindowLongW(window, GWL_STYLE) & BS_TYPEMASK) == BS_OWNERDRAW) t_buttonPaint = window;
        if (message == WM_PAINT || message == WM_PRINT || message == WM_PRINTCLIENT || message == WM_ERASEBKGND) ++t_paintDepth;
        __try { return original != nullptr ? CallWindowProcW(original, window, message, w, l) : DefWindowProcW(window, message, w, l); }
        __finally { t_paintDepth = previousDepth; t_buttonPaint = previousButton; }
    }

    void ApplyLegacyPaint(HWND window)
    {
        wchar_t name[32]{}, parentClass[32]{};
        GetClassNameW(window, name, ARRAYSIZE(name));
        GetClassNameW(GetParent(window), parentClass, ARRAYSIZE(parentClass));
        const LONG style = GetWindowLongW(window, GWL_STYLE);
        const bool label = _wcsicmp(name, L"Static") == 0 && (style & SS_TYPEMASK) == SS_LEFTNOWORDWRAP;
        const bool button = _wcsicmp(name, L"Button") == 0 && (style & BS_TYPEMASK) == BS_OWNERDRAW;
        if ((!label && !button) || wcscmp(parentClass, L"#32770") != 0 || GetPropW(window, kLegacyProcedure) != nullptr) return;
        // Bentley's legacy WNDPROC can consume paint without forwarding to the comctl subclass chain.
        // Attach outside it only after initialization, and delegate all non-paint messages unchanged.
        const auto original = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
        if (original != nullptr && SetPropW(window, kLegacyProcedure, reinterpret_cast<HANDLE>(original)))
        {
            if (SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(LegacyPaintProc)) == 0)
                RemovePropW(window, kLegacyProcedure);
            else InvalidateRect(window, nullptr, FALSE);
        }
    }

    LRESULT CALLBACK PaintProc(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR)
    {
        if (message == WM_NCDESTROY) RemoveWindowSubclass(window, PaintProc, id);
        if (PaintPlainLabel(window, message, w, l)) return 0;
        const bool drawButton = message == WM_DRAWITEM && l != 0 && reinterpret_cast<const DRAWITEMSTRUCT*>(l)->CtlType == ODT_BUTTON;
        if (!drawButton && message != WM_PAINT && message != WM_PRINT && message != WM_PRINTCLIENT && message != WM_ERASEBKGND)
            return DefSubclassProc(window, message, w, l);
        const unsigned previousDepth = t_paintDepth;
        wchar_t name[32]{};
        GetClassNameW(window, name, ARRAYSIZE(name));
        const bool standardButton = _wcsicmp(name, L"Button") == 0 &&
            (GetWindowLongW(window, GWL_STYLE) & BS_TYPEMASK) != BS_GROUPBOX;
        const HWND previousButton = t_buttonPaint;
        t_buttonPaint = standardButton ? window : nullptr;
        const HWND previousAccess = t_accessPaint;
        const bool access = IsAccessControlWindow(window);
        t_accessPaint = access ? window : nullptr;
        t_paintDepth = standardButton || (access && _wcsicmp(name, WC_HEADERW) != 0) ? 0 : previousDepth + 1;
        __try { return DefSubclassProc(window, message, w, l); }
        __finally { t_paintDepth = previousDepth; t_buttonPaint = previousButton; t_accessPaint = previousAccess; }
    }
}

bool IsAccessControlWindow(HWND window) noexcept
{
    wchar_t name[80]{};
    for (HWND current = window; current != nullptr; current = GetParent(current))
        if (GetClassNameW(current, name, ARRAYSIZE(name)) != 0 && wcscmp(name, L"PWUserAccessWindowClass") == 0) return true;
    return false;
}

bool IsPaintColourWindow(HWND window) noexcept
{
    wchar_t name[80]{};
    if (GetClassNameW(window, name, ARRAYSIZE(name)) == 0) return false;
    if (IsAccessControlWindow(window) || IsDependencyColourWindow(window)) return true;
    if (_wcsicmp(name, L"Button") == 0) return true; // Native buttons keep their glyph rendering; their backing is handled separately.
    return _wcsicmp(name, L"Internet Explorer_Server") == 0 || _wcsicmp(name, L"ProjectPreviewCtrlView") == 0 || _wcsicmp(name, L"#32770") == 0 ||
        _wcsicmp(name, L"Edit") == 0 || _wcsicmp(name, WC_TABCONTROLW) == 0 || _wcsicmp(name, WC_HEADERW) == 0 || _wcsicmp(name, L"Static") == 0;
}

void ApplyPaintColours(HWND window) noexcept
{
    if (g_installed && IsPaintColourWindow(window))
    {
        wchar_t name[64]{}; GetClassNameW(window, name, ARRAYSIZE(name));
        if (wcscmp(name, L"Internet Explorer_Server") == 0) { ApplyHtmlReportColours(window); return; }
        SetWindowSubclass(window, PaintProc, kSubclass, 0);
        ApplyLegacyPaint(window);
        ApplyDependencyColours(window);
    }
}

bool InstallPaintColourHooks() noexcept
{
    if (g_installed) return true;
    if (g_attempted) return false;
    g_attempted = true;
    struct Spec { const wchar_t* dll; const char* name; void* replacement; void** original; };
    const Spec specs[] = {
        {L"user32.dll", "DrawFrameControl", reinterpret_cast<void*>(FrameControl), reinterpret_cast<void**>(&g_frameControl)},
        {L"uxtheme.dll", "DrawThemeParentBackground", reinterpret_cast<void*>(ParentBackground), reinterpret_cast<void**>(&g_parentBackground)},
        {L"uxtheme.dll", "DrawThemeParentBackgroundEx", reinterpret_cast<void*>(ParentBackgroundEx), reinterpret_cast<void**>(&g_parentBackgroundEx)},
        {L"user32.dll", "FillRect", reinterpret_cast<void*>(Fill), reinterpret_cast<void**>(&g_fillRect)},
        {L"user32.dll", "DrawTextW", reinterpret_cast<void*>(Text), reinterpret_cast<void**>(&g_drawText)},
        {L"user32.dll", "DrawTextExW", reinterpret_cast<void*>(TextEx), reinterpret_cast<void**>(&g_drawTextEx)},
        {L"gdi32.dll", "TextOutW", reinterpret_cast<void*>(PlainText), reinterpret_cast<void**>(&g_textOut)},
        {L"gdi32.dll", "ExtTextOutW", reinterpret_cast<void*>(ExtText), reinterpret_cast<void**>(&g_extTextOut)},
        {L"user32.dll", "DrawTextA", reinterpret_cast<void*>(AnsiText), reinterpret_cast<void**>(&g_drawTextA)},
        {L"user32.dll", "DrawTextExA", reinterpret_cast<void*>(AnsiTextEx), reinterpret_cast<void**>(&g_drawTextExA)},
        {L"gdi32.dll", "TextOutA", reinterpret_cast<void*>(AnsiPlainText), reinterpret_cast<void**>(&g_textOutA)},
        {L"gdi32.dll", "ExtTextOutA", reinterpret_cast<void*>(AnsiExtText), reinterpret_cast<void**>(&g_extTextOutA)},
        {L"gdi32.dll", "PatBlt", reinterpret_cast<void*>(Pattern), reinterpret_cast<void**>(&g_patBlt)},
        {L"gdi32.dll", "Rectangle", reinterpret_cast<void*>(Rect), reinterpret_cast<void**>(&g_rectangle)},
    };
    void* targets[std::size(specs)]{};
    for (std::size_t i = 0; i < std::size(specs); ++i)
    {
        const auto& s = specs[i];
        if (MH_CreateHookApiEx(s.dll, s.name, s.replacement, s.original, &targets[i]) != MH_OK)
        {
            for (std::size_t j = 0; j < i; ++j) MH_RemoveHook(targets[j]);
            Log(L"paint colours: could not prepare %S", s.name);
            return false;
        }
    }
    for (void* target : targets)
    {
        if (MH_EnableHook(target) != MH_OK)
        {
            for (void* other : targets) MH_DisableHook(other);
            Log(L"paint colours: installation failed; fixed-colour grid left stock");
            return false;
        }
    }
    g_installed = true;
    return true;
}
