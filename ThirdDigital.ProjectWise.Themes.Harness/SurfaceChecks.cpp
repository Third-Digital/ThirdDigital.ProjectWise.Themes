#include "Fixtures.h"

#include "Domain/ExplorerSurface.h"
#include "Features/ControlColours/ControlSubclass.h"
#include "Features/SystemColours/SystemColourHooks.h"
#include "Features/ThemeSwitch.h"
#include "Features/PaintColours/PaintColours.h"
#include "Features/HtmlReports/HtmlReports.h"
#include <atlbase.h>
#include <mshtml.h>
#include <oleacc.h>
#include <cwctype>
#include "Features/TitleBar/TitleBar.h"
#include "Shared/ActivePalette.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <ddeml.h>
#include <gdiplus.h>
#include "Features/DependencyColours/DependencyColours.h"
#pragma comment(lib, "gdiplus.lib")

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <map>
#include <thread>

using namespace Harness;

namespace
{
    const PaletteResources* Activate(Recorder& r, const wchar_t* scheme)
    {
        const PaletteResources* p = PreparePalette(*FindScheme(scheme));
        if (!r.Expect(p != nullptr && InstallSystemColourHooks(), L"colour hooks installed") ||
            !r.Expect(InstallControlHook(GetCurrentThreadId()), L"control hook installed"))
        {
            return nullptr;
        }
        if (!InstallCallRecorder())
        {
            r.Invalidate(L"call recorder could not be installed");
            return nullptr;
        }
        PublishPalette(p);
        return p;
    }

    BOOL CALLBACK ApplyToChild(HWND child, LPARAM resources)
    {
        ApplyControlTheme(child, reinterpret_cast<const PaletteResources*>(resources));
        return TRUE;
    }

