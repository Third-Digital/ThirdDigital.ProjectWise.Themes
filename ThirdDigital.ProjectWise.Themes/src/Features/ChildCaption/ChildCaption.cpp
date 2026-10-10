#include "ChildCaption.h"

#include "Shared/CaptionGlyph.h"
#include "Shared/Log.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <cwchar>
#include <vector>

namespace
{
    constexpr UINT_PTR kSubclassId = 0x54445043;
    // Undocumented: sent by DefWindowProc to redraw a themed caption or frame outside WM_NCPAINT.
    constexpr UINT WM_NCUAHDRAWCAPTION = 0x00AE;
    constexpr UINT WM_NCUAHDRAWFRAME = 0x00AF;

    constexpr int kButtonCount = 3;
    constexpr int kTitleBarButton[kButtonCount] = {2, 3, 5};
    constexpr LRESULT kButtonHit[kButtonCount] = {HTMINBUTTON, HTMAXBUTTON, HTCLOSE};

    // Caption buttons as Windows reports them, in window coordinates for a window `width` wide. Inside
    // non-client messages a themed child reports classic button positions, so this is read outside them.
    struct Buttons
    {
        bool valid;
        int width;
        RECT rects[kButtonCount];
        bool shown[kButtonCount];
        bool available[kButtonCount];
    };

    struct Child
    {
        HWND window;
        LRESULT pressed;
        bool pushed;
        bool queried;
        Buttons buttons;
    };

    bool g_disabled = false;
    UINT g_queryButtons = 0;

    // UI thread only.
    std::vector<Child> g_children;

    Child* Find(HWND window) noexcept
    {
        for (Child& c : g_children)
        {
            if (c.window == window)
            {
                return &c;
            }
        }
        return nullptr;
    }

    void QueryButtons(HWND window, Buttons& buttons) noexcept
    {
        buttons.valid = false;
        RECT screen{};
        TITLEBARINFOEX info{sizeof(info)};
        if (!GetWindowRect(window, &screen))
        {
            return;
        }
        SendMessageW(window, WM_GETTITLEBARINFOEX, 0, reinterpret_cast<LPARAM>(&info));
        if (IsRectEmpty(&info.rcTitleBar))
        {
            return;
        }
        buttons.width = screen.right - screen.left;
        for (int i = 0; i < kButtonCount; ++i)
        {
            const DWORD state = info.rgstate[kTitleBarButton[i]];
            buttons.shown[i] = (state & STATE_SYSTEM_INVISIBLE) == 0 && !IsRectEmpty(&info.rgrect[kTitleBarButton[i]]);
            buttons.available[i] = (state & STATE_SYSTEM_UNAVAILABLE) == 0;
            buttons.rects[i] = info.rgrect[kTitleBarButton[i]];
            OffsetRect(&buttons.rects[i], -screen.left, -screen.top);
        }
        buttons.valid = true;
    }

    // Window coordinates. Everything outside `inner` is painted here; inside it Windows paints.
    struct Layout
    {
        RECT window;
        RECT inner;
        RECT caption;
        Buttons buttons;
    };

