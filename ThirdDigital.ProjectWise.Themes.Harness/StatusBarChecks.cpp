#include "Fixtures.h"

#include "Features/ThemeSwitch.h"
#include "Shared/ActivePalette.h"

#include <commctrl.h>
#include <uxtheme.h>
#include <vssym32.h>

using namespace Harness;

namespace
{
    // Paints like an MFC status-bar pane in the disabled state: GRAYTEXT on 3DFACE.
    LRESULT CALLBACK GrayTextProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_PAINT)
        {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);
            FillRect(dc, &client, GetSysColorBrush(COLOR_3DFACE));
            HGDIOBJ previous = SelectObject(dc, AliasedFont());
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
            TextOutW(dc, 6, 4, L"WWWWWW", 6);
            SelectObject(dc, previous);
            EndPaint(window, &paint);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    void RegisterGrayClass(const wchar_t* name)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = GrayTextProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = name;
        RegisterClassW(&wc);
    }

    std::optional<TextCell> PaneText(Recorder& r, HWND pane, COLORREF expected)
    {
        RECT client{};
        GetClientRect(pane, &client);
        return SampleText(r, pane, client, expected);
    }

    int StatusBarDisabledTextUsesTextColour(const CheckContext&)
    {
        Recorder r(L"StatusBarDisabledTextUsesTextColour");
        RegisterGrayClass(L"Afx:StatusBar:harness");
        RegisterGrayClass(L"TDPWHarnessGrayControl");
        HWND top = CreateTopLevel(L"StatusBarDisabledTextUsesTextColour");
        HWND existing = CreateChild(L"Afx:StatusBar:harness", top, 0, 10, 10, 300, 36);
        HWND control = CreateChild(L"TDPWHarnessGrayControl", top, 0, 10, 60, 300, 36);
        Pump(300);
        const COLORREF stockGray = GetSysColor(COLOR_GRAYTEXT);
        const auto stock = PaneText(r, existing, stockGray);

        StartTheming();
        const ColourScheme* scheme = FindScheme(L"Tango Dark");
        r.Expect(Switch(scheme), L"switch to Tango Dark");
        HWND created = CreateChild(L"Afx:StatusBar:harness", top, 0, 10, 110, 300, 36);
        Pump(500);
        const SystemColourPalette palette = BuildPalette(*scheme);
        const COLORREF text = *palette.At(COLOR_BTNTEXT);
        const COLORREF gray = *palette.At(COLOR_GRAYTEXT);
        for (HWND pane : {existing, created})
        {
            if (const auto cell = PaneText(r, pane, text))
            {
                r.Expect(cell->text == text && cell->textPixels > 20, L"%s status-bar pane text %06lX (%d px) want BTNTEXT %06lX",
                         pane == created ? L"new" : L"existing", cell->text, cell->textPixels, text);
            }
        }
        if (const auto cell = PaneText(r, control, gray))
        {
            r.Expect(cell->text == gray && cell->textPixels > 20, L"other window keeps GRAYTEXT %06lX (%d px) want %06lX", cell->text, cell->textPixels, gray);
        }

        r.Expect(Switch(nullptr), L"switch to None");
        Pump(500);
        if (const auto after = PaneText(r, existing, stockGray); after && stock)
        {
            r.Expect(after->text == stock->text && after->text == stockGray, L"status-bar pane text back to stock GRAYTEXT after None (%06lX, was %06lX)", after->text,
                     stock->text);
        }
        DestroyWindow(top);
        return r.Result();
    }

    bool g_raiseInPaint = false;

    LRESULT CALLBACK FaultingPaintProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_PAINT && g_raiseInPaint)
        {
            g_raiseInPaint = false;
            RaiseException(0xE0545453, 0, 0, nullptr);
        }
        return GrayTextProc(window, message, wParam, lParam);
    }

    bool g_caughtInPaint = false;

    // Installed after the module's subclass, so it runs first and the fault unwinds through the module's
    // procedure before being caught here, still inside the window procedure: a fault cannot cross the
    // kernel callback that delivers WM_PAINT.
    LRESULT CALLBACK CatchingSubclass(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
    {
        __try
        {
            return DefSubclassProc(window, message, wParam, lParam);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_caughtInPaint = true;
            ValidateRect(window, nullptr);
            return 0;
        }
    }

    int StatusBarPaintFaultLeavesTextAlone(const CheckContext&)
    {
        Recorder r(L"StatusBarPaintFaultLeavesTextAlone");
        WNDCLASSW wc{};
        wc.lpfnWndProc = FaultingPaintProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"Afx:StatusBar:harness-fault";
        RegisterClassW(&wc);
        HWND top = CreateTopLevel(L"StatusBarPaintFaultLeavesTextAlone");
        StartTheming();
        const ColourScheme* scheme = FindScheme(L"Tango Dark");
        r.Expect(Switch(scheme), L"switch to Tango Dark");
        HWND pane = CreateChild(L"Afx:StatusBar:harness-fault", top, 0, 10, 10, 300, 36);
        Pump(400);

        SetWindowSubclass(pane, CatchingSubclass, 1, 0);
        g_raiseInPaint = true;
        InvalidateRect(pane, nullptr, TRUE);
        UpdateWindow(pane);
        Pump(200);
        r.Expect(!g_raiseInPaint && g_caughtInPaint, L"the fault was raised inside the status bar's WM_PAINT and caught above it");

        const COLORREF gray = *BuildPalette(*scheme).At(COLOR_GRAYTEXT);
        HDC dc = CreateCompatibleDC(nullptr);
        SetTextColor(dc, gray);
        const COLORREF stored = GetTextColor(dc);
        DeleteDC(dc);
        r.Expect(stored == gray, L"GRAYTEXT set outside any status-bar paint stays GRAYTEXT after the fault (%06lX want %06lX)", stored, gray);
        Switch(nullptr);
        Pump(200);
        DestroyWindow(top);
        return r.Result();
    }

    // Paints as CMFCVisualManagerOffice2003 does: the uxtheme STATUS background, SP_PANE per pane, then the
    // pane text in GRAYTEXT.
    LRESULT CALLBACK ThemedStatusProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_PAINT)
        {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);
            if (HTHEME theme = OpenThemeData(window, L"STATUS"))
            {
                DrawThemeBackground(theme, dc, 0, 0, &client, nullptr);
                RECT pane = client;
                pane.right = client.right / 2;
                DrawThemeBackground(theme, dc, SP_PANE, 0, &pane, nullptr);
                pane.left = pane.right;
                pane.right = client.right;
                DrawThemeBackground(theme, dc, SP_PANE, 0, &pane, nullptr);
                CloseThemeData(theme);
            }
            HGDIOBJ previous = SelectObject(dc, AliasedFont());
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
            TextOutW(dc, 6, 4, L"WWWWWW", 6);
            SelectObject(dc, previous);
            EndPaint(window, &paint);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    int StatusBarBackgroundUsesPalette(const CheckContext&)
    {
        Recorder r(L"StatusBarBackgroundUsesPalette");
        for (const wchar_t* name : {L"Afx:StatusBar:harness-theme", L"TDPWHarnessThemedControl"})
        {
            WNDCLASSW wc{};
            wc.lpfnWndProc = ThemedStatusProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = name;
            RegisterClassW(&wc);
        }
        HWND top = CreateTopLevel(L"StatusBarBackgroundUsesPalette");
        HWND bar = CreateChild(L"Afx:StatusBar:harness-theme", top, 0, 10, 10, 400, 36);
        HWND control = CreateChild(L"TDPWHarnessThemedControl", top, 0, 10, 60, 400, 36);
        Pump(300);
        const auto stockBar = PaneText(r, bar, GetSysColor(COLOR_GRAYTEXT));
        const auto stockControl = PaneText(r, control, GetSysColor(COLOR_GRAYTEXT));
        if (stockBar)
        {
            r.Note(L"stock status bar background %06lX text %06lX", stockBar->background, stockBar->text);
        }

        StartTheming();
        for (const wchar_t* schemeName : {L"Tango Dark", L"One Half Light"})
        {
            const ColourScheme* scheme = FindScheme(schemeName);
            r.Expect(Switch(scheme), L"switch to %s", schemeName);
            Pump(500);
            const SystemColourPalette palette = BuildPalette(*scheme);
            const COLORREF face = *palette.At(COLOR_3DFACE);
            const COLORREF text = *palette.At(COLOR_BTNTEXT);
            if (const auto cell = PaneText(r, bar, text))
            {
                r.Expect(cell->background == face, L"%s status-bar background %06lX want 3DFACE %06lX", schemeName, cell->background, face);
                r.Expect(cell->text == text && cell->textPixels > 20, L"%s status-bar text %06lX want BTNTEXT %06lX", schemeName, cell->text, text);
            }
            if (const auto cell = PaneText(r, control, *palette.At(COLOR_GRAYTEXT)); cell && stockControl)
            {
                r.Expect(cell->background == stockControl->background, L"%s other window keeps the stock STATUS background %06lX (now %06lX)", schemeName,
                         stockControl->background, cell->background);
            }
        }

        r.Expect(Switch(nullptr), L"switch to None");
        Pump(500);
        if (const auto after = PaneText(r, bar, GetSysColor(COLOR_GRAYTEXT)); after && stockBar)
        {
            r.Expect(after->background == stockBar->background && after->text == stockBar->text,
                     L"status bar stock after None (%06lX on %06lX, was %06lX on %06lX)", after->text, after->background, stockBar->text, stockBar->background);
        }
        DestroyWindow(top);
        return r.Result();
    }

    int ComctlStatusBarUsesPalette(const CheckContext&)
    {
        Recorder r(L"ComctlStatusBarUsesPalette");
        HWND top = CreateTopLevel(L"ComctlStatusBarUsesPalette");
        HWND bar = CreateChild(STATUSCLASSNAMEW, top, SBARS_SIZEGRIP, 0, 0, 0, 0);
        int parts[] = {220, -1};
        SendMessageW(bar, SB_SETPARTS, 2, reinterpret_cast<LPARAM>(parts));
        SendMessageW(bar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(L"WWWWWW"));
        Pump(300);
        RECT part{};
        SendMessageW(bar, SB_GETRECT, 0, reinterpret_cast<LPARAM>(&part));
        InflateRect(&part, -2, -2);
        const auto stock = SampleText(r, bar, part, GetSysColor(COLOR_BTNTEXT));
        if (stock)
        {
            r.Note(L"stock comctl32 status bar background %06lX text %06lX", stock->background, stock->text);
        }

        StartTheming();
        const ColourScheme* scheme = FindScheme(L"Tango Dark");
        r.Expect(Switch(scheme), L"switch to Tango Dark");
        Pump(500);
        const SystemColourPalette palette = BuildPalette(*scheme);
        if (const auto cell = SampleText(r, bar, part, *palette.At(COLOR_BTNTEXT)))
        {
            r.Expect(cell->background == *palette.At(COLOR_3DFACE), L"status bar background %06lX want 3DFACE %06lX", cell->background, *palette.At(COLOR_3DFACE));
            r.Expect(cell->text == *palette.At(COLOR_BTNTEXT) && cell->textPixels > 20, L"status bar text %06lX want BTNTEXT %06lX", cell->text,
                     *palette.At(COLOR_BTNTEXT));
        }
        r.Expect(Switch(nullptr), L"switch to None");
        Pump(500);
        if (const auto after = SampleText(r, bar, part, GetSysColor(COLOR_BTNTEXT)); after && stock)
        {
            r.Expect(after->background == stock->background && after->text == stock->text, L"status bar stock after None (%06lX on %06lX)", after->text,
                     after->background);
        }
        DestroyWindow(top);
        return r.Result();
    }

    const CheckRegistration registrations[] = {
        CheckRegistration{{L"StatusBar", L"ComctlStatusBarUsesPalette", L"-", Host::Self, false, ComctlStatusBarUsesPalette}},
        CheckRegistration{{L"StatusBar", L"StatusBarBackgroundUsesPalette", L"-", Host::Self, false, StatusBarBackgroundUsesPalette}},
        CheckRegistration{{L"StatusBar", L"StatusBarPaintFaultLeavesTextAlone", L"-", Host::Self, false, StatusBarPaintFaultLeavesTextAlone}},
        CheckRegistration{{L"StatusBar", L"StatusBarDisabledTextUsesTextColour", L"-", Host::Self, false, StatusBarDisabledTextUsesTextColour}},
    };
}
