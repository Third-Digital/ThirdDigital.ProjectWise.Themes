#include "MenuBar.h"

#include "Shared/CaptionGlyph.h"
#include "Shared/Log.h"

#include <commctrl.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace
{
    constexpr UINT WM_UAHDRAWMENU = 0x0091;
    constexpr UINT WM_UAHDRAWMENUITEM = 0x0092;
    constexpr UINT_PTR kSubclassId = 0x5444504D;

    // Undocumented: the lParam layouts of WM_UAHDRAWMENU and WM_UAHDRAWMENUITEM on x86.
    struct UahMenu
    {
        HMENU menu;
        HDC dc;
        DWORD flags;
    };

    struct UahMenuItem
    {
        int position;
        SIZE metrics[4];
        DWORD popupWidths[4];
        DWORD popupFlags;
    };

    struct UahDrawMenuItem
    {
        DRAWITEMSTRUCT item;
        UahMenu menu;
        UahMenuItem menuItem;
    };

    static_assert(sizeof(UahMenu) == 12);
    static_assert(sizeof(UahMenuItem) == 56);
    static_assert(sizeof(DRAWITEMSTRUCT) == 48);
    static_assert(offsetof(UahDrawMenuItem, menu) == 48);
    static_assert(offsetof(UahDrawMenuItem, menuItem) == 60);
    static_assert(sizeof(UahDrawMenuItem) == 116);

    bool g_disabled = false;

    // UI thread only.
    std::vector<HWND> g_windows;

    bool Tracked(HWND window) noexcept
    {
        return std::find(g_windows.begin(), g_windows.end(), window) != g_windows.end();
    }

    bool ToWindowCoordinates(HWND window, RECT& rect) noexcept
    {
        RECT windowRect{};
        if (!GetWindowRect(window, &windowRect))
        {
            return false;
        }
        OffsetRect(&rect, -windowRect.left, -windowRect.top);
        return true;
    }

    void Fill(HDC dc, const RECT& rect, COLORREF colour) noexcept
    {
        if (HBRUSH brush = CreateSolidBrush(colour))
        {
            FillRect(dc, &rect, brush);
            DeleteObject(brush);
        }
    }

    void DrawBar(HWND window, const UahMenu& menu, const PaletteResources& p) noexcept
    {
        MENUBARINFO bar{sizeof(bar)};
        if (GetMenuBarInfo(window, OBJID_MENU, 0, &bar) && ToWindowCoordinates(window, bar.rcBar))
        {
            Fill(menu.dc, bar.rcBar, *p.palette.At(COLOR_MENUBAR));
        }
    }

    // Under dark schemes Windows draws a maximised MDI child's menu-bar glyphs light on a white item, so they are
    // drawn here on the bar colour. Returns false for any other bitmap, which Windows then draws itself.
    bool DrawGlyphItem(const UahDrawMenuItem& draw, HBITMAP bitmap, ULONG_PTR data, const PaletteResources& p) noexcept
    {
        UINT glyph = 0;
        if (bitmap == HBMMENU_MBAR_CLOSE || bitmap == HBMMENU_MBAR_CLOSE_D)
        {
            glyph = DFCS_CAPTIONCLOSE;
        }
        else if (bitmap == HBMMENU_MBAR_MINIMIZE || bitmap == HBMMENU_MBAR_MINIMIZE_D)
        {
            glyph = DFCS_CAPTIONMIN;
        }
        else if (bitmap == HBMMENU_MBAR_RESTORE)
        {
            glyph = DFCS_CAPTIONRESTORE;
        }
        else if (bitmap != HBMMENU_SYSTEM)
        {
            return false;
        }

        HICON icon = nullptr;
        if (bitmap == HBMMENU_SYSTEM && (icon = WindowSmallIcon(reinterpret_cast<HWND>(data))) == nullptr)
        {
            return false;
        }
        HDC dc = draw.item.hDC;
        Fill(dc, draw.item.rcItem, *p.palette.At(COLOR_MENUBAR));
        if (icon != nullptr)
        {
            const int cx = GetSystemMetrics(SM_CXSMICON);
            const int cy = GetSystemMetrics(SM_CYSMICON);
            const RECT& item = draw.item.rcItem;
            DrawIconEx(dc, item.left + (item.right - item.left - cx) / 2, item.top + (item.bottom - item.top - cy) / 2, icon, cx, cy, 0, nullptr, DI_NORMAL);
            return true;
        }
        const bool inactive =
            bitmap == HBMMENU_MBAR_CLOSE_D || bitmap == HBMMENU_MBAR_MINIMIZE_D || (draw.item.itemState & (ODS_GRAYED | ODS_DISABLED)) != 0;
        if ((draw.item.itemState & ODS_SELECTED) != 0)
        {
            glyph |= DFCS_PUSHED;
        }
        return DrawCaptionGlyph(dc, CentredSquare(draw.item.rcItem), glyph, *p.palette.At(inactive ? COLOR_GRAYTEXT : COLOR_MENUTEXT));
    }

    // Plain text items and the MDI glyphs are drawn here; every other bitmap or owner-draw item returns false so
    // Windows draws it itself.
    bool DrawItem(const UahDrawMenuItem& draw, const PaletteResources& p) noexcept
    {
        wchar_t label[256]{};
        MENUITEMINFOW info{sizeof(info)};
        info.fMask = MIIM_FTYPE | MIIM_BITMAP | MIIM_STRING | MIIM_DATA;
        info.dwTypeData = label;
        info.cch = ARRAYSIZE(label) - 1;
        if (!GetMenuItemInfoW(draw.menu.menu, static_cast<UINT>(draw.menuItem.position), TRUE, &info))
        {
            return false;
        }
        if (info.hbmpItem != nullptr)
        {
            return DrawGlyphItem(draw, info.hbmpItem, info.dwItemData, p);
        }
        if ((info.fType & (MFT_BITMAP | MFT_OWNERDRAW)) != 0)
        {
            return false;
        }

        const UINT state = draw.item.itemState;
        const COLORREF bar = *p.palette.At(COLOR_MENUBAR);
        COLORREF text = *p.palette.At(COLOR_MENUTEXT);
        const COLORREF background = (state & (ODS_HOTLIGHT | ODS_SELECTED)) != 0 ? *p.palette.At(COLOR_MENUHILIGHT) : bar;
        if ((state & (ODS_GRAYED | ODS_DISABLED)) != 0)
        {
            text = *p.palette.At(COLOR_GRAYTEXT);
        }
        HDC dc = draw.item.hDC;
        RECT rect = draw.item.rcItem;
        Fill(dc, rect, background);

        UINT format = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
        if ((state & ODS_NOACCEL) != 0)
        {
            format |= DT_HIDEPREFIX;
        }
        const int mode = SetBkMode(dc, TRANSPARENT);
        const COLORREF previous = SetTextColor(dc, text);
        DrawTextW(dc, label, -1, &rect, format);
        SetTextColor(dc, previous);
        SetBkMode(dc, mode);
        return true;
    }

    // Windows draws a one-pixel light line between the menu bar and the client area.
    void DrawUnderline(HWND window, const PaletteResources& p) noexcept
    {
        RECT client{};
        POINT origin{0, 0};
        if (GetMenu(window) == nullptr || !GetClientRect(window, &client) || !ClientToScreen(window, &origin))
        {
            return;
        }
        RECT line{origin.x, origin.y - 1, origin.x + client.right, origin.y};
        if (!ToWindowCoordinates(window, line))
        {
            return;
        }
        if (HDC dc = GetWindowDC(window))
        {
            Fill(dc, line, *p.palette.At(COLOR_MENUBAR));
            ReleaseDC(window, dc);
        }
    }

    bool GuardedDraw(HWND window, UINT message, LPARAM lParam, const PaletteResources& p) noexcept
    {
        __try
        {
            if (message == WM_UAHDRAWMENU)
            {
                DrawBar(window, *reinterpret_cast<const UahMenu*>(lParam), p);
                return true;
            }
            return DrawItem(*reinterpret_cast<const UahDrawMenuItem*>(lParam), p);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_disabled = true;
            return false;
        }
    }

    void GuardedUnderline(HWND window, const PaletteResources& p) noexcept
    {
        __try
        {
            DrawUnderline(window, p);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_disabled = true;
        }
    }

    void Forget(HWND window) noexcept
    {
        g_windows.erase(std::remove(g_windows.begin(), g_windows.end(), window), g_windows.end());
    }

    LRESULT CALLBACK MenuBarProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR)
    {
        if (message == WM_NCDESTROY)
        {
            RemoveWindowSubclass(window, MenuBarProc, id);
            Forget(window);
            return DefSubclassProc(window, message, wParam, lParam);
        }
        const PaletteResources* p = g_disabled ? nullptr : ActivePalette();
        if (p == nullptr)
        {
            return DefSubclassProc(window, message, wParam, lParam);
        }
        if ((message == WM_UAHDRAWMENU || message == WM_UAHDRAWMENUITEM) && lParam != 0 && GuardedDraw(window, message, lParam, *p))
        {
            return TRUE;
        }
        if (message == WM_NCPAINT || message == WM_NCACTIVATE)
        {
            const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
            GuardedUnderline(window, *p);
            return result;
        }
        return DefSubclassProc(window, message, wParam, lParam);
    }
}

void ApplyMenuBar(HWND window, const PaletteResources* resources) noexcept
{
    if (g_disabled || window == nullptr || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId() ||
        (GetWindowLongW(window, GWL_STYLE) & WS_CHILD) != 0 || GetMenu(window) == nullptr)
    {
        return;
    }
    try
    {
        const bool tracked = Tracked(window);
        if (resources != nullptr)
        {
            if (!tracked)
            {
                g_windows.reserve(g_windows.size() + 1);
                if (!SetWindowSubclass(window, MenuBarProc, kSubclassId, 0))
                {
                    return;
                }
                g_windows.push_back(window);
                DrawMenuBar(window);
            }
        }
        else if (tracked)
        {
            DrawMenuBar(window);
            RedrawWindow(window, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
        }
    }
    catch (...)
    {
        Log(L"menu bar: could not be themed; left stock");
    }
}