    void ApplyAll(HWND top, const PaletteResources* p)
    {
        ApplyControlTheme(top, p);
        ApplyTitleBar(top, p);
        EnumChildWindows(top, ApplyToChild, reinterpret_cast<LPARAM>(p));
        RedrawWindow(top, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    }

    COLORREF Want(const PaletteResources* p, int index)
    {
        return *p->palette.At(index);
    }

    bool Has(const std::vector<RecordedCall>& calls, RecordedCall::Kind kind, const wchar_t* text = nullptr)
    {
        for (const RecordedCall& call : calls)
        {
            if (call.kind == kind && (text == nullptr || call.text == text))
            {
                return true;
            }
        }
        return false;
    }

    std::optional<double> ScrollbarLuminance(Recorder& r, HWND list)
    {
        RECT window{};
        GetWindowRect(list, &window);
        const auto pixel = Sample(r, list, {window.right - window.left - 9, (window.bottom - window.top) * 2 / 3});
        if (!pixel)
        {
            return std::nullopt;
        }
        return RelativeLuminance(*pixel);
    }

    std::optional<COLORREF> TreeBackground(Recorder& r, HWND tree)
    {
        RECT window{};
        GetWindowRect(tree, &window);
        return Sample(r, tree, {(window.right - window.left) / 2, window.bottom - window.top - 20});
    }

    int ExistingAndNewControlsTheme(const CheckContext&)
    {
        Recorder r(L"ExistingAndNewControlsTheme");
        HWND top = CreateTopLevel(L"ExistingAndNewControlsTheme");
        HWND existingList = ListWithScrollbar(top, 10, 10);
        HWND existingTree = ShortTree(top, 260, 10);
        Pump(300);
        const auto stockScroll = ScrollbarLuminance(r, existingList);
        const auto stockTree = TreeBackground(r, existingTree);

        const PaletteResources* p = Activate(r, L"Campbell");
        if (p == nullptr)
        {
            return r.Result();
        }
        ApplyAll(top, p);
        SendMessageW(existingList, WM_SYSCOLORCHANGE, 0, 0);
        SendMessageW(existingTree, WM_SYSCOLORCHANGE, 0, 0);
        HWND newList = ListWithScrollbar(top, 10, 180);
        HWND newTree = ShortTree(top, 260, 180);
        RedrawWindow(top, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        Pump(400);

        if (stockScroll && stockTree)
        {
            r.Note(L"stock scrollbar luminance %.3f, stock tree background %06lX", *stockScroll, *stockTree);
            r.Expect(*stockScroll > 0.5, L"stock scrollbar is light");
        }
        for (HWND list : {existingList, newList})
        {
            const wchar_t* which = list == newList ? L"new" : L"existing";
            if (const auto lum = ScrollbarLuminance(r, list))
            {
                r.Expect(*lum < 0.2, L"%s list-view scrollbar is dark (%.3f)", which, *lum);
            }
            if (const auto bg = Sample(r, list, {200, 60}))
            {
                r.Expect(*bg == Want(p, COLOR_WINDOW), L"%s list-view background %06lX want %06lX", which, *bg, Want(p, COLOR_WINDOW));
            }
            r.Expect(Has(RecordedCalls(list), RecordedCall::Kind::SetWindowTheme, L"DarkMode_DarkTheme"), L"%s list-view given DarkMode_DarkTheme", which);
        }
        for (HWND tree : {existingTree, newTree})
        {
            const wchar_t* which = tree == newTree ? L"new" : L"existing";
            if (const auto bg = TreeBackground(r, tree))
            {
                r.Expect(*bg == Want(p, COLOR_WINDOW), L"%s tree-view background %06lX want %06lX", which, *bg, Want(p, COLOR_WINDOW));
            }
            r.Expect(Has(RecordedCalls(tree), RecordedCall::Kind::SetWindowTheme, L"DarkMode_Explorer"), L"%s tree-view given DarkMode_Explorer", which);
        }
        DestroyWindow(top);
        return r.Result();
    }

    HBRUSH g_appBrush = nullptr;

    LRESULT CALLBACK CustomBrushProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_CTLCOLOREDIT)
        {
            SetBkColor(reinterpret_cast<HDC>(wParam), RGB(0xC0, 0x20, 0x20));
            return reinterpret_cast<LRESULT>(g_appBrush);
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    int CustomBrushIsPreserved(const CheckContext&)
    {
        Recorder r(L"CustomBrushIsPreserved");
        g_appBrush = CreateSolidBrush(RGB(0xC0, 0x20, 0x20));
        WNDCLASSW wc{};
        wc.lpfnWndProc = CustomBrushProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"TDPWHarnessCustomBrush";
        RegisterClassW(&wc);

        const PaletteResources* p = Activate(r, L"Campbell");
        if (p == nullptr)
        {
            return r.Result();
        }
        HWND top = CreateTopLevel(L"CustomBrushIsPreserved");
        HWND container = CreateChild(L"TDPWHarnessCustomBrush", top, 0, 10, 10, 300, 80);
        HWND customEdit = CreateChild(L"EDIT", container, WS_BORDER | ES_AUTOHSCROLL, 10, 10, 260, 40);
        HWND stockEdit = CreateChild(L"EDIT", top, WS_BORDER | ES_AUTOHSCROLL, 10, 120, 260, 40);
        Pump(300);

        HDC dc = GetDC(customEdit);
        const LRESULT returned = SendMessageW(container, WM_CTLCOLOREDIT, reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(customEdit));
        ReleaseDC(customEdit, dc);
        r.Expect(reinterpret_cast<HBRUSH>(returned) == g_appBrush, L"application brush returned unchanged");
        if (const auto pixel = Sample(r, customEdit, {200, 20}))
        {
            r.Expect(*pixel == RGB(0xC0, 0x20, 0x20), L"application-coloured edit keeps its colour (%06lX)", *pixel);
        }
        if (const auto pixel = Sample(r, stockEdit, {200, 20}))
        {
            r.Expect(*pixel == Want(p, COLOR_WINDOW), L"stock edit uses the palette (%06lX)", *pixel);
        }
        DestroyWindow(top);
        return r.Result();
    }

    HWND OpenDialog(const wchar_t* title)
    {
        std::vector<WORD> tmpl = DialogTemplate(title, DS_MODALFRAME);
        HWND dialog = CreateDialogIndirectParamW(GetModuleHandleW(nullptr), reinterpret_cast<LPCDLGTEMPLATEW>(tmpl.data()), nullptr,
                                                 [](HWND, UINT message, WPARAM, LPARAM) -> INT_PTR { return message == WM_INITDIALOG; }, 0);
        if (dialog != nullptr)
        {
            SendDlgItemMessageW(dialog, 100, WM_SETFONT, reinterpret_cast<WPARAM>(AliasedFont()), TRUE);
            SendDlgItemMessageW(dialog, 101, WM_SETFONT, reinterpret_cast<WPARAM>(AliasedFont()), TRUE);
            SetWindowPos(dialog, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        }
        return dialog;
    }

    int DialogCtlColorUsesPalette(const CheckContext&)
    {
        Recorder r(L"DialogCtlColorUsesPalette");
        const PaletteResources* p = Activate(r, L"Campbell");
        if (p == nullptr)
        {
            return r.Result();
        }
        HWND dialog = OpenDialog(L"DialogCtlColorUsesPalette");
        if (dialog == nullptr)
        {
            r.Invalidate(L"dialog not created");
            return r.Result();
        }
        Pump(400);
        RECT client{};
        GetClientRect(dialog, &client);
        RECT window{};
        GetWindowRect(dialog, &window);
        POINT inner{client.right - 10, client.bottom - 10};
        MapWindowPoints(dialog, nullptr, &inner, 1);
        if (const auto pixel = Sample(r, dialog, {inner.x - window.left, inner.y - window.top}))
        {
            r.Expect(*pixel == Want(p, COLOR_3DFACE), L"dialog background %06lX want %06lX", *pixel, Want(p, COLOR_3DFACE));
        }
        HWND label = GetDlgItem(dialog, 100);
        RECT labelRect{};
        GetClientRect(label, &labelRect);
        if (const auto cell = SampleText(r, label, labelRect, Want(p, COLOR_WINDOWTEXT)))
        {
            r.Expect(cell->background == Want(p, COLOR_3DFACE), L"static text cell background %06lX want %06lX", cell->background, Want(p, COLOR_3DFACE));
        }
        DestroyWindow(dialog);
        return r.Result();
    }

    int CtlColorTextUsesPalette(const CheckContext&)
    {
        Recorder r(L"CtlColorTextUsesPalette");
        const PaletteResources* p = Activate(r, L"Campbell");
        if (p == nullptr)
        {
            return r.Result();
        }
        HWND dialog = OpenDialog(L"CtlColorTextUsesPalette");
        if (dialog == nullptr)
        {
            r.Invalidate(L"dialog not created");
            return r.Result();
        }
        Pump(400);
        struct Expectation
        {
            int id;
            int background;
            const wchar_t* label;
        };
        for (const Expectation& e : {Expectation{100, COLOR_3DFACE, L"static"}, Expectation{101, COLOR_WINDOW, L"edit"}})
        {
            HWND control = GetDlgItem(dialog, e.id);
            RECT rect{};
            GetClientRect(control, &rect);
            const auto cell = SampleText(r, control, rect, Want(p, COLOR_WINDOWTEXT));
            if (!cell)
            {
                continue;
            }
            r.Expect(cell->background == Want(p, e.background), L"%s background %06lX want %06lX", e.label, cell->background, Want(p, e.background));
            r.Expect(cell->text == Want(p, COLOR_WINDOWTEXT) && cell->textPixels > 20, L"%s text %06lX (%d px) want %06lX", e.label, cell->text,
                     cell->textPixels, Want(p, COLOR_WINDOWTEXT));
        }
        DestroyWindow(dialog);
        return r.Result();
    }

    int UnknownWindowsRemainUntouched(const CheckContext&)
    {
        Recorder r(L"UnknownWindowsRemainUntouched");
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"TDPWHarnessUnknown";
        RegisterClassW(&wc);

        const PaletteResources* p = Activate(r, L"Campbell");
        if (p == nullptr)
        {
            return r.Result();
        }
        HWND top = CreateTopLevel(L"UnknownWindowsRemainUntouched");
        HWND unknownChild = CreateChild(L"TDPWHarnessUnknown", top, 0, 10, 10, 200, 100);
        HWND unknownPopup = CreateWindowExW(WS_EX_TOOLWINDOW, L"TDPWHarnessUnknown", L"", WS_POPUP | WS_VISIBLE, 300, 300, 80, 80, nullptr, nullptr,
                                            GetModuleHandleW(nullptr), nullptr);
        Pump(200);
        ApplyAll(top, p);
        ApplyControlTheme(unknownChild, p);
        ApplyTitleBar(unknownChild, p);
        ApplyControlTheme(unknownPopup, p);
        ApplyTitleBar(unknownPopup, p);
        Pump(200);
        r.Expect(RecordedCalls(unknownChild).empty(), L"unknown child window received no subclass, theme or DWM change (%u calls)",
                 static_cast<unsigned>(RecordedCalls(unknownChild).size()));
        r.Expect(RecordedCalls(unknownPopup).empty(), L"unknown captionless popup received no change (%u calls)",
                 static_cast<unsigned>(RecordedCalls(unknownPopup).size()));
        DestroyWindow(unknownPopup);
        DestroyWindow(top);
        return r.Result();
    }

    int SubclassRemovesOnDestroy(const CheckContext&)
    {
        Recorder r(L"SubclassRemovesOnDestroy");
        const PaletteResources* p = Activate(r, L"Campbell");
        if (p == nullptr)
        {
            return r.Result();
        }
        HWND top = CreateTopLevel(L"SubclassRemovesOnDestroy");
        HWND edit = CreateChild(L"EDIT", top, WS_BORDER, 10, 10, 200, 30);
        Pump(100);
        r.Expect(Has(RecordedCalls(edit), RecordedCall::Kind::SetWindowSubclass), L"new edit control subclassed");
        DestroyWindow(edit);
        r.Expect(Has(RecordedCalls(edit), RecordedCall::Kind::RemoveWindowSubclass), L"subclass removed on WM_NCDESTROY");
        DestroyWindow(top);
        r.Expect(Has(RecordedCalls(top), RecordedCall::Kind::RemoveWindowSubclass), L"top-level subclass removed on WM_NCDESTROY");
        return r.Result();
    }

    int WrongThreadIsSkipped(const CheckContext&)
    {
        Recorder r(L"WrongThreadIsSkipped");
        const PaletteResources* p = Activate(r, L"Campbell");
        if (p == nullptr)
        {
            return r.Result();
        }
        HWND top = CreateTopLevel(L"WrongThreadIsSkipped");
        HWND edit = CreateChild(L"EDIT", top, WS_BORDER, 10, 10, 200, 30);
        HWND tree = ShortTree(top, 10, 60);
        Pump(100);
        ClearRecordedCalls();
        HWND workerTop = nullptr;
        HWND workerTree = nullptr;
        std::thread worker([&] {
            ApplyControlTheme(edit, p);
            ApplyControlTheme(tree, p);
            ApplyTitleBar(top, p);
            ApplyControlTheme(edit, nullptr);
            ApplyTitleBar(top, nullptr);
            workerTop = CreateWindowExW(0, L"TDPWHarnessWindow", L"worker", WS_OVERLAPPEDWINDOW, 0, 0, 200, 200, nullptr, nullptr,
                                        GetModuleHandleW(nullptr), nullptr);
            workerTree = CreateWindowExW(0, WC_TREEVIEWW, L"", WS_CHILD, 0, 0, 100, 100, workerTop, nullptr, GetModuleHandleW(nullptr), nullptr);
            ApplyControlTheme(workerTree, p);
            ApplyControlTheme(workerTop, p);
            ApplyTitleBar(workerTop, p);
            DestroyWindow(workerTop);
        });
        worker.join();
        r.Expect(RecordedCalls(nullptr).empty(), L"no window changes from a non-UI thread (%u calls)", static_cast<unsigned>(RecordedCalls(nullptr).size()));
        DestroyWindow(top);
        return r.Result();
    }

    int TitleBarAttributesReset(const CheckContext&)
    {
        Recorder r(L"TitleBarAttributesReset");
        const PaletteResources* p = Activate(r, L"Campbell");
        if (p == nullptr)
        {
            return r.Result();
        }
        HWND top = CreateTopLevel(L"TitleBarAttributesReset");
        Pump(100);
        auto lastValue = [&](DWORD attribute) -> std::optional<DWORD> {
            std::optional<DWORD> value;
            for (const RecordedCall& call : RecordedCalls(top))
            {
                if (call.kind == RecordedCall::Kind::DwmSetWindowAttribute && call.attribute == attribute)
                {
                    value = call.value;
                }
            }
            return value;
        };
        r.Expect(lastValue(DWMWA_USE_IMMERSIVE_DARK_MODE) == 1u, L"dark mode attribute set on a new window");
        r.Expect(lastValue(DWMWA_BORDER_COLOR) == Want(p, COLOR_ACTIVEBORDER), L"border colour set");
        r.Expect(lastValue(DWMWA_CAPTION_COLOR) == Want(p, COLOR_ACTIVECAPTION), L"caption colour set");
        r.Expect(lastValue(DWMWA_TEXT_COLOR) == Want(p, COLOR_CAPTIONTEXT), L"caption text colour set");

        ApplyTitleBar(top, nullptr);
        r.Expect(lastValue(DWMWA_USE_IMMERSIVE_DARK_MODE) == 0u, L"dark mode reset to FALSE");
        r.Expect(lastValue(DWMWA_BORDER_COLOR) == DWMWA_COLOR_DEFAULT, L"border reset to DWMWA_COLOR_DEFAULT");
        r.Expect(lastValue(DWMWA_CAPTION_COLOR) == DWMWA_COLOR_DEFAULT, L"caption reset to DWMWA_COLOR_DEFAULT");
        r.Expect(lastValue(DWMWA_TEXT_COLOR) == DWMWA_COLOR_DEFAULT, L"text reset to DWMWA_COLOR_DEFAULT");

        ClearRecordedCalls();
        ApplyTitleBar(top, nullptr);
        r.Expect(RecordedCalls(top).empty(), L"an untracked window is not reset again");
        const PaletteResources* light = PreparePalette(*FindScheme(L"Tango Light"));
        PublishPalette(light);
        HWND lightTop = CreateTopLevel(L"TitleBarAttributesReset light");
        Pump(100);
        ApplyTitleBar(lightTop, light);
        r.Expect(!Has(RecordedCalls(lightTop), RecordedCall::Kind::DwmSetWindowAttribute) && Has(RecordedCalls(lightTop), RecordedCall::Kind::SetWindowSubclass),
                 L"a light scheme makes no DWM calls on untracked windows");
        DestroyWindow(lightTop);
        DestroyWindow(top);
        return r.Result();
    }

    int UnsupportedDwmIsNonFatal(const CheckContext&)
    {
        Recorder r(L"UnsupportedDwmIsNonFatal");
        const PaletteResources* p = Activate(r, L"Campbell");
        if (p == nullptr)
        {
            return r.Result();
        }
        FailDwmCalls(true);
        HWND top = CreateTopLevel(L"UnsupportedDwmIsNonFatal");
        Pump(100);
        ApplyTitleBar(top, p);
        ApplyTitleBar(top, nullptr);
        FailDwmCalls(false);
        r.Expect(Has(RecordedCalls(top), RecordedCall::Kind::DwmSetWindowAttribute), L"DWM attributes were attempted");
        DWORD_PTR result = 0;
        r.Expect(SendMessageTimeoutW(top, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 1000, &result) != 0, L"window still responsive after rejected attributes");
        DestroyWindow(top);
        return r.Result();
    }

    int TreeViewBackgroundFollowsPalette(const CheckContext&)
    {
        Recorder r(L"TreeViewBackgroundFollowsPalette");
        HWND top = CreateTopLevel(L"TreeViewBackgroundFollowsPalette");
        HWND existing = ShortTree(top, 10, 10);
        Pump(300);
        for (const wchar_t* name : {L"Campbell", L"One Half Light"})
        {
            const PaletteResources* p = ActivePalette() == nullptr ? Activate(r, name) : PreparePalette(*FindScheme(name));
            PublishPalette(p);
            if (p == nullptr)
            {
                return r.Result();
            }
            ApplyAll(top, p);
            HWND created = ShortTree(top, 260, 10);
            RedrawWindow(top, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
            Pump(400);
            for (HWND tree : {existing, created})
            {
                if (const auto bg = TreeBackground(r, tree))
                {
                    r.Expect(*bg == Want(p, COLOR_WINDOW), L"%s %s tree-view background %06lX want %06lX", name,
                             tree == existing ? L"existing" : L"new", *bg, Want(p, COLOR_WINDOW));
                }
            }
            DestroyWindow(created);
        }
        DestroyWindow(top);
        return r.Result();
    }

    int TreeViewBackgroundRestoredOnNone(const CheckContext&)
    {
        Recorder r(L"TreeViewBackgroundRestoredOnNone");
        HWND top = CreateTopLevel(L"TreeViewBackgroundRestoredOnNone");
        HWND plain = ShortTree(top, 10, 10);
        HWND custom = ShortTree(top, 260, 10);
        TreeView_SetBkColor(custom, RGB(0x20, 0x60, 0x20));
        Pump(300);
        const COLORREF plainBefore = TreeView_GetBkColor(plain);
        const COLORREF customBefore = TreeView_GetBkColor(custom);
        const auto plainPixel = TreeBackground(r, plain);
        const PaletteResources* p = Activate(r, L"Campbell");
        if (p == nullptr)
        {
            return r.Result();
        }
        ApplyAll(top, p);
        Pump(300);
        PublishPalette(nullptr);
        ApplyAll(top, nullptr);
        Pump(400);
        r.Expect(TreeView_GetBkColor(plain) == plainBefore, L"default tree-view background restored (%08lX was %08lX)", TreeView_GetBkColor(plain), plainBefore);
        r.Expect(TreeView_GetBkColor(custom) == customBefore, L"application tree-view background restored (%08lX was %08lX)", TreeView_GetBkColor(custom),
                 customBefore);
        if (const auto after = TreeBackground(r, plain); after && plainPixel)
        {
            r.Expect(*after == *plainPixel, L"default tree-view pixels restored (%06lX was %06lX)", *after, *plainPixel);
        }

        HWND untracked = ShortTree(top, 10, 180);
        TreeView_SetBkColor(untracked, RGB(0x40, 0x10, 0x10));
        const COLORREF untrackedBefore = TreeView_GetBkColor(untracked);
        ApplyAll(top, nullptr);
        r.Expect(TreeView_GetBkColor(untracked) == untrackedBefore, L"untracked tree-view untouched under None");
        DestroyWindow(top);
        return r.Result();
    }

    std::optional<COLORREF> ClientCorner(Recorder& r, HWND top)
    {
        RECT client{};
        GetClientRect(top, &client);
        POINT point{client.right - 20, client.bottom - 20};
        MapWindowPoints(top, nullptr, &point, 1);
        RECT window{};
        GetWindowRect(top, &window);
        return Sample(r, top, {point.x - window.left, point.y - window.top});
    }

    int ClassBrushEraseUsesPalette(const CheckContext&)
    {
        Recorder r(L"ClassBrushEraseUsesPalette");
        Fixture f = CreateFixture(L"ClassBrushEraseUsesPalette");
        Pump(400);
        const auto stock = ClientCorner(r, f.top);
        const PaletteResources* p = Activate(r, L"Campbell");
        if (p == nullptr)
        {
            return r.Result();
        }
        ApplyAll(f.top, p);
        Pump(400);
        if (const auto themed = ClientCorner(r, f.top))
        {
            r.Expect(*themed == Want(p, COLOR_3DFACE), L"COLOR_3DFACE + 1 class-brush client %06lX want %06lX", *themed, Want(p, COLOR_3DFACE));
        }
        PublishPalette(nullptr);
        ApplyAll(f.top, nullptr);
        Pump(400);
        if (const auto after = ClientCorner(r, f.top); after && stock)
        {
            r.Expect(*after == *stock, L"class-brush client restored after None (%06lX was %06lX)", *after, *stock);
        }
        DestroyFixture(f);
        return r.Result();
    }

    // An MFC-style window: its C++ state exists only once its own WM_CREATE handler has returned, and it
    // wraps every procedure installed during creation, as AfxWndProc does.
    int g_earlyStyleOrTheme = 0;
    bool g_pumpInCreate = false;
    const wchar_t kOriginalProc[] = L"TDPWHarnessOriginalProc";
    const wchar_t kReady[] = L"TDPWHarnessReady";

    LRESULT CALLBACK MfcStyleProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto original = reinterpret_cast<WNDPROC>(GetPropW(window, kOriginalProc));
        if ((message == WM_STYLECHANGING || message == WM_STYLECHANGED || message == WM_THEMECHANGED) && GetPropW(window, kReady) == nullptr)
        {
            ++g_earlyStyleOrTheme;
        }
        const LRESULT result = CallWindowProcW(original, window, message, wParam, lParam);
        if (message == WM_CREATE)
        {
            // As a modal loop, COM STA wait or MFC RunModalLoop inside OnCreate would: dispatch messages while
            // the window is still inside its own WM_CREATE.
            if (g_pumpInCreate)
            {
                const ULONGLONG until = GetTickCount64() + 60;
                MSG pending{};
                while (GetTickCount64() < until)
                {
                    while (PeekMessageW(&pending, nullptr, 0, 0, PM_REMOVE))
                    {
                        TranslateMessage(&pending);
                        DispatchMessageW(&pending);
                    }
                    Sleep(1);
                }
            }
            SetPropW(window, kReady, reinterpret_cast<HANDLE>(1));
        }
        else if (message == WM_NCDESTROY)
        {
            RemovePropW(window, kReady);
            RemovePropW(window, kOriginalProc);
        }
        return result;
    }

    LRESULT CALLBACK MfcStyleCbt(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code == HCBT_CREATEWND)
        {
            HWND window = reinterpret_cast<HWND>(wParam);
            const CREATESTRUCTW* cs = reinterpret_cast<CBT_CREATEWNDW*>(lParam)->lpcs;
            if (cs != nullptr && cs->lpszName != nullptr && !IS_INTRESOURCE(cs->lpszName) && wcscmp(cs->lpszName, L"MfcStyle") == 0)
            {
                SetPropW(window, kOriginalProc, reinterpret_cast<HANDLE>(GetWindowLongPtrW(window, GWLP_WNDPROC)));
                SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(MfcStyleProc));
            }
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    int CreateDeferralCheck(const wchar_t* name, bool pumpInCreate)
    {
        Recorder r(name);
        g_pumpInCreate = pumpInCreate;
        // Installed before the module's own CBT hook, so it runs after it and wraps outermost, as MFC does.
        if (SetWindowsHookExW(WH_CBT, MfcStyleCbt, nullptr, GetCurrentThreadId()) == nullptr || !TrackCreateMessages())
        {
            r.Invalidate(L"harness hooks could not be installed");
            return r.Result();
        }
        auto createWindows = [](HWND parent, HWND& tree, HWND& list, HWND& edit) {
            tree = CreateChild(WC_TREEVIEWW, parent, TVS_HASLINES | WS_BORDER, 10, 10, 230, 150, L"MfcStyle");
            list = CreateChild(WC_LISTVIEWW, parent, LVS_REPORT | WS_BORDER, 260, 10, 230, 150, L"MfcStyle");
            edit = CreateChild(L"EDIT", parent, WS_BORDER, 10, 180, 230, 30, L"MfcStyle");
        };
        // Some controls change their own style during creation; only changes beyond that stock count are ours.
        HWND stockTop = CreateTopLevel(L"ThemeNeverAppliedDuringCreate stock");
        HWND stockTree = nullptr;
        HWND stockList = nullptr;
        HWND stockEdit = nullptr;
        createWindows(stockTop, stockTree, stockList, stockEdit);
        Pump(300);
        const int stockEarly = g_earlyStyleOrTheme;
        DestroyWindow(stockTop);
        g_earlyStyleOrTheme = 0;

        const PaletteResources* p = Activate(r, L"Campbell");
        if (p == nullptr)
        {
            return r.Result();
        }
        HWND top = CreateTopLevel(L"ThemeNeverAppliedDuringCreate");
        HWND tree = nullptr;
        HWND list = nullptr;
        HWND edit = nullptr;
        createWindows(top, tree, list, edit);
        Pump(500);

        r.Expect(g_earlyStyleOrTheme == stockEarly,
                 L"style and theme changes reaching MFC-style windows before their WM_CREATE returned: %d, as without the module (%d)",
                 g_earlyStyleOrTheme, stockEarly);
        std::size_t duringCreate = 0;
        for (const RecordedCall& call : RecordedCalls(nullptr))
        {
            if (call.duringCreate && (call.kind == RecordedCall::Kind::SetWindowTheme || call.kind == RecordedCall::Kind::DwmSetWindowAttribute))
            {
                ++duringCreate;
            }
        }
        r.Expect(duringCreate == 0, L"no SetWindowTheme or DWM call with WM_NCCREATE/WM_CREATE on the stack (%u)", static_cast<unsigned>(duringCreate));
        r.Expect(Has(RecordedCalls(tree), RecordedCall::Kind::SetWindowTheme, L"DarkMode_Explorer"), L"tree-view themed once created");
        r.Expect(Has(RecordedCalls(list), RecordedCall::Kind::SetWindowTheme, L"DarkMode_DarkTheme"), L"list-view themed once created");
        r.Expect(Has(RecordedCalls(edit), RecordedCall::Kind::SetWindowTheme, L"DarkMode_CFD"), L"edit themed once created");
        r.Expect(Has(RecordedCalls(top), RecordedCall::Kind::DwmSetWindowAttribute), L"title bar applied once created");
        if (const auto bg = TreeBackground(r, tree))
        {
            r.Expect(*bg == Want(p, COLOR_WINDOW), L"deferred tree-view background %06lX want %06lX", *bg, Want(p, COLOR_WINDOW));
        }
        DestroyWindow(top);
        return r.Result();
    }

    int ThemeNeverAppliedDuringCreate(const CheckContext&)
    {
        return CreateDeferralCheck(L"ThemeNeverAppliedDuringCreate", false);
    }

    int ThemeDeferredWhileCreatePumps(const CheckContext&)
    {
        return CreateDeferralCheck(L"ThemeDeferredWhileCreatePumps", true);
    }

    std::optional<TextCell> TreeItemText(Recorder& r, HWND tree, COLORREF expected)
    {
        RECT item{};
        HTREEITEM root = TreeView_GetRoot(tree);
        if (root == nullptr || !TreeView_GetItemRect(tree, root, &item, TRUE))
        {
            r.Invalidate(L"tree-view item rectangle unavailable");
            return std::nullopt;
        }
        return SampleText(r, tree, item, expected);
    }

    int TreeViewTextFollowsPalette(const CheckContext&)
    {
        Recorder r(L"TreeViewTextFollowsPalette");
        HWND top = CreateTopLevel(L"TreeViewTextFollowsPalette");
        HWND existing = ShortTree(top, 10, 10);
        Pump(300);
        const COLORREF textBefore = TreeView_GetTextColor(existing);
        const auto stock = TreeItemText(r, existing, GetSysColor(COLOR_WINDOWTEXT));
        for (const wchar_t* name : {L"Tango Dark", L"One Half Dark", L"One Half Light"})
        {
            const PaletteResources* p = ActivePalette() == nullptr ? Activate(r, name) : PreparePalette(*FindScheme(name));
            PublishPalette(p);
            if (p == nullptr)
            {
                return r.Result();
            }
            ApplyAll(top, p);
            HWND created = ShortTree(top, 260, 10);
            Pump(400);
            RedrawWindow(top, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
            Pump(300);
            for (HWND tree : {existing, created})
            {
                if (const auto cell = TreeItemText(r, tree, Want(p, COLOR_WINDOWTEXT)))
                {
                    r.Expect(cell->text == Want(p, COLOR_WINDOWTEXT) && cell->textPixels > 10, L"%s %s tree-view item text %06lX (%d px) want %06lX", name,
                             tree == existing ? L"existing" : L"new", cell->text, cell->textPixels, Want(p, COLOR_WINDOWTEXT));
                    r.Expect(cell->background == Want(p, COLOR_WINDOW), L"%s %s tree-view item background %06lX want %06lX", name,
                             tree == existing ? L"existing" : L"new", cell->background, Want(p, COLOR_WINDOW));
                }
            }
            DestroyWindow(created);
        }
        PublishPalette(nullptr);
        ApplyAll(top, nullptr);
        Pump(400);
        r.Expect(TreeView_GetTextColor(existing) == textBefore, L"tree-view text colour restored on None (%08lX was %08lX)", TreeView_GetTextColor(existing),
                 textBefore);
        if (const auto after = TreeItemText(r, existing, GetSysColor(COLOR_WINDOWTEXT)); after && stock)
        {
            r.Expect(after->text == stock->text, L"tree-view item text pixels restored on None (%06lX was %06lX)", after->text, stock->text);
        }
        HWND untracked = ShortTree(top, 10, 180);
        TreeView_SetTextColor(untracked, RGB(0x30, 0x60, 0x90));
        ApplyAll(top, nullptr);
        r.Expect(TreeView_GetTextColor(untracked) == RGB(0x30, 0x60, 0x90), L"untracked tree-view text colour untouched under None");
        DestroyWindow(top);
        return r.Result();
    }

    std::optional<TextCell> HeaderText(Recorder& r, HWND header, COLORREF expected)
    {
        RECT item{};
        if (header == nullptr || !Header_GetItemRect(header, 0, &item))
        {
            r.Invalidate(L"header item rectangle unavailable");
            return std::nullopt;
        }
        InflateRect(&item, -2, -2);
        return SampleText(r, header, item, expected);
    }

    int HeaderTextFollowsPalette(const CheckContext&)
    {
        Recorder r(L"HeaderTextFollowsPalette");
        HWND top = CreateTopLevel(L"HeaderTextFollowsPalette");
        HWND list = ListWithScrollbar(top, 10, 10, 300, 160);
        HWND header = ListView_GetHeader(list);
        Pump(300);
        const auto stock = HeaderText(r, header, GetSysColor(COLOR_WINDOWTEXT));
        const PaletteResources* p = Activate(r, L"Tango Dark");
        if (p == nullptr)
        {
            return r.Result();
        }
        ApplyAll(top, p);
        Pump(500);
        if (const auto cell = HeaderText(r, header, Want(p, COLOR_WINDOWTEXT)))
        {
            const double contrast = Contrast(cell->text, cell->background);
            r.Expect(RelativeLuminance(cell->text) > RelativeLuminance(cell->background) && contrast >= 3.0,
                     L"dark scheme header text %06lX on %06lX is light and readable (%.2f:1)", cell->text, cell->background, contrast);
            r.Expect(RelativeLuminance(cell->background) < 0.2, L"dark scheme header background %06lX is dark", cell->background);
        }
        r.Expect(Has(RecordedCalls(header), RecordedCall::Kind::SetWindowTheme, L"DarkMode_ItemsView"), L"header given DarkMode_ItemsView");

        PublishPalette(nullptr);
        ApplyAll(top, nullptr);
        Pump(500);
        r.Expect(Has(RecordedCalls(header), RecordedCall::Kind::SetWindowTheme, L"(null)"), L"header theme reset on None");
        if (const auto after = HeaderText(r, header, GetSysColor(COLOR_WINDOWTEXT)); after && stock)
        {
            r.Expect(after->text == stock->text && after->background == stock->background, L"header pixels restored on None (%06lX on %06lX, was %06lX on %06lX)",
                     after->text, after->background, stock->text, stock->background);
        }
        DestroyWindow(top);
        return r.Result();
    }

    struct RowSample
    {
        COLORREF fill = 0;
        COLORREF text = 0;
        int textPixels = 0;
    };

    // Most common colour of a client rectangle of `control` (the row fill) and the colour furthest from it in
    // luminance (the text), from two stable captures of its top-level window with the focus left where it is.
    std::optional<RowSample> SampleRow(Recorder& r, HWND control, RECT row)
    {
        HWND top = GetAncestor(control, GA_ROOT);
        MapWindowPoints(control, top, reinterpret_cast<POINT*>(&row), 2);
        for (int attempt = 1; attempt <= 3; ++attempt)
        {
            Image first;
            Image second;
            if (!Capture(top, first) || !Capture(top, second))
            {
                r.Invalidate(L"PrintWindow failed");
                return std::nullopt;
            }
            if (row.left < 0 || row.top < 0 || row.right > first.width || row.bottom > first.height || row.right <= row.left || row.bottom <= row.top)
            {
                r.Invalidate(L"selected row outside the capture");
                return std::nullopt;
            }
            std::map<COLORREF, int> histogram;
            bool stable = true;
            for (int y = row.top; stable && y < row.bottom; ++y)
            {
                for (int x = row.left; x < row.right; ++x)
                {
                    const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(first.width) + static_cast<std::size_t>(x);
                    if (first.pixels[i] != second.pixels[i] || first.pixels[i] == 0x00FE01FE)
                    {
                        stable = false;
                        break;
                    }
                    ++histogram[first.At(x, y)];
                }
            }
            if (!stable)
            {
                Pump(100);
                continue;
            }
            RowSample sample;
            int most = 0;
            for (const auto& [c, n] : histogram)
            {
                if (n > most)
                {
                    most = n;
                    sample.fill = c;
                }
            }
            double furthest = -1.0;
            for (const auto& [c, n] : histogram)
            {
                const double d = std::abs(RelativeLuminance(c) - RelativeLuminance(sample.fill));
                if (n >= 3 && d > furthest)
                {
                    furthest = d;
                    sample.text = c;
                    sample.textPixels = n;
                }
            }
            return sample;
        }
        r.Invalidate(L"selected row undrawn or unstable in 3 capture pairs");
        return std::nullopt;
    }

    int SelectionUsesSubtleFill(const CheckContext&)
    {
        Recorder r(L"SelectionUsesSubtleFill");
        HWND top = CreateTopLevel(L"SelectionUsesSubtleFill");
        HWND listBox = CreateChild(L"LISTBOX", top, WS_BORDER | LBS_NOTIFY, 10, 10, 230, 150);
        for (const wchar_t* item : {L"WWWWWW one", L"WWWWWW two", L"WWWWWW three"})
        {
            SendMessageW(listBox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
        }
        SendMessageW(listBox, LB_SETCURSEL, 0, 0);
        // Created with LVS_SHOWSELALWAYS: the harness window cannot rely on keeping the focus.
        HWND list = CreateChild(WC_LISTVIEWW, top, LVS_REPORT | LVS_SHOWSELALWAYS | WS_BORDER, 260, 10, 230, 150);
        LVCOLUMNW column{LVCF_TEXT | LVCF_WIDTH, 0, 180, const_cast<wchar_t*>(L"Name")};
        ListView_InsertColumn(list, 0, &column);
        for (int i = 0; i < 3; ++i)
        {
            LVITEMW item{LVIF_TEXT};
            item.iItem = i;
            item.pszText = const_cast<wchar_t*>(L"WWWWWW");
            ListView_InsertItem(list, &item);
        }
        ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        Pump(300);

        for (const wchar_t* name : {L"Tango Dark", L"One Half Dark", L"One Half Light"})
        {
            const PaletteResources* p = ActivePalette() == nullptr ? Activate(r, name) : PreparePalette(*FindScheme(name));
            PublishPalette(p);
            if (p == nullptr)
            {
                return r.Result();
            }
            ApplyAll(top, p);
            SendMessageW(listBox, WM_SYSCOLORCHANGE, 0, 0);
            SetFocus(list);
            Pump(500);
            const SystemColourPalette& palette = p->palette;
            const COLORREF fill = *palette.At(COLOR_HIGHLIGHT);
            const COLORREF background = *palette.At(COLOR_WINDOW);

            RECT boxRow{};
            SendMessageW(listBox, LB_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&boxRow));
            if (const auto s = SampleRow(r, listBox, boxRow))
            {
                r.Note(L"%s list box selected fill %06lX text %06lX", name, s->fill, s->text);
                r.Expect(s->fill == fill, L"%s list box selection fill %06lX is the HIGHLIGHT blend %06lX", name, s->fill, fill);
                r.Expect(Contrast(s->text, s->fill) >= 3.0 && (RelativeLuminance(s->text) > RelativeLuminance(s->fill)) == palette.isDark,
                         L"%s list box selected text %06lX stays in the foreground's direction (%.2f:1)", name, s->text, Contrast(s->text, s->fill));
            }

            RECT listRow{};
            ListView_GetItemRect(list, 0, &listRow, LVIR_BOUNDS);
            listRow.right = std::min<LONG>(listRow.right, 170);
            if (const auto s = SampleRow(r, list, listRow))
            {
                r.Note(L"%s list-view selected fill %06lX text %06lX (background %06lX)", name, s->fill, s->text, background);
                if (palette.isDark)
                {
                    // DarkMode_Explorer draws its own selection; it must be visible but stay close to the background.
                    r.Expect(s->fill != background && std::abs(RelativeLuminance(s->fill) - RelativeLuminance(background)) <= 0.25,
                             L"%s themed list-view selection %06lX is a subtle fill against %06lX", name, s->fill, background);
                }
                else
                {
                    r.Expect(s->fill == fill, L"%s list-view selection fill %06lX is the HIGHLIGHT blend %06lX", name, s->fill, fill);
                }
                r.Expect(Contrast(s->text, s->fill) >= 3.0 && (RelativeLuminance(s->text) > RelativeLuminance(s->fill)) == palette.isDark,
                         L"%s list-view selected text %06lX stays in the foreground's direction (%.2f:1)", name, s->text, Contrast(s->text, s->fill));
            }
        }
        DestroyWindow(top);
        return r.Result();
    }

    bool NeutralGrey(COLORREF c)
    {
        const int red = GetRValue(c);
        const int green = GetGValue(c);
        const int blue = GetBValue(c);
        return std::abs(red - green) <= 12 && std::abs(green - blue) <= 12 && std::abs(red - blue) <= 12;
    }

    int ListViewSelectedAndHotTextReadable(const CheckContext&)
    {
        Recorder r(L"ListViewSelectedAndHotTextReadable");
        HWND top = CreateTopLevel(L"ListViewSelectedAndHotTextReadable");
        HWND list = CreateChild(WC_LISTVIEWW, top, LVS_REPORT | LVS_SHOWSELALWAYS | WS_BORDER, 10, 10, 360, 200);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        LVCOLUMNW column{LVCF_TEXT | LVCF_WIDTH, 0, 300, const_cast<wchar_t*>(L"Name")};
        ListView_InsertColumn(list, 0, &column);
        for (int i = 0; i < 4; ++i)
        {
            LVITEMW item{LVIF_TEXT};
            item.iItem = i;
            item.pszText = const_cast<wchar_t*>(L"WWWWWW WWWWWW");
            ListView_InsertItem(list, &item);
        }
        Pump(300);
        for (const wchar_t* name : {L"Tango Dark", L"One Half Dark"})
        {
            const PaletteResources* p = ActivePalette() == nullptr ? Activate(r, name) : PreparePalette(*FindScheme(name));
            PublishPalette(p);
            if (p == nullptr)
            {
                return r.Result();
            }
            ApplyAll(top, p);
            ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_SetHotItem(list, 2);
            Pump(500);
            const COLORREF background = *p->palette.At(COLOR_WINDOW);
            for (const auto& [index, label] : {std::pair<int, const wchar_t*>{0, L"selected"}, std::pair<int, const wchar_t*>{2, L"hot"}})
            {
                RECT row{};
                ListView_GetItemRect(list, index, &row, LVIR_BOUNDS);
                row.right = std::min<LONG>(row.right, 300);
                if (const auto s = SampleRow(r, list, row))
                {
                    r.Note(L"%s %s row fill %06lX text %06lX", name, label, s->fill, s->text);
                    r.Expect(s->fill != background, L"%s %s row is visibly marked (%06lX)", name, label, s->fill);
                    r.Expect(NeutralGrey(s->fill), L"%s %s row fill %06lX is a neutral grey", name, label, s->fill);
                    r.Expect(RelativeLuminance(s->text) > RelativeLuminance(s->fill) && Contrast(s->text, s->fill) >= 3.0,
                             L"%s %s row text %06lX is light and readable on %06lX (%.2f:1)", name, label, s->text, s->fill, Contrast(s->text, s->fill));
                }
            }
        }
        DestroyWindow(top);
        return r.Result();
    }

    // Custom-draws as Bentley's access-control list does: alternate rows on the stock 3DFACE colour it
    // cached, with stock text, and one row in an application colour.
    LRESULT CALLBACK CustomDrawParentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(lParam)->code == NM_CUSTOMDRAW)
        {
            auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
            if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
            {
                return CDRF_NOTIFYITEMDRAW;
            }
            if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
            {
                if (draw->nmcd.dwItemSpec % 2 == 1)
                {
                    draw->clrTextBk = RGB(240, 240, 240);
                    draw->clrText = RGB(0, 0, 0);
                }
                else if (draw->nmcd.dwItemSpec == 2)
                {
                    draw->clrTextBk = RGB(0x40, 0x80, 0x40);
                }
                return CDRF_DODEFAULT;
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    int CustomDrawStockColoursRemapped(const CheckContext&)
    {
        Recorder r(L"CustomDrawStockColoursRemapped");
        WNDCLASSW wc{};
        wc.lpfnWndProc = CustomDrawParentProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"TDPWHarnessCustomDrawParent";
        RegisterClassW(&wc);
        const PaletteResources* p = Activate(r, L"Tango Dark");
        if (p == nullptr)
        {
            return r.Result();
        }
        HWND top = CreateTopLevel(L"CustomDrawStockColoursRemapped");
        HWND parent = CreateChild(L"TDPWHarnessCustomDrawParent", top, 0, 10, 10, 420, 240);
        HWND list = CreateChild(WC_LISTVIEWW, parent, LVS_REPORT | WS_BORDER, 10, 10, 380, 200);
        LVCOLUMNW column{LVCF_TEXT | LVCF_WIDTH, 0, 300, const_cast<wchar_t*>(L"Name")};
        ListView_InsertColumn(list, 0, &column);
        for (int i = 0; i < 4; ++i)
        {
            LVITEMW item{LVIF_TEXT};
            item.iItem = i;
            item.pszText = const_cast<wchar_t*>(L"WWWWWW WWWWWW");
            ListView_InsertItem(list, &item);
        }
        Pump(500);
        auto row = [&](int index) {
            RECT rect{};
            ListView_GetItemRect(list, index, &rect, LVIR_LABEL);
            return SampleRow(r, list, rect);
        };
        if (const auto odd = row(1))
        {
            r.Note(L"stock-coloured custom row fill %06lX text %06lX", odd->fill, odd->text);
            r.Expect(odd->fill == Want(p, COLOR_3DFACE), L"custom-drawn stock 3DFACE row shows palette 3DFACE (%06lX want %06lX)", odd->fill, Want(p, COLOR_3DFACE));
            r.Expect(RelativeLuminance(odd->text) > RelativeLuminance(odd->fill) && Contrast(odd->text, odd->fill) >= 3.0,
                     L"custom-drawn stock text %06lX is light and readable (%.2f:1)", odd->text, Contrast(odd->text, odd->fill));
        }
        if (const auto own = row(2))
        {
            r.Expect(own->fill == RGB(0x40, 0x80, 0x40), L"application-coloured row untouched (%06lX)", own->fill);
        }
        DestroyWindow(top);
        return r.Result();
    }

    int TooltipFollowsDarkScheme(const CheckContext&)
    {
        Recorder r(L"TooltipFollowsDarkScheme");
        HWND top = CreateTopLevel(L"TooltipFollowsDarkScheme");
        InstallCallRecorder();
        StartTheming();
        if (!r.Expect(Switch(FindScheme(L"Tango Dark")), L"switch to Tango Dark"))
        {
            return r.Result();
        }
        HWND tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT,
                                   CW_USEDEFAULT, CW_USEDEFAULT, top, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(tip, WM_SETFONT, reinterpret_cast<WPARAM>(AliasedFont()), TRUE);
        TTTOOLINFOW tool{sizeof(tool)};
        tool.uFlags = TTF_TRACK | TTF_ABSOLUTE;
        tool.hwnd = top;
        tool.uId = 1;
        tool.lpszText = const_cast<wchar_t*>(L"WWWWWW WWWWWW");
        SendMessageW(tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        Pump(200);
        RECT window{};
        GetWindowRect(top, &window);
        SendMessageW(tip, TTM_TRACKPOSITION, 0, MAKELPARAM(window.left + 60, window.top + 120));
        SendMessageW(tip, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&tool));
        Pump(500);
        RECT client{};
        GetClientRect(tip, &client);
        InflateRect(&client, -1, -1);
        if (const auto cell = SampleText(r, tip, client, RGB(0xFF, 0xFF, 0xFF)))
        {
            r.Note(L"tooltip background %06lX text %06lX", cell->background, cell->text);
            r.Expect(RelativeLuminance(cell->background) < 0.2, L"tooltip background %06lX is dark", cell->background);
            r.Expect(RelativeLuminance(cell->text) > RelativeLuminance(cell->background) && Contrast(cell->text, cell->background) >= 3.0,
                     L"tooltip text %06lX is light and readable (%.2f:1)", cell->text, Contrast(cell->text, cell->background));
        }
        r.Expect(Has(RecordedCalls(tip), RecordedCall::Kind::SetWindowTheme, L"DarkMode_Explorer"), L"tooltip given DarkMode_Explorer");
        r.Expect(Switch(nullptr), L"switch to None");
        Pump(300);
        r.Expect(Has(RecordedCalls(tip), RecordedCall::Kind::SetWindowTheme, L"(null)"), L"tooltip theme reset on None");
        SendMessageW(tip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool));
        DestroyWindow(tip);
        DestroyWindow(top);
        return r.Result();
    }

    bool WINAPI ThemeMissing(LPCWSTR) noexcept
    {
        return false;
    }

    int ListViewFallsBackWithoutDarkTheme(const CheckContext&)
    {
        Recorder r(L"ListViewFallsBackWithoutDarkTheme");
        ControlSeams::themeAvailable = ThemeMissing;
        const PaletteResources* p = Activate(r, L"One Half Dark");
        if (p == nullptr)
        {
            return r.Result();
        }
        HWND top = CreateTopLevel(L"ListViewFallsBackWithoutDarkTheme");
        HWND list = ListWithScrollbar(top, 10, 10, 300, 160);
        ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        Pump(500);
        r.Expect(Has(RecordedCalls(list), RecordedCall::Kind::SetWindowTheme, L"DarkMode_Explorer"), L"fallback theme DarkMode_Explorer applied");
        r.Expect(!Has(RecordedCalls(list), RecordedCall::Kind::SetWindowTheme, L"DarkMode_DarkTheme"), L"missing theme not applied");
        r.Expect(ListView_GetTextColor(list) == Want(p, COLOR_WINDOWTEXT), L"list-view text colour set to the foreground (%06lX)", ListView_GetTextColor(list));
        RECT row{};
        ListView_GetItemRect(list, 1, &row, LVIR_LABEL);
        if (const auto s = SampleRow(r, list, row))
        {
            r.Expect(RelativeLuminance(s->text) > RelativeLuminance(s->fill) && Contrast(s->text, s->fill) >= 3.0, L"fallback row text %06lX light on %06lX", s->text,
                     s->fill);
        }
        r.Expect(LogLines(L"DarkMode_DarkTheme unavailable") == 1, L"fallback logged once");
        DestroyWindow(top);
        return r.Result();
    }

    int ListViewTextColourRestoredOnNone(const CheckContext&)
    {
        Recorder r(L"ListViewTextColourRestoredOnNone");
        HWND top = CreateTopLevel(L"ListViewTextColourRestoredOnNone");
        HWND list = ListWithScrollbar(top, 10, 10, 300, 160);
        Pump(200);
        const COLORREF before = ListView_GetTextColor(list);
        const PaletteResources* p = Activate(r, L"Tango Dark");
        if (p == nullptr)
        {
            return r.Result();
        }
        ApplyAll(top, p);
        Pump(300);
        r.Expect(ListView_GetTextColor(list) == Want(p, COLOR_WINDOWTEXT), L"dark scheme sets the list-view text colour");
        PublishPalette(nullptr);
        ApplyAll(top, nullptr);
        Pump(300);
        r.Expect(ListView_GetTextColor(list) == before, L"list-view text colour restored on None (%08lX was %08lX)", ListView_GetTextColor(list), before);
        r.Expect(Has(RecordedCalls(list), RecordedCall::Kind::SetWindowTheme, L"(null)"), L"list-view theme reset on None");
        DestroyWindow(top);
        return r.Result();
    }

    int LateHeaderFollowsPalette(const CheckContext&)
    {
        Recorder r(L"LateHeaderFollowsPalette");
        const PaletteResources* p = Activate(r, L"One Half Dark");
        if (p == nullptr)
        {
            return r.Result();
        }
        HWND top = CreateTopLevel(L"LateHeaderFollowsPalette");
        HWND list = CreateChild(WC_LISTVIEWW, top, LVS_ICON, 10, 10, 300, 160);
        Pump(500);
        if (ListView_GetHeader(list) != nullptr)
        {
            r.Invalidate(L"the list-view created its header before it was themed");
            DestroyWindow(top);
            return r.Result();
        }
        r.Expect(Has(RecordedCalls(list), RecordedCall::Kind::SetWindowTheme, L"DarkMode_DarkTheme"), L"list-view themed before its header exists");
        ListView_SetView(list, LV_VIEW_DETAILS);
        wchar_t title[] = L"WWWWWW";
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.cx = 200;
        column.pszText = title;
        ListView_InsertColumn(list, 0, &column);
        Pump(500);
        HWND header = ListView_GetHeader(list);
        if (header == nullptr)
        {
            r.Invalidate(L"the list-view created no header in details view");
            DestroyWindow(top);
            return r.Result();
        }
        r.Expect(Has(RecordedCalls(header), RecordedCall::Kind::SetWindowTheme, L"DarkMode_ItemsView"), L"late header given DarkMode_ItemsView");
        if (const auto cell = HeaderText(r, header, Want(p, COLOR_WINDOWTEXT)))
        {
            const double contrast = Contrast(cell->text, cell->background);
            r.Expect(RelativeLuminance(cell->text) > RelativeLuminance(cell->background) && contrast >= 3.0,
                     L"late header text %06lX on %06lX is light and readable (%.2f:1)", cell->text, cell->background, contrast);
            r.Expect(RelativeLuminance(cell->background) < 0.2, L"late header background %06lX is dark", cell->background);
        }
        PublishPalette(nullptr);
        ApplyAll(top, nullptr);
        Pump(300);
        r.Expect(Has(RecordedCalls(header), RecordedCall::Kind::SetWindowTheme, L"(null)"), L"late header theme reset on None");
        DestroyWindow(top);
        return r.Result();
    }

    // Builds its view, list-view (with header) and scroll bar inside its own WM_CREATE, as MFC frames do.
    LRESULT CALLBACK ViewFrameProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_CREATE)
        {
            HINSTANCE instance = GetModuleHandleW(nullptr);
            HWND view = CreateWindowExW(WS_EX_CLIENTEDGE, L"TDPWHarnessView", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 500, 300, window, nullptr,
                                        instance, nullptr);
            HWND list = CreateWindowExW(0, WC_LISTVIEWW, nullptr, WS_CHILD | WS_VISIBLE | LVS_REPORT, 10, 10, 300, 200, view, reinterpret_cast<HMENU>(1), instance,
                                        nullptr);
            wchar_t title[] = L"WWWWWW";
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH;
            column.cx = 200;
            column.pszText = title;
            ListView_InsertColumn(list, 0, &column);
            CreateWindowExW(0, L"SCROLLBAR", nullptr, WS_CHILD | WS_VISIBLE | SBS_VERT, 330, 10, 20, 200, view, reinterpret_cast<HMENU>(2), instance, nullptr);
            return view != nullptr ? 0 : -1;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    struct FrameSample
    {
        std::optional<COLORREF> caption;
        std::optional<TextCell> header;
        std::optional<COLORREF> scrollbar;
        std::optional<DWORD> darkMode;
    };

    FrameSample SampleFrame(Recorder& r, HWND frame)
    {
        FrameSample f;
        RECT window{};
        GetWindowRect(frame, &window);
        const int middle = (window.right - window.left) / 2;
        if (const auto caption = CaptureRegion(r, frame, {middle - 40, 8, middle + 40, 20}))
        {
            f.caption = MostCommon(*caption);
        }
        HWND view = GetWindow(frame, GW_CHILD);
        HWND list = view != nullptr ? GetDlgItem(view, 1) : nullptr;
        HWND scrollbar = view != nullptr ? GetDlgItem(view, 2) : nullptr;
        if (list == nullptr || scrollbar == nullptr || ListView_GetHeader(list) == nullptr)
        {
            r.Invalidate(L"frame children missing");
            return f;
        }
        f.header = HeaderText(r, ListView_GetHeader(list), GetSysColor(COLOR_WINDOWTEXT));
        f.scrollbar = Sample(r, scrollbar, {10, 100});
        for (const RecordedCall& call : RecordedCalls(frame))
        {
            if (call.kind == RecordedCall::Kind::DwmSetWindowAttribute && call.attribute == DWMWA_USE_IMMERSIVE_DARK_MODE)
            {
                f.darkMode = call.value;
            }
        }
        return f;
    }

    void ExpectSameAsRefresh(Recorder& r, const FrameSample& created, const FrameSample& refreshed, const wchar_t* which)
    {
        if (created.caption && refreshed.caption)
        {
            r.Expect(*created.caption == *refreshed.caption, L"%s caption %06lX matches the live switch %06lX", which, *created.caption, *refreshed.caption);
        }
        r.Expect(created.darkMode == refreshed.darkMode && refreshed.darkMode.has_value(), L"%s dark title bar attribute %ld matches the live switch %ld", which,
                 static_cast<long>(created.darkMode.value_or(99)), static_cast<long>(refreshed.darkMode.value_or(99)));
        if (created.header && refreshed.header)
        {
            r.Expect(created.header->background == refreshed.header->background && created.header->text == refreshed.header->text,
                     L"%s header %06lX on %06lX matches the live switch %06lX on %06lX", which, created.header->text, created.header->background,
                     refreshed.header->text, refreshed.header->background);
        }
        if (created.scrollbar && refreshed.scrollbar)
        {
            r.Expect(*created.scrollbar == *refreshed.scrollbar, L"%s scroll bar %06lX matches the live switch %06lX", which, *created.scrollbar,
                     *refreshed.scrollbar);
        }
    }

    // Two frames created while a dark scheme is active: one with its caption from the start, and one created
    // as a captionless popup that is given its caption once created, as MFC does when it floats a window.
    int NewTopLevelWindowThemedLikeRefresh(const CheckContext&)
    {
        Recorder r(L"NewTopLevelWindowThemedLikeRefresh");
        HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW frameClass{sizeof(frameClass)};
        frameClass.lpfnWndProc = ViewFrameProc;
        frameClass.hInstance = instance;
        frameClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        frameClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_3DFACE + 1);
        frameClass.lpszClassName = L"TDPWHarnessViewFrame";
        WNDCLASSEXW viewClass{sizeof(viewClass)};
        viewClass.lpfnWndProc = DefWindowProcW;
        viewClass.hInstance = instance;
        viewClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        viewClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        viewClass.lpszClassName = L"TDPWHarnessView";
        if (!r.Expect(RegisterClassExW(&frameClass) != 0 && RegisterClassExW(&viewClass) != 0, L"classes registered") ||
            !r.Expect(InstallCallRecorder(), L"call recorder installed"))
        {
            return r.Result();
        }
        StartTheming();
        const ColourScheme* scheme = FindScheme(L"One Half Dark");
        r.Expect(Switch(scheme), L"switch to One Half Dark");
        Pump(300);

        HWND captioned = CreateWindowExW(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE, frameClass.lpszClassName, L"Captioned frame", WS_OVERLAPPEDWINDOW, 40, 40, 520,
                                         360, nullptr, nullptr, instance, nullptr);
        HWND floated = CreateWindowExW(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE, frameClass.lpszClassName, L"Floated frame", WS_POPUP | WS_CLIPCHILDREN, 600, 40, 520,
                                       360, nullptr, nullptr, instance, nullptr);
        if (captioned == nullptr || floated == nullptr)
        {
            r.Invalidate(L"frames not created");
            return r.Result();
        }
        SetWindowLongPtrW(floated, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN);
        SetWindowPos(floated, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        ShowWindow(captioned, SW_SHOWNORMAL);
        ShowWindow(floated, SW_SHOWNORMAL);
        Pump(600);
        const FrameSample createdCaptioned = SampleFrame(r, captioned);
        const FrameSample createdFloated = SampleFrame(r, floated);

        r.Expect(Switch(nullptr), L"switch to None");
        Pump(400);
        r.Expect(Switch(scheme), L"switch back to One Half Dark");
        Pump(600);
        ExpectSameAsRefresh(r, createdCaptioned, SampleFrame(r, captioned), L"captioned frame");
        ExpectSameAsRefresh(r, createdFloated, SampleFrame(r, floated), L"floated frame");
        Switch(nullptr);
        DestroyWindow(captioned);
        DestroyWindow(floated);
        return r.Result();
    }

    struct PaintCounts
    {
        int paint = 0;
        int ncPaint = 0;
        int sysColorChange = 0;
    };

    LRESULT CALLBACK CountPaintsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data)
    {
        auto* counts = reinterpret_cast<PaintCounts*>(data);
        if (message == WM_PAINT)
        {
            ++counts->paint;
        }
        else if (message == WM_NCPAINT)
        {
            ++counts->ncPaint;
        }
        else if (message == WM_SYSCOLORCHANGE)
        {
            ++counts->sysColorChange;
        }
        return DefSubclassProc(window, message, wParam, lParam);
    }

    LRESULT CALLBACK HoverPopupProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_CREATE)
        {
            CreateWindowExW(0, L"STATIC", L"Preview", WS_CHILD | WS_VISIBLE, 4, 4, 120, 20, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    struct Watched
    {
        const wchar_t* name;
        HWND window;
        PaintCounts counts;
    };

    struct PopupWork
    {
        int tooltipRedraws = 0;
        int popupChanges = 0;
    };

    PopupWork g_popupWork;

    // Hover creates and destroys tooltips and captionless popups (some with children) continuously.
    void HoverChurn(std::vector<Watched>& watched, HWND owner, int rounds)
    {
        for (Watched& w : watched)
        {
            w.counts = {};
            ClearRecordedCalls();
        }
        for (int i = 0; i < rounds; ++i)
        {
            HWND tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0, owner, nullptr,
                                       GetModuleHandleW(nullptr), nullptr);
            HWND popup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"TDPWHarnessHoverPopup", nullptr, WS_POPUP | WS_BORDER, 700, 100 + (i % 5), 160,
                                         40, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
            HWND bare = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"TDPWHarnessUnknown", nullptr, WS_POPUP, 700, 200, 80, 20, owner, nullptr,
                                        GetModuleHandleW(nullptr), nullptr);
            ShowWindow(popup, SW_SHOWNOACTIVATE);
            ShowWindow(bare, SW_SHOWNOACTIVATE);
            Pump(15);
            for (const RecordedCall& call : RecordedCalls(tip))
            {
                g_popupWork.tooltipRedraws += call.kind == RecordedCall::Kind::RedrawWindow ? 1 : 0;
            }
            for (HWND h : {popup, bare})
            {
                for (const RecordedCall& call : RecordedCalls(h))
                {
                    g_popupWork.popupChanges += call.kind == RecordedCall::Kind::RedrawWindow || call.kind == RecordedCall::Kind::SetWindowTheme ||
                                                        call.kind == RecordedCall::Kind::DwmSetWindowAttribute
                                                    ? 1
                                                    : 0;
                }
            }
            // Some code toggles a frame's caption bits in passing, which must not re-run its theming.
            if (i % 10 == 0)
            {
                const LONG_PTR style = GetWindowLongPtrW(owner, GWL_STYLE);
                SetWindowLongPtrW(owner, GWL_STYLE, style & ~static_cast<LONG_PTR>(WS_CAPTION));
                SetWindowLongPtrW(owner, GWL_STYLE, style);
            }
            DestroyWindow(tip);
            DestroyWindow(popup);
            DestroyWindow(bare);
        }
        Pump(300);
    }

    int CountThemeCalls(HWND window)
    {
        int n = 0;
        for (const RecordedCall& call : RecordedCalls(window))
        {
            n += call.kind == RecordedCall::Kind::SetWindowTheme ? 1 : 0;
        }
        return n;
    }

    int HoverChurnDoesNotRepaintFrame(const CheckContext&)
    {
        Recorder r(L"HoverChurnDoesNotRepaintFrame");
        HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW popupClass{sizeof(popupClass)};
        popupClass.lpfnWndProc = HoverPopupProc;
        popupClass.hInstance = instance;
        popupClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_INFOBK + 1);
        popupClass.lpszClassName = L"TDPWHarnessHoverPopup";
        WNDCLASSW unknownClass{};
        unknownClass.lpfnWndProc = DefWindowProcW;
        unknownClass.hInstance = instance;
        unknownClass.lpszClassName = L"TDPWHarnessUnknown";
        RegisterClassW(&unknownClass);
        if (!r.Expect(RegisterClassExW(&popupClass) != 0, L"popup class registered") || !r.Expect(InstallCallRecorder(), L"call recorder installed"))
        {
            return r.Result();
        }
        StartTheming();
        HWND frame = CreateTopLevel(L"HoverChurnDoesNotRepaintFrame", 620, 420);
        HWND list = ListWithScrollbar(frame, 10, 10, 280, 200);
        HWND tree = ShortTree(frame, 300, 10, 280, 200);
        std::vector<Watched> watched{{L"frame", frame, {}}, {L"list-view", list, {}}, {L"tree-view", tree, {}}, {L"header", ListView_GetHeader(list), {}}};
        for (Watched& w : watched)
        {
            SetWindowSubclass(w.window, CountPaintsProc, 0x54445048, reinterpret_cast<DWORD_PTR>(&w.counts));
        }
        Pump(300);
        constexpr int kRounds = 200;

        HoverChurn(watched, frame, kRounds);
        std::vector<PaintCounts> stock;
        std::vector<int> stockThemeCalls;
        for (const Watched& w : watched)
        {
            stock.push_back(w.counts);
            stockThemeCalls.push_back(CountThemeCalls(w.window));
        }

        r.Expect(Switch(FindScheme(L"Tango Dark")), L"switch to Tango Dark");
        Pump(500);
        g_popupWork = {};
        HoverChurn(watched, frame, kRounds);
        r.Expect(g_popupWork.popupChanges == 0, L"captionless popups left alone (%d theme, DWM or redraw calls)", g_popupWork.popupChanges);
        r.Expect(g_popupWork.tooltipRedraws == 0, L"tooltips themed without forced redraws (%d)", g_popupWork.tooltipRedraws);
        for (std::size_t i = 0; i < watched.size(); ++i)
        {
            const Watched& w = watched[i];
            r.Note(L"%s: paint %d/%d nc %d/%d syscolor %d/%d (dark/stock)", w.name, w.counts.paint, stock[i].paint, w.counts.ncPaint, stock[i].ncPaint,
                   w.counts.sysColorChange, stock[i].sysColorChange);
            r.Expect(w.counts.paint == stock[i].paint, L"%s WM_PAINT %d during hover (stock %d)", w.name, w.counts.paint, stock[i].paint);
            r.Expect(w.counts.ncPaint == stock[i].ncPaint, L"%s WM_NCPAINT %d during hover (stock %d)", w.name, w.counts.ncPaint, stock[i].ncPaint);
            r.Expect(w.counts.sysColorChange == stock[i].sysColorChange, L"%s WM_SYSCOLORCHANGE %d during hover (stock %d)", w.name, w.counts.sysColorChange,
                     stock[i].sysColorChange);
            r.Expect(CountThemeCalls(w.window) == stockThemeCalls[i], L"%s SetWindowTheme %d during hover (stock %d)", w.name, CountThemeCalls(w.window),
                     stockThemeCalls[i]);
        }
        for (Watched& w : watched)
        {
            RemoveWindowSubclass(w.window, CountPaintsProc, 0x54445048);
        }
        Switch(nullptr);
        DestroyWindow(frame);
        return r.Result();
    }


    HBRUSH g_cachedWhite = nullptr;
    HBRUSH g_cachedGrey = nullptr;
    HBRUSH g_accent = nullptr;
    bool g_dcRestored = true;

    LRESULT CALLBACK CachedGridProc(HWND window, UINT message, WPARAM w, LPARAM l)
    {
        if (message == WM_PRINTCLIENT)
        {
            HDC dc = reinterpret_cast<HDC>(w);
            SetTextColor(dc, RGB(0, 0, 0));
            SetBkColor(dc, RGB(255, 255, 255));
            RECT row{0, 0, 100, 30}, header{0, 30, 100, 60}, accent{0, 60, 100, 90};
            FillRect(dc, &row, g_cachedWhite);
            FillRect(dc, &header, g_cachedGrey);
            FillRect(dc, &accent, g_accent);
            RECT opaque{0, 90, 100, 120};
            ExtTextOutW(dc, 0, 90, ETO_OPAQUE, &opaque, L"", 0, nullptr);
            auto previous = SelectObject(dc, g_cachedWhite);
            PatBlt(dc, 0, 120, 100, 30, PATCOPY);
            g_dcRestored = GetCurrentObject(dc, OBJ_BRUSH) == g_cachedWhite &&
                GetTextColor(dc) == RGB(0, 0, 0) && GetBkColor(dc) == RGB(255, 255, 255);
            SelectObject(dc, previous);
            return 0;
        }
        return DefWindowProcW(window, message, w, l);
    }

    int CachedGridColoursFollowEveryScheme(const CheckContext&)
    {
        Recorder r(L"CachedGridColoursFollowEveryScheme");
        g_cachedWhite = CreateSolidBrush(RGB(255, 255, 255));
        g_cachedGrey = CreateSolidBrush(RGB(192, 192, 192));
        g_accent = CreateSolidBrush(RGB(220, 30, 40));
        WNDCLASSW wc{};
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpfnWndProc = CachedGridProc;
        wc.lpszClassName = L"ProjectPreviewCtrlView";
        if (!r.Expect(RegisterClassW(&wc) != 0, L"grid class registered")) return r.Result();
        StartTheming();
        HWND top = CreateTopLevel(L"CachedGridColoursFollowEveryScheme");
        HWND grid = CreateChild(wc.lpszClassName, top, 0, 0, 0, 100, 150);
        HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
        HBITMAP bitmap = CreateCompatibleBitmap(screen, 100, 150);
        ReleaseDC(nullptr, screen);
        HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
        for (const auto& scheme : Schemes())
        {
            r.Expect(Switch(&scheme), L"switch %s", scheme.name);
            Pump(50);
            SendMessageW(grid, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
            const auto* p = ActivePalette();
            r.Expect(p != nullptr, L"palette published");
            if (!p) break;
            r.Expect(GetPixel(dc, 50, 15) == Want(p, COLOR_WINDOW), L"cached white row follows %s", scheme.name);
            r.Expect(GetPixel(dc, 50, 45) == Want(p, COLOR_3DFACE), L"cached grey header follows %s", scheme.name);
            r.Expect(GetPixel(dc, 50, 75) == RGB(220, 30, 40), L"accent preserved");
            r.Expect(GetPixel(dc, 50, 105) == Want(p, COLOR_WINDOW), L"opaque text background follows palette");
            r.Expect(GetPixel(dc, 50, 135) == Want(p, COLOR_WINDOW), L"selected brush drawing follows palette");
            r.Expect(g_dcRestored, L"drawing restores DC state");
            RECT outside{0, 0, 100, 30};
            FillRect(dc, &outside, g_cachedWhite);
            r.Expect(GetPixel(dc, 50, 15) == RGB(255, 255, 255), L"drawing outside grid is unchanged");
            r.Expect(BrushColour(g_cachedWhite) == RGB(255, 255, 255), L"cached brush remains unchanged");
            Switch(nullptr);
            SendMessageW(grid, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
            r.Expect(GetPixel(dc, 50, 15) == RGB(255, 255, 255) && GetPixel(dc, 50, 45) == RGB(192, 192, 192), L"None restores original grid colours");
        }
        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap); DeleteDC(dc);
        DestroyWindow(top);
        DeleteObject(g_cachedWhite); DeleteObject(g_cachedGrey); DeleteObject(g_accent);
        return r.Result();
    }

    int BufferedListDoesNotEraseOutsidePaint(const CheckContext&)
    {
        Recorder r(L"BufferedListDoesNotEraseOutsidePaint");
        StartTheming();
        HWND top = CreateTopLevel(L"BufferedListDoesNotEraseOutsidePaint");
        HWND list = ListWithScrollbar(top, 0, 0);
        ListView_SetExtendedListViewStyleEx(list, LVS_EX_DOUBLEBUFFER, LVS_EX_DOUBLEBUFFER);
        HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
        HBITMAP bitmap = CreateCompatibleBitmap(screen, 230, 150);
        ReleaseDC(nullptr, screen);
        HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
        HBRUSH sentinel = CreateSolidBrush(RGB(255, 0, 255));
        RECT bounds{0, 0, 230, 150};
        FillRect(dc, &bounds, sentinel);
        SendMessageW(list, WM_ERASEBKGND, reinterpret_cast<WPARAM>(dc), 0);
        COLORREF stockText = ListView_GetTextColor(list);
        COLORREF stock = GetPixel(dc, 200, 120);
        r.Expect(stock == RGB(255, 0, 255), L"stock buffered control defers erasure to paint");
        for (const auto& scheme : Schemes())
        {
            Switch(&scheme);
            FillRect(dc, &bounds, sentinel);
            SendMessageW(list, WM_ERASEBKGND, reinterpret_cast<WPARAM>(dc), 0);
            r.Expect(GetPixel(dc, 200, 120) == stock, L"%s preserves buffered erasure", scheme.name);
            HWND late = ListWithScrollbar(top, 0, 0);
            Pump(50);
            Switch(nullptr);
            r.Expect(ListView_GetTextColor(list) == stockText, L"existing list restores original text colour");
            r.Expect(ListView_GetTextColor(late) == CLR_DEFAULT, L"list created under theme restores default text colour");
            DestroyWindow(late);
        }
        Switch(nullptr);
        DestroyWindow(top);
        SelectObject(dc, oldBitmap); DeleteObject(bitmap); DeleteObject(sentinel); DeleteDC(dc);
        return r.Result();
    }


    HDDEDATA CALLBACK DdeCallback(UINT, UINT, HCONV, HSZ, HSZ, HDDEDATA, ULONG_PTR, ULONG_PTR) { return nullptr; }

    LRESULT CALLBACK AccessFixtureProc(HWND window, UINT message, WPARAM w, LPARAM l)
    {
        if (message == WM_PRINTCLIENT)
        {
            HDC dc = reinterpret_cast<HDC>(w);
            RECT bounds{0, 0, 240, 70};
            FillRect(dc, &bounds, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            SetBkColor(dc, RGB(240, 240, 240));
            RECT fill{0, 0, 120, 20};
            ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &fill, nullptr, 0, nullptr);
            fill = {120, 0, 240, 20};
            ExtTextOutA(dc, 0, 0, ETO_OPAQUE, &fill, nullptr, 0, nullptr);
            for (int i = 0; i < 6; ++i)
            {
                RECT box{5 + i * 35, 30, 22 + i * 35, 47};
                const UINT state = DFCS_BUTTONCHECK | ((i & 1) != 0 ? DFCS_CHECKED : 0) |
                    (i >= 4 ? DFCS_PUSHED : i >= 2 ? DFCS_INACTIVE : 0);
                DrawFrameControl(dc, &box, DFC_BUTTON, state);
            }
            return 0;
        }
        return DefWindowProcW(window, message, w, l);
    }
    int AccessGridFillsAndCheckStates(const CheckContext&)
    {
        Recorder r(L"AccessGridFillsAndCheckStates");
        WNDCLASSW wc{}; wc.hInstance = GetModuleHandleW(nullptr); wc.lpfnWndProc = AccessFixtureProc;
        wc.lpszClassName = L"PWUserAccessWindowClass";
        if (!r.Expect(RegisterClassW(&wc) != 0, L"Access Control fixture class registered")) return r.Result();
        HWND host = CreateWindowExW(0, wc.lpszClassName, L"Permission paint fixture", WS_OVERLAPPEDWINDOW,
                                   0, 0, 300, 180, nullptr, nullptr, wc.hInstance, nullptr);
        HWND toolbar = CreateChild(TOOLBARCLASSNAMEW, host, 0, 0, 80, 240, 24);
        HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
        HBITMAP bitmap = CreateCompatibleBitmap(screen, 240, 70);
        ReleaseDC(nullptr, screen);
        HGDIOBJ old = SelectObject(dc, bitmap);
        StartTheming();
        for (const auto& scheme : Schemes())
        {
            Switch(&scheme); Pump(40);
            SendMessageW(host, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
            r.Expect(GetPixel(dc, 50, 10) == Want(ActivePalette(), COLOR_3DFACE) && GetPixel(dc, 180, 10) == Want(ActivePalette(), COLOR_3DFACE),
                     L"%s ANSI and Unicode opaque row fills follow palette", scheme.name);
            r.Expect(GetBkColor(dc) == RGB(240, 240, 240), L"opaque painting restores cached DC background");
            r.Expect(GetPixel(dc, 230, 60) == RGB(255, 255, 255), L"unrelated bitmap-style FillRect remains unchanged");
            for (int pair = 0; pair < 3; ++pair)
            {
                int changed = 0;
                for (int y = 30; y < 47; ++y) for (int x = 5; x < 22; ++x)
                    changed += GetPixel(dc, x + pair * 70, y) != GetPixel(dc, x + pair * 70 + 35, y) ? 1 : 0;
                r.Expect(changed > 2, L"%s checked and unchecked glyphs differ for state pair %d", scheme.name, pair);
            }
            if (ActivePalette()->palette.isDark)
            {
                r.Expect(RelativeLuminance(GetPixel(dc, 12, 37)) < 0.35, L"checkbox uses native dark fill");
                r.Expect(GetWindowTheme(toolbar) == nullptr, L"Access toolbar uses colour-aware classic rendering");
            }
        }
        Switch(nullptr); Pump(40);
        SendMessageW(host, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
        r.Expect(GetPixel(dc, 50, 10) == RGB(240, 240, 240), L"None restores cached row background");
        r.Expect(RelativeLuminance(GetPixel(dc, 12, 37)) > 0.6, L"None restores original checkbox fill");
        r.Expect(GetWindowTheme(toolbar) != nullptr, L"None removes the toolbar override");
        SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); DestroyWindow(host);
        return r.Result();
    }

    HOOKPROC g_creationObserver = nullptr;
    HHOOK WINAPI CaptureCreationObserver(int kind, HOOKPROC procedure, HINSTANCE module, DWORD thread)
    {
        if (kind == WH_CALLWNDPROC) g_creationObserver = procedure;
        return SetWindowsHookExW(kind, procedure, module, thread);
    }
    int UnpairedSystemCreationDoesNotBlockTheme(const CheckContext&)
    {
        Recorder r(L"UnpairedSystemCreationDoesNotBlockTheme");
        ControlSeams::setWindowsHookEx = CaptureCreationObserver;
        const PaletteResources* p = Activate(r, L"One Half Dark");
        ControlSeams::setWindowsHookEx = SetWindowsHookExW;
        if (p == nullptr || !r.Expect(g_creationObserver != nullptr, L"creation observer installed")) return r.Result();
        HWND internal = CreateWindowExW(0, L"Static", L"system notification fixture", 0, 0, 0, 1, 1, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        // USER-created service windows may notify CALLWNDPROC without a corresponding return.
        // The previous implementation permanently accumulated six outstanding messages here.
        for (int i = 0; i < 3; ++i)
            for (UINT message : {WM_NCCREATE, WM_CREATE})
            {
                CWPSTRUCT notification{0, 0, message, internal};
                g_creationObserver(HC_ACTION, 0, reinterpret_cast<LPARAM>(&notification));
            }
        const auto dt = DialogTemplate(L"Unpaired creation regression", WS_POPUP | WS_CAPTION | WS_VISIBLE | DS_MODALFRAME);
        HWND dialog = CreateDialogIndirectParamW(GetModuleHandleW(nullptr), reinterpret_cast<const DLGTEMPLATE*>(dt.data()), nullptr, [](HWND, UINT, WPARAM, LPARAM) -> INT_PTR { return FALSE; }, 0);
        HWND tab = CreateChild(WC_TABCONTROLW, dialog, 0, 5, 5, 200, 100);
        Pump(200);
        r.Expect(Has(RecordedCalls(tab), RecordedCall::Kind::SetWindowTheme, L""), L"new dialog tab receives its theme despite unpaired system notifications");
        r.Expect(Has(RecordedCalls(dialog), RecordedCall::Kind::DwmSetWindowAttribute), L"new dialog title bar themed");
        DestroyWindow(dialog); DestroyWindow(internal);
        return r.Result();
    }

    int DdeDoesNotBlockNewDialogTheme(const CheckContext&)
    {
        Recorder r(L"DdeDoesNotBlockNewDialogTheme");
        StartTheming();
        if (!r.Expect(InstallCallRecorder() && Switch(FindScheme(L"One Half Dark")), L"theming active")) return r.Result();
        DWORD instance = 0;
        r.Expect(DdeInitializeW(&instance, DdeCallback, APPCMD_CLIENTONLY, 0) == DMLERR_NO_ERROR, L"DDE initialized after hooks");
        const auto dialogTemplate = DialogTemplate(L"DdeDoesNotBlockNewDialogTheme", WS_POPUP | WS_CAPTION | WS_VISIBLE | DS_MODALFRAME);
        HWND dialog = CreateDialogIndirectParamW(GetModuleHandleW(nullptr), reinterpret_cast<const DLGTEMPLATE*>(dialogTemplate.data()),
            nullptr, [](HWND, UINT, WPARAM, LPARAM) -> INT_PTR { return FALSE; }, 0);
        if (!r.Expect(dialog != nullptr, L"dialog created")) return r.Result();
        Pump(300);
        r.Expect(Has(RecordedCalls(dialog), RecordedCall::Kind::DwmSetWindowAttribute), L"new dialog title themed without switching again");
        r.Expect(Has(RecordedCalls(GetDlgItem(dialog, 101)), RecordedCall::Kind::SetWindowTheme, L"DarkMode_CFD"), L"new edit themed after DDE initialization");
        DestroyWindow(dialog);
        if (instance != 0) DdeUninitialize(instance);
        Switch(nullptr);
        return r.Result();
    }

    HBRUSH g_labelBrush = nullptr;
    LRESULT CALLBACK LabelBrushProc(HWND window, UINT message, WPARAM w, LPARAM l)
    {
        if (message == WM_CTLCOLORSTATIC)
        {
            SetTextColor(reinterpret_cast<HDC>(w), RGB(0, 0, 0));
            return reinterpret_cast<LRESULT>(g_labelBrush);
        }
        return DefWindowProcW(window, message, w, l);
    }

    int CachedAndTransparentLabelBrushes(const CheckContext&)
    {
        Recorder r(L"CachedAndTransparentLabelBrushes");
        WNDCLASSW wc{};
        wc.hInstance = GetModuleHandleW(nullptr); wc.lpfnWndProc = LabelBrushProc; wc.lpszClassName = L"CachedLabelFixture";
        RegisterClassW(&wc);
        StartTheming();
        Switch(FindScheme(L"One Half Dark"));
        HWND parent = CreateWindowW(wc.lpszClassName, L"Labels", WS_OVERLAPPEDWINDOW, 0, 0, 200, 150, nullptr, nullptr, wc.hInstance, nullptr);
        HWND label = CreateChild(L"Static", parent, SS_LEFTNOWORDWRAP, 5, 5, 100, 20, L"Label");
        Pump(100);
        HDC dc = GetDC(label);
        g_labelBrush = static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
        r.Expect(reinterpret_cast<HBRUSH>(SendMessageW(parent, WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(label))) == g_labelBrush,
            L"transparent label retains transparent brush");
        r.Expect(GetTextColor(dc) == Want(ActivePalette(), COLOR_WINDOWTEXT), L"transparent black label becomes readable");
        g_labelBrush = CreateSolidBrush(RGB(248, 248, 248));
        HBRUSH returned = reinterpret_cast<HBRUSH>(SendMessageW(parent, WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(label)));
        r.Expect(BrushColour(returned) == Want(ActivePalette(), COLOR_3DFACE), L"cached neutral background follows palette");
        r.Expect(BrushColour(g_labelBrush) == RGB(248, 248, 248), L"cached brush unchanged");
        Switch(nullptr);
        r.Expect(reinterpret_cast<HBRUSH>(SendMessageW(parent, WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(label))) == g_labelBrush,
            L"None returns original cached brush");
        r.Expect(GetTextColor(dc) == RGB(0, 0, 0), L"None restores application text");
        ReleaseDC(label, dc); DestroyWindow(parent); DeleteObject(g_labelBrush);
        return r.Result();
    }


    WNDPROC g_legacyLabelProc = nullptr;
    HWND g_legacyLabel = nullptr;
    HWND g_legacyButton = nullptr;
    WNDPROC g_legacyButtonProc = nullptr;
    bool g_nativeButtonFrame = false;
    LRESULT CALLBACK LegacyLabelProc(HWND window, UINT message, WPARAM w, LPARAM l)
    {
        if (message == WM_PRINTCLIENT)
        {
            HDC dc = reinterpret_cast<HDC>(w);
            RECT rect{}; GetClientRect(window, &rect);
            if (window == g_legacyButton && g_nativeButtonFrame) DrawFrameControl(dc, &rect, DFC_BUTTON, DFCS_BUTTONPUSH);
            else FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            SetTextColor(dc, RGB(0, 0, 0)); SetBkMode(dc, TRANSPARENT);
            DrawTextW(dc, L"Legacy label", -1, &rect, DT_SINGLELINE);
            return 0; // Deliberately bypass the comctl subclass chain, as Bentley's handler does.
        }
        return CallWindowProcW(window == g_legacyButton ? g_legacyButtonProc : g_legacyLabelProc, window, message, w, l);
    }
    INT_PTR CALLBACK LateLabelDialog(HWND window, UINT message, WPARAM, LPARAM)
    {
        if (message == WM_INITDIALOG)
        {
            g_legacyLabel = CreateChild(L"Static", window, SS_LEFTNOWORDWRAP, 5, 5, 200, 24, L"Legacy label");
            SendMessageW(g_legacyLabel, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(SYSTEM_FIXED_FONT)), FALSE);
            g_legacyButton = CreateChild(L"Button", window, BS_OWNERDRAW, 5, 75, 200, 24, L"Advanced");
            Pump(40); // The deferred handler must wait until initialization has returned.
            g_legacyLabelProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_legacyLabel, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(LegacyLabelProc)));
            g_legacyButtonProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_legacyButton, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(LegacyLabelProc)));
        }
        return FALSE;
    }
    int LateLegacyLabelAndNativeCheckbox(const CheckContext&)
    {
        Recorder r(L"LateLegacyLabelAndNativeCheckbox");
        StartTheming();
        Switch(FindScheme(L"One Half Dark"));
        const auto dt = DialogTemplate(L"Lazy property page", WS_POPUP | WS_CAPTION | WS_VISIBLE | DS_MODALFRAME);
        HWND dialog = CreateDialogIndirectParamW(GetModuleHandleW(nullptr), reinterpret_cast<const DLGTEMPLATE*>(dt.data()), nullptr, LateLabelDialog, 0);
        if (!r.Expect(dialog != nullptr && g_legacyLabel != nullptr, L"lazy page initialized")) return r.Result();
        HWND check = CreateChild(L"Button", dialog, BS_AUTOCHECKBOX, 5, 40, 200, 24, L"Checked");
        Pump(150);
        HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
        HBITMAP bitmap = CreateCompatibleBitmap(screen, 200, 24);
        ReleaseDC(nullptr, screen);
        HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
        for (const auto& scheme : Schemes())
        {
            Switch(&scheme); Pump(30);
            SendMessageW(g_legacyLabel, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
            const COLORREF background = Want(ActivePalette(), COLOR_3DFACE);
            r.Expect(GetPixel(dc, 180, 12) == background, L"%s legacy label background follows palette", scheme.name);
            int ink = 0;
            for (int y = 0; y < 20; ++y) for (int x = 0; x < 100; ++x) ink += GetPixel(dc, x, y) != background ? 1 : 0;
            r.Expect(ink > 20, L"legacy label text remains visible");
            r.Expect(SendMessageW(g_legacyLabel, WM_GETFONT, 0, 0) == reinterpret_cast<LRESULT>(GetStockObject(SYSTEM_FIXED_FONT)), L"label font unchanged");
            SendMessageW(g_legacyButton, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
            r.Expect(GetPixel(dc, 180, 12) == Want(ActivePalette(), COLOR_WINDOW), L"legacy owner-drawn button fill follows palette");
            if (ActivePalette()->palette.isDark)
            {
                g_nativeButtonFrame = true;
                SendMessageW(g_legacyButton, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
                r.Expect(RelativeLuminance(GetPixel(dc, 180, 12)) < 0.3, L"legacy DrawFrameControl uses a dark native button fill");
                g_nativeButtonFrame = false;
            }
            SendMessageW(check, BM_SETCHECK, BST_UNCHECKED, 0);
            SendMessageW(check, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
            r.Expect(GetPixel(dc, 180, 12) == background, L"%s checkbox backing follows palette", scheme.name);
            COLORREF unchecked[480]{};
            for (int y = 0; y < 24; ++y) for (int x = 0; x < 20; ++x) unchecked[y * 20 + x] = GetPixel(dc, x, y);
            SendMessageW(check, BM_SETCHECK, BST_CHECKED, 0);
            SendMessageW(check, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
            int changed = 0;
            for (int y = 0; y < 24; ++y) for (int x = 0; x < 20; ++x) changed += unchecked[y * 20 + x] != GetPixel(dc, x, y) ? 1 : 0;
            r.Expect(changed > 2, L"native check mark is visible");
            r.Expect(SendMessageW(check, BM_GETCHECK, 0, 0) == BST_CHECKED, L"painting preserves checked state");
        }
        Switch(nullptr);
        SendMessageW(g_legacyLabel, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
        r.Expect(GetPixel(dc, 180, 12) == RGB(255, 255, 255), L"None delegates label drawing to the original legacy handler");
        r.Expect(SendMessageW(check, BM_GETCHECK, 0, 0) == BST_CHECKED, L"None preserves checkbox state");
        SendMessageW(g_legacyButton, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
        r.Expect(GetPixel(dc, 180, 12) == RGB(255, 255, 255), L"None restores original owner-drawn button fill");
        SelectObject(dc, oldBitmap); DeleteObject(bitmap); DeleteDC(dc); DestroyWindow(dialog);
        return r.Result();
    }


    CComPtr<IHTMLDocument2> NewReportDocument()
    {
        CComPtr<IHTMLDocument2> document;
        if (FAILED(document.CoCreateInstance(__uuidof(HTMLDocument)))) return document;
        CComQIPtr<IPersistStreamInit> persistence(document);
        if (!persistence || FAILED(persistence->InitNew())) { document.Release(); return document; }
        SAFEARRAY* content = SafeArrayCreateVector(VT_VARIANT, 0, 1);
        if (content == nullptr) { document.Release(); return document; }
        long index = 0;
        CComVariant markup(L"<html><head><style>td{background:white;color:black}</style></head><body><table><tr><th>Action</th></tr><tr><td>Unchanged report data</td></tr></table></body></html>");
        SafeArrayPutElement(content, &index, &markup);
        document->write(content); document->close(); SafeArrayDestroy(content);
        return document;
    }
    LRESULT CALLBACK ReportFixtureProc(HWND window, UINT message, WPARAM w, LPARAM l)
    {
        static const UINT getObject = RegisterWindowMessageW(L"WM_HTML_GETOBJECT");
        if (message == getObject)
        {
            auto* document = reinterpret_cast<IHTMLDocument2*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            return document != nullptr ? LresultFromObject(__uuidof(IHTMLDocument2), w, document) : 0;
        }
        return DefWindowProcW(window, message, w, l);
    }
    int AuditReportUsesScreenCssAndRestoresNone(const CheckContext&)
    {
        Recorder r(L"AuditReportUsesScreenCssAndRestoresNone");
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        auto document = NewReportDocument(), otherDocument = NewReportDocument();
        if (!r.Expect(document && otherDocument, L"real MSHTML documents created")) return r.Result();
        CComPtr<IHTMLElement> body;
        document->get_body(&body);
        CComBSTR before;
        if (body) body->get_innerHTML(&before);
        r.Expect(body && before, L"report content loaded");
        WNDCLASSW wc{}; wc.hInstance = GetModuleHandleW(nullptr); wc.lpfnWndProc = ReportFixtureProc; wc.lpszClassName = L"Internet Explorer_Server";
        r.Expect(RegisterClassW(&wc) != 0, L"MSHTML window adapter registered");
        StartTheming();
        const auto dt = DialogTemplate(L"@Audit Trail", WS_POPUP | WS_CAPTION | WS_VISIBLE);
        HWND parent = CreateDialogIndirectParamW(wc.hInstance, reinterpret_cast<const DLGTEMPLATE*>(dt.data()), nullptr,
            [](HWND, UINT, WPARAM, LPARAM) -> INT_PTR { return FALSE; }, 0);
        const auto otherDt = DialogTemplate(L"Unrelated web content", WS_POPUP | WS_CAPTION | WS_VISIBLE);
        HWND other = CreateDialogIndirectParamW(wc.hInstance, reinterpret_cast<const DLGTEMPLATE*>(otherDt.data()), nullptr,
            [](HWND, UINT, WPARAM, LPARAM) -> INT_PTR { return FALSE; }, 0);
        HWND report = CreateChild(wc.lpszClassName, parent, 0, 0, 0, 200, 100);
        HWND unrelated = CreateChild(wc.lpszClassName, other, 0, 0, 0, 200, 100);
        SetWindowLongPtrW(report, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(document.p));
        SetWindowLongPtrW(unrelated, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(otherDocument.p));
        CComQIPtr<IHTMLDocument3> doc3(document), other3(otherDocument);
        for (const auto& scheme : Schemes())
        {
            Switch(&scheme); Pump(500);
            CComPtr<IHTMLElement> element;
            doc3->getElementById(CComBSTR(L"ThirdDigitalProjectWiseTheme"), &element);
            if (!r.Expect(element != nullptr, L"%s styles the local report", scheme.name)) continue;
            CComQIPtr<IHTMLStyleElement> style(element);
            CComBSTR media;
            if (style) style->get_media(&media);
            r.Expect(media && wcscmp(media, L"screen") == 0, L"stylesheet excluded from printing");
            CComPtr<IHTMLStyleSheet> sheet;
            if (style) style->get_styleSheet(&sheet);
            CComBSTR css;
            if (sheet) sheet->get_cssText(&css);
            std::wstring normalized(css ? static_cast<const wchar_t*>(css) : L"");
            std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
            wchar_t colour[8]{}; const COLORREF background = Want(ActivePalette(), COLOR_WINDOW);
            swprintf_s(colour, L"#%02x%02x%02x", GetRValue(background), GetGValue(background), GetBValue(background));
            r.Expect(normalized.find(colour) != std::wstring::npos, L"report stylesheet follows palette background");
            CComBSTR after;
            body->get_innerHTML(&after);
            r.Expect(after && wcscmp(before, after) == 0, L"report data and markup unchanged");
            CComPtr<IHTMLElement> outside;
            other3->getElementById(CComBSTR(L"ThirdDigitalProjectWiseTheme"), &outside);
            r.Expect(!outside, L"unrelated embedded HTML is untouched");
        }
        Switch(nullptr); Pump(500);
        CComPtr<IHTMLElement> remaining;
        doc3->getElementById(CComBSTR(L"ThirdDigitalProjectWiseTheme"), &remaining);
        r.Expect(!remaining, L"None removes only the injected stylesheet");
        CComPtr<IHTMLElementCollection> originalStyles;
        doc3->getElementsByTagName(CComBSTR(L"style"), &originalStyles);
        long count = 0; if (originalStyles) originalStyles->get_length(&count);
        r.Expect(count == 1, L"original report stylesheet is retained without duplicates");
        DestroyWindow(parent); DestroyWindow(other);
        remaining.Release(); originalStyles.Release(); body.Release(); doc3.Release(); other3.Release(); document.Release(); otherDocument.Release();
        if (SUCCEEDED(initialized)) CoUninitialize();
        return r.Result();
    }


    Gdiplus::SolidBrush* g_dependencyWhite = nullptr;
    Gdiplus::Pen* g_dependencyPen = nullptr;
    Gdiplus::LinearGradientBrush* g_dependencyGradient = nullptr;
    LRESULT CALLBACK DependencyFixture(HWND window, UINT message, WPARAM w, LPARAM l)
    {
        if (message != WM_PRINTCLIENT) return DefWindowProcW(window, message, w, l);
        Gdiplus::Graphics graphics(reinterpret_cast<HDC>(w));
        graphics.Clear(static_cast<Gdiplus::ARGB>(Gdiplus::Color::White));
        graphics.FillRectangle(g_dependencyWhite, 0, 0, 40, 30);
        Gdiplus::SolidBrush accent(Gdiplus::Color(255, 220, 30, 40));
        graphics.FillRectangle(&accent, 40, 0, 40, 30);
        graphics.DrawLine(g_dependencyPen, 0, 40, 70, 40);
        graphics.FillRectangle(g_dependencyGradient, 0, 50, 80, 30);
        // Cached icon/image pixels must not be recoloured with the vector background.
        Gdiplus::Bitmap icon(10, 10, PixelFormat32bppARGB);
        for (int y=0;y<10;++y) for(int x=0;x<10;++x) icon.SetPixel(x,y,static_cast<Gdiplus::ARGB>(Gdiplus::Color::White));
        graphics.DrawImage(&icon, 90, 0);
        graphics.Flush(Gdiplus::FlushIntentionSync);
        return 0;
    }
    int DependencyDrawingIsScopedAndReversible(const CheckContext&)
    {
        Recorder r(L"DependencyDrawingIsScopedAndReversible");
        Gdiplus::GdiplusStartupInput startup; ULONG_PTR token=0;
        if (!r.Expect(Gdiplus::GdiplusStartup(&token,&startup,nullptr)==Gdiplus::Ok,L"GDI+ started")) return r.Result();
        WNDCLASSW wc{}; wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"DMStudioContainerWndClass";
        r.Expect(RegisterClassW(&wc)!=0,L"dependency host registered");
        wc.lpfnWndProc=DependencyFixture;wc.lpszClassName=L"DependencyDrawingFixture";
        r.Expect(RegisterClassW(&wc)!=0,L"drawing fixture registered");
        StartTheming();
        HWND top=CreateTopLevel(L"DependencyDrawingIsScopedAndReversible");
        HWND host=CreateChild(L"DMStudioContainerWndClass",top,0,0,0,120,100);
        HWND child=CreateChild(wc.lpszClassName,host,0,0,0,120,100);
        HWND unrelated=CreateChild(wc.lpszClassName,top,0,0,0,120,100);
        // Bentley reparents its managed child after constructing it; its identity must survive.
        r.Expect(IsDependencyColourWindow(child),L"dependency ancestry identified");
        SetParent(child,top);
        HDC screen=GetDC(nullptr), dc=CreateCompatibleDC(screen);
        HBITMAP bitmap=CreateCompatibleBitmap(screen,120,100); ReleaseDC(nullptr,screen);
        HGDIOBJ old=SelectObject(dc,bitmap);
        g_dependencyWhite=new Gdiplus::SolidBrush(static_cast<Gdiplus::ARGB>(Gdiplus::Color::White));
        g_dependencyPen=new Gdiplus::Pen(static_cast<Gdiplus::ARGB>(Gdiplus::Color::Black),1.0f);
        g_dependencyGradient=new Gdiplus::LinearGradientBrush(Gdiplus::Point(0,50),Gdiplus::Point(0,80),Gdiplus::Color(255,227,239,255),Gdiplus::Color(255,176,210,255));
        Gdiplus::Color stops[3]={Gdiplus::Color(255,227,239,255),Gdiplus::Color(255,210,225,250),Gdiplus::Color(255,176,210,255)};
        Gdiplus::REAL positions[3]={0.0f,0.5f,1.0f};g_dependencyGradient->SetInterpolationColors(stops,positions,3);
        for(const auto& scheme:Schemes()) {
            r.Expect(Switch(&scheme),L"switch %s",scheme.name);Pump(50);
            ApplyPaintColours(child);
            SendMessageW(child,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
            const auto* p=ActivePalette(); if(!p) {r.Expect(false,L"palette published");break;}
            r.Expect(GetPixel(dc,20,15)==Want(p,COLOR_WINDOW),L"cached white fill follows %s",scheme.name);
            r.Expect(GetPixel(dc,100,90)==Want(p,COLOR_WINDOW),L"canvas clear follows palette");
            r.Expect(GetPixel(dc,50,15)==RGB(220,30,40),L"semantic accent preserved");
            r.Expect(GetPixel(dc,20,40)==Want(p,COLOR_WINDOWTEXT),L"graph outlines readable");
            r.Expect(GetPixel(dc,95,5)==RGB(255,255,255),L"icon pixels unchanged");
            r.Expect(GetPixel(dc,20,65)==Want(p,COLOR_3DFACE),L"multistop Office gradient follows palette");
            Gdiplus::Color color;g_dependencyWhite->GetColor(&color);r.Expect(color.GetValue()==static_cast<Gdiplus::ARGB>(Gdiplus::Color::White),L"cached brush unchanged");
            g_dependencyPen->GetColor(&color);r.Expect(color.GetValue()==static_cast<Gdiplus::ARGB>(Gdiplus::Color::Black),L"cached pen unchanged");
            Gdiplus::Color colors[2];g_dependencyGradient->GetLinearColors(colors);
            r.Expect(colors[0].GetValue()==0xffe3efff && colors[1].GetValue()==0xffb0d2ff,L"cached Office gradient unchanged");
            SendMessageW(unrelated,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
            r.Expect(GetPixel(dc,20,15)==RGB(255,255,255) && GetPixel(dc,20,40)==RGB(0,0,0),L"unrelated GDI+ drawing unchanged");
            HDC meta=CreateEnhMetaFileW(dc,nullptr,nullptr,L"Theme harness\0Dependency\0\0");
            SendMessageW(child,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(meta),PRF_CLIENT);
            HENHMETAFILE record=CloseEnhMetaFile(meta);RECT bounds{0,0,120,100};
            PatBlt(dc,0,0,120,100,BLACKNESS);PlayEnhMetaFile(dc,record,&bounds);
            r.Expect(GetPixel(dc,20,15)==RGB(255,255,255),L"metafile rendering is stock");DeleteEnhMetaFile(record);
            Switch(nullptr);SendMessageW(child,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
            r.Expect(GetPixel(dc,20,15)==RGB(255,255,255) && GetPixel(dc,20,40)==RGB(0,0,0),L"None restores original fill and outlines");
        }
        delete g_dependencyWhite;delete g_dependencyPen;delete g_dependencyGradient;
        g_dependencyWhite=nullptr;g_dependencyPen=nullptr;g_dependencyGradient=nullptr;
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);DestroyWindow(top);
        Gdiplus::GdiplusShutdown(token);return r.Result();
    }

    const CheckRegistration registrations[] = {
        CheckRegistration{{L"Stability", L"AuditReportUsesScreenCssAndRestoresNone", L"-", Host::Self, false, AuditReportUsesScreenCssAndRestoresNone}},
        CheckRegistration{{L"Stability", L"LateLegacyLabelAndNativeCheckbox", L"-", Host::Self, false, LateLegacyLabelAndNativeCheckbox}},
        CheckRegistration{{L"Stability", L"DependencyDrawingIsScopedAndReversible", L"-", Host::Self, false, DependencyDrawingIsScopedAndReversible}},
        CheckRegistration{{L"Stability", L"AccessGridFillsAndCheckStates", L"-", Host::Self, false, AccessGridFillsAndCheckStates}},
        CheckRegistration{{L"Stability", L"UnpairedSystemCreationDoesNotBlockTheme", L"-", Host::Self, false, UnpairedSystemCreationDoesNotBlockTheme}},
        CheckRegistration{{L"Stability", L"DdeDoesNotBlockNewDialogTheme", L"-", Host::Self, false, DdeDoesNotBlockNewDialogTheme}},
        CheckRegistration{{L"Stability", L"CachedAndTransparentLabelBrushes", L"-", Host::Self, false, CachedAndTransparentLabelBrushes}},
        CheckRegistration{{L"Stability", L"CachedGridColoursFollowEveryScheme", L"-", Host::Self, false, CachedGridColoursFollowEveryScheme}},
        CheckRegistration{{L"Stability", L"BufferedListDoesNotEraseOutsidePaint", L"-", Host::Self, false, BufferedListDoesNotEraseOutsidePaint}},
        CheckRegistration{{L"Surfaces", L"ExistingAndNewControlsTheme", L"-", Host::Self, false, ExistingAndNewControlsTheme}},
        CheckRegistration{{L"Surfaces", L"CustomBrushIsPreserved", L"-", Host::Self, false, CustomBrushIsPreserved}},
        CheckRegistration{{L"Surfaces", L"DialogCtlColorUsesPalette", L"-", Host::Self, false, DialogCtlColorUsesPalette}},
        CheckRegistration{{L"Surfaces", L"CtlColorTextUsesPalette", L"-", Host::Self, false, CtlColorTextUsesPalette}},
        CheckRegistration{{L"Surfaces", L"UnknownWindowsRemainUntouched", L"-", Host::Self, false, UnknownWindowsRemainUntouched}},
        CheckRegistration{{L"Surfaces", L"SubclassRemovesOnDestroy", L"-", Host::Self, false, SubclassRemovesOnDestroy}},
        CheckRegistration{{L"Surfaces", L"WrongThreadIsSkipped", L"-", Host::Self, false, WrongThreadIsSkipped}},
        CheckRegistration{{L"Surfaces", L"TitleBarAttributesReset", L"-", Host::Self, false, TitleBarAttributesReset}},
        CheckRegistration{{L"Surfaces", L"UnsupportedDwmIsNonFatal", L"-", Host::Self, false, UnsupportedDwmIsNonFatal}},
        CheckRegistration{{L"Surfaces", L"TreeViewBackgroundFollowsPalette", L"-", Host::Self, false, TreeViewBackgroundFollowsPalette}},
        CheckRegistration{{L"Surfaces", L"TreeViewBackgroundRestoredOnNone", L"-", Host::Self, false, TreeViewBackgroundRestoredOnNone}},
        CheckRegistration{{L"Surfaces", L"ClassBrushEraseUsesPalette", L"-", Host::Self, false, ClassBrushEraseUsesPalette}},
        CheckRegistration{{L"Surfaces", L"ThemeNeverAppliedDuringCreate", L"-", Host::Self, false, ThemeNeverAppliedDuringCreate}},
        CheckRegistration{{L"Surfaces", L"TreeViewTextFollowsPalette", L"-", Host::Self, false, TreeViewTextFollowsPalette}},
        CheckRegistration{{L"Surfaces", L"HeaderTextFollowsPalette", L"-", Host::Self, false, HeaderTextFollowsPalette}},
        CheckRegistration{{L"Surfaces", L"SelectionUsesSubtleFill", L"-", Host::Self, false, SelectionUsesSubtleFill}},
        CheckRegistration{{L"Surfaces", L"ThemeDeferredWhileCreatePumps", L"-", Host::Self, false, ThemeDeferredWhileCreatePumps}},
        CheckRegistration{{L"Surfaces", L"ListViewSelectedAndHotTextReadable", L"-", Host::Self, false, ListViewSelectedAndHotTextReadable}},
        CheckRegistration{{L"Surfaces", L"CustomDrawStockColoursRemapped", L"-", Host::Self, false, CustomDrawStockColoursRemapped}},
        CheckRegistration{{L"Surfaces", L"TooltipFollowsDarkScheme", L"-", Host::Self, false, TooltipFollowsDarkScheme}},
        CheckRegistration{{L"Surfaces", L"ListViewFallsBackWithoutDarkTheme", L"-", Host::Self, false, ListViewFallsBackWithoutDarkTheme}},
        CheckRegistration{{L"Surfaces", L"ListViewTextColourRestoredOnNone", L"-", Host::Self, false, ListViewTextColourRestoredOnNone}},
        CheckRegistration{{L"Surfaces", L"LateHeaderFollowsPalette", L"-", Host::Self, false, LateHeaderFollowsPalette}},
        CheckRegistration{{L"Surfaces", L"NewTopLevelWindowThemedLikeRefresh", L"-", Host::Self, false, NewTopLevelWindowThemedLikeRefresh}},
        CheckRegistration{{L"Surfaces", L"HoverChurnDoesNotRepaintFrame", L"-", Host::Self, false, HoverChurnDoesNotRepaintFrame}},
    };
}