    bool ComputeLayout(HWND window, const Buttons& buttons, Layout& layout) noexcept
    {
        const LONG style = GetWindowLongW(window, GWL_STYLE);
        const LONG exStyle = GetWindowLongW(window, GWL_EXSTYLE);
        // Captions this slice cannot reproduce exactly stay with Windows.
        if (!buttons.valid || (style & WS_CAPTION) != WS_CAPTION || IsZoomed(window) || IsIconic(window) ||
            (exStyle & (WS_EX_TOOLWINDOW | WS_EX_LAYOUTRTL)) != 0 || GetDpiForWindow(window) != GetDpiForSystem())
        {
            return false;
        }
        RECT screen{};
        RECT insets{};
        if (!GetWindowRect(window, &screen) ||
            !AdjustWindowRectEx(&insets, static_cast<DWORD>(style) & ~(WS_VSCROLL | WS_HSCROLL), FALSE,
                                static_cast<DWORD>(exStyle) & ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE)))
        {
            return false;
        }
        const int width = screen.right - screen.left;
        const int height = screen.bottom - screen.top;
        layout.window = {0, 0, width, height};
        layout.inner = {-insets.left, -insets.top, width - insets.right, height - insets.bottom};
        // rcTitleBar of a child window starts past the icon, so the caption is placed from the frame metrics.
        layout.caption = {layout.inner.left, layout.inner.top - GetSystemMetrics(SM_CYCAPTION), layout.inner.right, layout.inner.top};
        if (IsRectEmpty(&layout.inner) || layout.caption.top < 0)
        {
            return false;
        }
        // The buttons keep their place relative to the right edge as the window is resized.
        layout.buttons = buttons;
        for (RECT& rect : layout.buttons.rects)
        {
            OffsetRect(&rect, width - buttons.width, 0);
        }
        return true;
    }

    void Fill(HDC dc, const RECT& rect, const PaletteResources& p, int index) noexcept
    {
        HBRUSH brush = p.Brush(index);
        if (brush != nullptr && GetObjectType(brush) == OBJ_BRUSH)
        {
            FillRect(dc, &rect, brush);
        }
    }

    void DrawCaptionText(HDC dc, HWND window, RECT area, COLORREF colour) noexcept
    {
        wchar_t title[256]{};
        if (area.right <= area.left || GetWindowTextW(window, title, ARRAYSIZE(title)) <= 0)
        {
            return;
        }
        NONCLIENTMETRICSW metrics{sizeof(metrics)};
        if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
        {
            return;
        }
        HFONT font = CreateFontIndirectW(&metrics.lfCaptionFont);
        if (font == nullptr)
        {
            return;
        }
        HGDIOBJ previousFont = SelectObject(dc, font);
        const int mode = SetBkMode(dc, TRANSPARENT);
        const COLORREF previous = SetTextColor(dc, colour);
        DrawTextW(dc, title, -1, &area, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        SetTextColor(dc, previous);
        SetBkMode(dc, mode);
        SelectObject(dc, previousFont);
        DeleteObject(font);
    }

    UINT GlyphFor(HWND window, int button) noexcept
    {
        switch (button)
        {
        case 0:
            return DFCS_CAPTIONMIN;
        case 1:
            return IsZoomed(window) ? DFCS_CAPTIONRESTORE : DFCS_CAPTIONMAX;
        default:
            return DFCS_CAPTIONCLOSE;
        }
    }

    void Paint(HWND window, const Layout& layout, const PaletteResources& p, bool active, const Child* child) noexcept
    {
        HDC dc = GetWindowDC(window);
        if (dc == nullptr)
        {
            return;
        }
        ExcludeClipRect(dc, layout.inner.left, layout.inner.top, layout.inner.right, layout.inner.bottom);
        Fill(dc, layout.window, p, active ? COLOR_ACTIVEBORDER : COLOR_INACTIVEBORDER);
        const int captionColour = active ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION;
        Fill(dc, layout.caption, p, captionColour);

        RECT text = layout.caption;
        text.left += 4;
        if ((GetWindowLongW(window, GWL_STYLE) & WS_SYSMENU) != 0)
        {
            if (HICON icon = WindowSmallIcon(window))
            {
                const int cx = GetSystemMetrics(SM_CXSMICON);
                const int cy = GetSystemMetrics(SM_CYSMICON);
                const int top = layout.caption.top + (layout.caption.bottom - layout.caption.top - cy) / 2;
                DrawIconEx(dc, layout.caption.left + 2, top, icon, cx, cy, 0, nullptr, DI_NORMAL);
                text.left = layout.caption.left + 2 + cx + 4;
            }
        }
        const COLORREF textColour = *p.palette.At(active ? COLOR_CAPTIONTEXT : COLOR_INACTIVECAPTIONTEXT);
        for (int i = 0; i < kButtonCount; ++i)
        {
            if (!layout.buttons.shown[i])
            {
                continue;
            }
            const RECT& button = layout.buttons.rects[i];
            text.right = (std::min)(text.right, button.left - 4);
            const bool pushed = child != nullptr && child->pushed && child->pressed == kButtonHit[i];
            Fill(dc, button, p, pushed ? COLOR_MENUHILIGHT : captionColour);
            const COLORREF colour = layout.buttons.available[i] ? textColour : *p.palette.At(COLOR_GRAYTEXT);
            DrawCaptionGlyph(dc, CentredSquare(button), GlyphFor(window, i) | (pushed ? DFCS_PUSHED : 0), colour);
        }
        DrawCaptionText(dc, window, text, textColour);
        ReleaseDC(window, dc);
    }

    bool ActiveCaption(HWND window) noexcept
    {
        WINDOWINFO info{sizeof(info)};
        return GetWindowInfo(window, &info) && (info.dwWindowStatus & WS_ACTIVECAPTION) != 0;
    }

    // Each returns false after a fault, which disables the slice.
    bool GuardedQuery(HWND window, Buttons& buttons) noexcept
    {
        __try
        {
            QueryButtons(window, buttons);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool GuardedLayout(HWND window, Layout& layout, bool& usable) noexcept
    {
        __try
        {
            const Child* child = Find(window);
            usable = child != nullptr && ComputeLayout(window, child->buttons, layout);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool GuardedPaint(HWND window, const Layout& layout, const PaletteResources& p, bool active) noexcept
    {
        __try
        {
            Paint(window, layout, p, active, Find(window));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool GuardedRegion(HWND window, WPARAM update, const Layout& layout, HRGN& region) noexcept
    {
        __try
        {
            region = nullptr;
            RECT screen{};
            if (!GetWindowRect(window, &screen))
            {
                return true;
            }
            RECT inner = layout.inner;
            OffsetRect(&inner, screen.left, screen.top);
            HRGN created = CreateRectRgnIndirect(&inner);
            if (created != nullptr && update > 1 && CombineRgn(created, created, reinterpret_cast<HRGN>(update), RGN_AND) == ERROR)
            {
                DeleteObject(created);
                created = nullptr;
            }
            region = created;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void Disable() noexcept
    {
        if (g_disabled)
        {
            return;
        }
        g_disabled = true;
        Log(L"child caption: fault while painting; MDI child frames left stock");
        for (const Child& c : g_children)
        {
            if (c.pressed != 0 && GetCapture() == c.window)
            {
                ReleaseCapture();
            }
            RedrawWindow(c.window, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
        }
    }

    bool Repaint(HWND window, const PaletteResources& p, bool active) noexcept
    {
        Layout layout{};
        bool usable = false;
        if (!GuardedLayout(window, layout, usable) || (usable && !GuardedPaint(window, layout, p, active)))
        {
            Disable();
            return false;
        }
        return usable;
    }

    LRESULT NcPaint(HWND window, WPARAM wParam, LPARAM lParam, const PaletteResources& p) noexcept
    {
        Layout layout{};
        bool usable = false;
        HRGN region = nullptr;
        if (!GuardedLayout(window, layout, usable) || (usable && !GuardedRegion(window, wParam, layout, region)))
        {
            Disable();
            return DefSubclassProc(window, WM_NCPAINT, wParam, lParam);
        }
        if (!usable)
        {
            return DefSubclassProc(window, WM_NCPAINT, wParam, lParam);
        }
        const LRESULT result = DefSubclassProc(window, WM_NCPAINT, region != nullptr ? reinterpret_cast<WPARAM>(region) : wParam, lParam);
        if (region != nullptr)
        {
            DeleteObject(region);
        }
        if (!GuardedPaint(window, layout, p, ActiveCaption(window)))
        {
            Disable();
        }
        return result;
    }

    POINT ScreenPoint(HWND window, LPARAM lParam) noexcept
    {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ClientToScreen(window, &point);
        return point;
    }

    WPARAM CommandFor(HWND window, LRESULT hit) noexcept
    {
        if (hit == HTCLOSE)
        {
            return SC_CLOSE;
        }
        if (hit == HTMINBUTTON)
        {
            return SC_MINIMIZE;
        }
        return IsZoomed(window) ? SC_RESTORE : SC_MAXIMIZE;
    }

    // Windows' own button tracking draws stock pressed buttons, so presses on the caption buttons are
    // tracked here; hit-testing and the resulting system command stay with Windows.
    bool TrackButton(HWND window, UINT message, WPARAM wParam, LPARAM lParam, const PaletteResources& p, LRESULT& result) noexcept
    {
        Child* child = Find(window);
        if (child == nullptr)
        {
            return false;
        }
        if (message == WM_NCLBUTTONDOWN)
        {
            Layout layout{};
            bool usable = false;
            const LRESULT hit = static_cast<LRESULT>(wParam);
            const auto index = std::find(std::begin(kButtonHit), std::end(kButtonHit), hit) - std::begin(kButtonHit);
            if (index >= kButtonCount || !GuardedLayout(window, layout, usable) || !usable || !layout.buttons.shown[index] || !layout.buttons.available[index])
            {
                return false;
            }
            child->pressed = hit;
            child->pushed = true;
            SetCapture(window);
            Repaint(window, p, ActiveCaption(window));
            result = 0;
            return true;
        }
        if (child->pressed == 0)
        {
            return false;
        }
        if (message == WM_MOUSEMOVE)
        {
            const POINT point = ScreenPoint(window, lParam);
            const bool over = SendMessageW(window, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y)) == child->pressed;
            child = Find(window);
            if (child != nullptr && child->pushed != over)
            {
                child->pushed = over;
                Repaint(window, p, ActiveCaption(window));
            }
            result = 0;
            return true;
        }
        if (message == WM_LBUTTONUP)
        {
            const POINT point = ScreenPoint(window, lParam);
            const LRESULT pressed = child->pressed;
            const bool fire = SendMessageW(window, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y)) == pressed;
            if ((child = Find(window)) != nullptr)
            {
                child->pressed = 0;
                child->pushed = false;
            }
            ReleaseCapture();
            Repaint(window, p, ActiveCaption(window));
            if (fire)
            {
                SendMessageW(window, WM_SYSCOMMAND, CommandFor(window, pressed), MAKELPARAM(point.x, point.y));
            }
            result = 0;
            return true;
        }
        if (message == WM_CAPTURECHANGED)
        {
            child->pressed = 0;
            child->pushed = false;
            Repaint(window, p, ActiveCaption(window));
        }
        return false;
    }

    // Reads the buttons now if this is outside a non-client message, otherwise once the message is over.
    void Requery(HWND window, bool now) noexcept
    {
        Child* child = Find(window);
        if (child == nullptr)
        {
            return;
        }
        if (!now)
        {
            if (!child->queried && PostMessageW(window, g_queryButtons, 0, 0))
            {
                child->queried = true;
            }
            return;
        }
        child->queried = false;
        Buttons buttons{};
        if (!GuardedQuery(window, buttons))
        {
            Disable();
            return;
        }
        if ((child = Find(window)) != nullptr)
        {
            child->buttons = buttons;
            RedrawWindow(window, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
        }
    }

    void Forget(HWND window) noexcept
    {
        g_children.erase(std::remove_if(g_children.begin(), g_children.end(), [window](const Child& c) { return c.window == window; }), g_children.end());
    }

    LRESULT CALLBACK ChildCaptionProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR)
    {
        if (message == WM_NCDESTROY)
        {
            RemoveWindowSubclass(window, ChildCaptionProc, id);
            Forget(window);
            return DefSubclassProc(window, message, wParam, lParam);
        }
        const PaletteResources* p = g_disabled ? nullptr : ActivePalette();
        if (message == g_queryButtons && g_queryButtons != 0)
        {
            if (p != nullptr)
            {
                Requery(window, true);
            }
            return 0;
        }
        if (p == nullptr)
        {
            return DefSubclassProc(window, message, wParam, lParam);
        }
        LRESULT result = 0;
        switch (message)
        {
        case WM_WINDOWPOSCHANGED:
            result = DefSubclassProc(window, message, wParam, lParam);
            if ((reinterpret_cast<const WINDOWPOS*>(lParam)->flags & SWP_FRAMECHANGED) != 0)
            {
                Requery(window, false);
            }
            return result;
        case WM_STYLECHANGED:
        case WM_THEMECHANGED:
        case WM_SETTINGCHANGE:
        case WM_DPICHANGED:
            result = DefSubclassProc(window, message, wParam, lParam);
            Requery(window, false);
            return result;
        case WM_NCPAINT:
            return NcPaint(window, wParam, lParam, *p);
        case WM_NCACTIVATE:
            result = DefSubclassProc(window, message, wParam, lParam);
            Repaint(window, *p, wParam != FALSE);
            return result;
        // Not answering these without DefWindowProc: uxtheme then treats the frame as self-drawn and leaves
        // the window unthemed, even after this slice lets go.
        case WM_NCUAHDRAWCAPTION:
        case WM_NCUAHDRAWFRAME:
        case WM_NCMOUSEMOVE:
        case WM_NCMOUSELEAVE:
        case WM_SETTEXT:
        case WM_SETICON:
            result = DefSubclassProc(window, message, wParam, lParam);
            Repaint(window, *p, ActiveCaption(window));
            return result;
        case WM_NCLBUTTONDOWN:
        case WM_MOUSEMOVE:
        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED:
            if (TrackButton(window, message, wParam, lParam, *p, result))
            {
                return result;
            }
            return DefSubclassProc(window, message, wParam, lParam);
        default:
            return DefSubclassProc(window, message, wParam, lParam);
        }
    }

    bool ClassNameIs(HWND window, const wchar_t* name, std::size_t prefix) noexcept
    {
        wchar_t actual[64]{};
        return window != nullptr && GetClassNameW(window, actual, ARRAYSIZE(actual)) > 0 && _wcsnicmp(actual, name, prefix) == 0;
    }
}

bool IsMdiChild(HWND window) noexcept
{
    if (window == nullptr || (GetWindowLongW(window, GWL_STYLE) & WS_CHILD) == 0)
    {
        return false;
    }
    constexpr wchar_t kMfcChildFrame[] = L"AfxMDIFrame";
    return (GetWindowLongW(window, GWL_EXSTYLE) & WS_EX_MDICHILD) != 0 || ClassNameIs(window, kMfcChildFrame, ARRAYSIZE(kMfcChildFrame) - 1) ||
           ClassNameIs(GetParent(window), L"MDIClient", SIZE_MAX);
}

void ApplyChildCaption(HWND window, const PaletteResources* resources) noexcept
{
    if (window == nullptr || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId() || !IsMdiChild(window))
    {
        return;
    }
    try
    {
        const bool tracked = Find(window) != nullptr;
        if (resources != nullptr && !g_disabled && !tracked)
        {
            g_children.reserve(g_children.size() + 1);
            if (g_queryButtons == 0)
            {
                g_queryButtons = RegisterWindowMessageW(L"ThirdDigital.ProjectWiseThemes.ChildCaptionButtons");
            }
            if (g_queryButtons != 0 && SetWindowSubclass(window, ChildCaptionProc, kSubclassId, 0))
            {
                g_children.push_back({window, 0, false, false, {}});
                Requery(window, true);
            }
        }
        else if (resources == nullptr && tracked)
        {
            RemoveWindowSubclass(window, ChildCaptionProc, kSubclassId);
            Forget(window);
            RedrawWindow(window, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
        }
    }
    catch (...)
    {
    }
}
