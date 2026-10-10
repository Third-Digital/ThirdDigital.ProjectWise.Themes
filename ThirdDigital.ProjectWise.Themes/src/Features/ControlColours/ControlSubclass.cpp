#include "ControlSubclass.h"

#include "Domain/ExplorerSurface.h"
#include "Domain/SystemColourMapping.h"
#include "Features/ChildCaption/ChildCaption.h"
#include "Features/ContextMenus/ContextMenus.h"
#include "Features/MenuBar/MenuBar.h"
#include "Features/StatusBar/StatusBar.h"
#include "Features/PaintColours/PaintColours.h"
#include "Features/SystemColours/SystemColourHooks.h"
#include "Features/TitleBar/TitleBar.h"
#include "Shared/Log.h"

#include <commctrl.h>
#include <uxtheme.h>
#include <vssym32.h>

#include <algorithm>
#include <cwchar>
#include <initializer_list>
#include <optional>
#include <vector>

namespace
{
    bool WINAPI DarkListThemeInstalled(LPCWSTR subApp) noexcept
    {
        wchar_t classList[64]{};
        if (swprintf_s(classList, L"%s::ListView", subApp) < 0)
        {
            return false;
        }
        HTHEME theme = OpenThemeData(nullptr, classList);
        if (theme == nullptr)
        {
            return false;
        }
        // An unknown subapp silently opens the plain ListView class, so only a dark list fill confirms it.
        COLORREF fill{};
        const bool installed = SUCCEEDED(GetThemeColor(theme, 0, 0, TMT_FILLCOLOR, &fill)) && RelativeLuminance(fill) < 0.5;
        CloseThemeData(theme);
        return installed;
    }
}

// Whether comctl32 controls are sent WM_SYSCOLORCHANGE to refresh their cached colours.
enum class ColourNotice
{
    Send,
    Skip,
};

static void ApplyThemeTo(HWND window, const PaletteResources* resources, ColourNotice notice) noexcept;
static void ApplyTree(HWND top, const PaletteResources* resources, ColourNotice notice) noexcept;

namespace ControlSeams
{
    SetHook setWindowsHookEx = ::SetWindowsHookExW;
    ThemeAvailable themeAvailable = DarkListThemeInstalled;
}

namespace
{
    constexpr UINT_PTR kSubclassId = 0x54445057;

    DWORD g_uiThread = 0;
    HHOOK g_cbt = nullptr;
    HHOOK g_beforeMessage = nullptr;
    HHOOK g_afterMessage = nullptr;
    thread_local int t_createDepth = 0;

    decltype(&::CreateWindowExW) g_createWindowW = nullptr;
    decltype(&::CreateWindowExA) g_createWindowA = nullptr;
    bool g_creationHooks = false;

    HWND WINAPI CreateWindowWGuard(DWORD ex, LPCWSTR cls, LPCWSTR title, DWORD style, int x, int y, int width, int height,
                                  HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter)
    {
        const bool ours = GetCurrentThreadId() == g_uiThread;
        if (ours) ++t_createDepth;
        __try { return g_createWindowW(ex, cls, title, style, x, y, width, height, parent, menu, instance, parameter); }
        __finally { if (ours) --t_createDepth; }
    }
    HWND WINAPI CreateWindowAGuard(DWORD ex, LPCSTR cls, LPCSTR title, DWORD style, int x, int y, int width, int height,
                                  HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter)
    {
        const bool ours = GetCurrentThreadId() == g_uiThread;
        if (ours) ++t_createDepth;
        __try { return g_createWindowA(ex, cls, title, style, x, y, width, height, parent, menu, instance, parameter); }
        __finally { if (ours) --t_createDepth; }
    }
    bool InstallCreationHooks() noexcept
    {
        if (g_creationHooks) return true;
        void* wide = nullptr;
        void* ansi = nullptr;
        if (MH_CreateHookApiEx(L"user32.dll", "CreateWindowExW", reinterpret_cast<void*>(CreateWindowWGuard),
                              reinterpret_cast<void**>(&g_createWindowW), &wide) != MH_OK) return false;
        if (MH_CreateHookApiEx(L"user32.dll", "CreateWindowExA", reinterpret_cast<void*>(CreateWindowAGuard),
                              reinterpret_cast<void**>(&g_createWindowA), &ansi) != MH_OK)
        {
            MH_RemoveHook(wide);
            return false;
        }
        if (MH_EnableHook(wide) != MH_OK || MH_EnableHook(ansi) != MH_OK)
        {
            MH_DisableHook(wide);
            MH_DisableHook(ansi);
            return false; // Keep possibly live trampolines allocated.
        }
        g_creationHooks = true;
        return true;
    }

    struct Tracked
    {
        HWND window;
        ExplorerSurface surface;
        const wchar_t* theme;
        const PaletteResources* colours;
        std::optional<COLORREF> treeBackground;
        std::optional<COLORREF> itemText;
        std::optional<COLORREF> itemTextApplied;
        // The palette whose frame-level theming this top-level window and its descendants last received.
        const PaletteResources* tree;
    };

    // UI thread only. Window messages sent from inside these functions can re-enter and grow the vector,
    // so entries are looked up again by HWND after any call that may dispatch messages.
    std::vector<Tracked> g_tracked;

    Tracked* Find(HWND window) noexcept
    {
        for (Tracked& t : g_tracked)
        {
            if (t.window == window)
            {
                return &t;
            }
        }
        return nullptr;
    }

    ExplorerSurface Classify(HWND window) noexcept
    {
        wchar_t name[64]{};
        const int length = GetClassNameW(window, name, ARRAYSIZE(name));
        return length > 0 ? ClassifyWindowClass(std::wstring_view(name, static_cast<std::size_t>(length))) : ExplorerSurface::Unknown;
    }

    bool IsListViewHeader(HWND window, HWND parent) noexcept
    {
        wchar_t name[32]{};
        return GetClassNameW(window, name, ARRAYSIZE(name)) > 0 && _wcsicmp(name, WC_HEADERW) == 0 && parent != nullptr &&
               Classify(parent) == ExplorerSurface::ListView;
    }

    bool IsListViewHeader(HWND window) noexcept
    {
        return IsListViewHeader(window, GetParent(window));
    }

    bool ListThemeInstalled(const wchar_t* theme) noexcept
    {
        static std::optional<bool> installed;
        if (!installed)
        {
            installed = ControlSeams::themeAvailable(theme);
            if (!*installed)
            {
                Log(L"control colours: %s unavailable; list-views use DarkMode_Explorer", theme);
            }
        }
        return *installed;
    }

    bool AccessToolbar(HWND window) noexcept
    {
        if (!IsAccessControlWindow(window)) return false;
        wchar_t name[32]{}; GetClassNameW(window, name, ARRAYSIZE(name));
        return wcscmp(name, TOOLBARCLASSNAMEW) == 0 || wcscmp(name, REBARCLASSNAMEW) == 0;
    }

    // List-view column headers under DarkMode_Explorer keep dark text; the items-view theme draws them light.
    const wchar_t* ThemeFor(HWND window, ExplorerSurface surface, bool dark) noexcept
    {
        if (AccessToolbar(window)) return dark ? L"" : nullptr;
        if (surface == ExplorerSurface::Unknown && IsListViewHeader(window))
        {
            return dark ? L"DarkMode_ItemsView" : nullptr;
        }
        // Native tab textures keep property pages white even when their system colours change.
        // Classic tab/dialog rendering uses the palette; None removes this override.
        if (surface == ExplorerSurface::Button)
            return dark ? ((GetWindowLongW(window, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX ? L"" : L"DarkMode_Explorer") : nullptr;
        if (surface == ExplorerSurface::Tab)
        {
            return dark ? L"" : nullptr;
        }
        const wchar_t* theme = DarkThemeClassFor(surface, dark);
        if (surface == ExplorerSurface::ListView && theme != nullptr && !ListThemeInstalled(theme))
        {
            return L"DarkMode_Explorer";
        }
        return theme;
    }

    bool CaptionedTopLevel(LONG style) noexcept
    {
        return (style & WS_CHILD) == 0 && (style & WS_CAPTION) == WS_CAPTION;
    }

    bool IsTopLevel(HWND window) noexcept
    {
        return (GetWindowLongW(window, GWL_STYLE) & WS_CHILD) == 0 && GetAncestor(window, GA_PARENT) == GetDesktopWindow();
    }

    BOOL CALLBACK CollectDescendant(HWND window, LPARAM list)
    {
        try
        {
            reinterpret_cast<std::vector<HWND>*>(list)->push_back(window);
        }
        catch (...)
        {
            return FALSE;
        }
        return TRUE;
    }

    bool UsesComctlColourCache(ExplorerSurface surface) noexcept
    {
        return surface == ExplorerSurface::ListView || surface == ExplorerSurface::TreeView || surface == ExplorerSurface::Tab;
    }

    LRESULT CALLBACK SubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

    void Adopt(HWND window, ExplorerSurface surface)
    {
        if (Find(window) != nullptr)
        {
            return;
        }
        g_tracked.reserve(g_tracked.size() + 1);
        if (SetWindowSubclass(window, SubclassProc, kSubclassId, 0))
        {
            g_tracked.push_back({window, surface, nullptr, nullptr, std::nullopt, std::nullopt, std::nullopt, nullptr});
        }
    }

    void AdoptForColours(HWND window, ExplorerSurface surface, LONG style, HWND parent)
    {
        if (surface != ExplorerSurface::Unknown || CaptionedTopLevel(style) || AccessToolbar(window))
        {
            Adopt(window, surface);
        }
        // WM_CTLCOLOR* goes to the parent, so the parent of a supported control is subclassed too.
        if (surface != ExplorerSurface::Unknown && parent != nullptr && parent != HWND_MESSAGE && parent != GetDesktopWindow() &&
            GetWindowThreadProcessId(parent, nullptr) == g_uiThread)
        {
            Adopt(parent, Classify(parent));
        }
    }

    ExplorerSurface CtlColorSurface(UINT message) noexcept
    {
        switch (message)
        {
        case WM_CTLCOLOREDIT:
            return ExplorerSurface::Edit;
        case WM_CTLCOLORLISTBOX:
            return ExplorerSurface::ListBox;
        case WM_CTLCOLORBTN:
            return ExplorerSurface::Button;
        case WM_CTLCOLORSCROLLBAR:
            return ExplorerSurface::ScrollBar;
        case WM_CTLCOLORMSGBOX:
        case WM_CTLCOLORDLG:
            return ExplorerSurface::Dialog;
        default:
            return ExplorerSurface::Static;
        }
    }

    // Remap stock, cached neutral and dialog texture brushes while preserving coloured application brushes.
    LRESULT SubstituteCtlColor(HWND window, UINT message, WPARAM wParam, LRESULT result) noexcept
    {
        const PaletteResources* p = ActivePalette();
        if (p == nullptr)
        {
            return result;
        }
        const SurfaceColours roles = ColoursFor(CtlColorSurface(message));
        HBRUSH ours = p->ControlBrush(roles.background);
        if (ours == nullptr || GetObjectType(ours) != OBJ_BRUSH)
        {
            return result;
        }
        HBRUSH returned = reinterpret_cast<HBRUSH>(result);
        LOGBRUSH returnedInfo{};
        const bool validBrush = GetObjectW(returned, sizeof(returnedInfo), &returnedInfo) == sizeof(returnedInfo);
        const bool neutralFill = validBrush &&
            returnedInfo.lbStyle == BS_SOLID && GetRValue(returnedInfo.lbColor) == GetGValue(returnedInfo.lbColor) &&
            GetRValue(returnedInfo.lbColor) == GetBValue(returnedInfo.lbColor) && GetRValue(returnedInfo.lbColor) >= 160;
        if (validBrush && returnedInfo.lbStyle == BS_HOLLOW && message == WM_CTLCOLORSTATIC)
        {
            // Transparent labels inherit the themed parent fill but may explicitly request black text.
            SetTextColor(reinterpret_cast<HDC>(wParam), *p->palette.At(roles.text));
            return result;
        }
        const bool tabTexture = validBrush && Classify(window) == ExplorerSurface::Dialog && IsThemeDialogTextureEnabled(window) &&
            (returnedInfo.lbStyle == BS_PATTERN || returnedInfo.lbStyle == BS_DIBPATTERN || returnedInfo.lbStyle == BS_DIBPATTERNPT);
        const bool paletteFill = validBrush && returnedInfo.lbStyle == BS_SOLID &&
            (p->palette.At(roles.background) == returnedInfo.lbColor || p->palette.At(COLOR_WINDOW) == returnedInfo.lbColor ||
             p->palette.At(COLOR_3DFACE) == returnedInfo.lbColor);
        if (!neutralFill && !paletteFill && !tabTexture && returned != ours && returned != p->Brush(roles.background) && returned != OriginalGetSysColorBrush(roles.background))
        {
            return result;
        }
        HDC dc = reinterpret_cast<HDC>(wParam);
        if (const auto text = p->palette.At(roles.text))
        {
            SetTextColor(dc, *text);
        }
        SetBkColor(dc, *p->palette.At(roles.background));
        return reinterpret_cast<LRESULT>(ours);
    }

    // A colour an application took from the stock system colours (cached, or from a source the hooks do not
    // reach) is mapped to that index's palette colour. Preferred indices win where stock values coincide.
    COLORREF RemapStockColour(COLORREF colour, int first, int second, const PaletteResources& p) noexcept
    {
        if ((colour & 0xFF000000) != 0)
        {
            return colour;
        }
        for (int index : {first, second})
        {
            if (const auto mapped = p.palette.At(index); mapped && OriginalGetSysColor(index) == colour)
            {
                return *mapped;
            }
        }
        for (int index = 0; index <= COLOR_MENUBAR; ++index)
        {
            if (const auto mapped = p.palette.At(index); mapped && OriginalGetSysColor(index) == colour)
            {
                return *mapped;
            }
        }
        return colour;
    }

    void RemapCustomDraw(LPARAM lParam) noexcept
    {
        const PaletteResources* p = ActivePalette();
        const auto* header = reinterpret_cast<const NMHDR*>(lParam);
        if (p == nullptr || header == nullptr || header->code != NM_CUSTOMDRAW || header->hwndFrom == nullptr)
        {
            return;
        }
        const ExplorerSurface surface = Classify(header->hwndFrom);
        COLORREF* text = nullptr;
        COLORREF* textBackground = nullptr;
        DWORD stage = 0;
        if (surface == ExplorerSurface::ListView)
        {
            auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
            stage = draw->nmcd.dwDrawStage;
            text = &draw->clrText;
            textBackground = &draw->clrTextBk;
        }
        else if (surface == ExplorerSurface::TreeView)
        {
            auto* draw = reinterpret_cast<NMTVCUSTOMDRAW*>(lParam);
            stage = draw->nmcd.dwDrawStage;
            text = &draw->clrText;
            textBackground = &draw->clrTextBk;
        }
        if (text == nullptr || (stage != CDDS_ITEMPREPAINT && stage != (CDDS_ITEMPREPAINT | CDDS_SUBITEM)))
        {
            return;
        }
        *text = RemapStockColour(*text, COLOR_WINDOWTEXT, COLOR_BTNTEXT, *p);
        *textBackground = RemapStockColour(*textBackground, COLOR_WINDOW, COLOR_3DFACE, *p);
    }

    LRESULT CALLBACK SubclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR)
    {
        switch (message)
        {
        case WM_CTLCOLORMSGBOX:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSCROLLBAR:
        case WM_CTLCOLORSTATIC:
            return SubstituteCtlColor(window, message, wParam, DefSubclassProc(window, message, wParam, lParam));

        // Class brushes given as COLOR_x + 1 are drawn from the kernel's colour table, which the hooks
        // cannot reach. Its return value is a handled flag, not a brush.
        case WM_ERASEBKGND:
            // Common controls own their erase/buffer protocol. Filling their window DC here exposes
            // a blank frame during every hot-item repaint, even with LVS_EX_DOUBLEBUFFER enabled.
            if (Classify(window) == ExplorerSurface::ListView || Classify(window) == ExplorerSurface::TreeView)
            {
                return DefSubclassProc(window, message, wParam, lParam);
            }
            if (const PaletteResources* p = ActivePalette())
            {
                const ULONG_PTR classBrush = GetClassLongPtrW(window, GCLP_HBRBACKGROUND);
                if (classBrush >= 1 && classBrush <= COLOR_MENUBAR + 1)
                {
                    HBRUSH brush = p->Brush(static_cast<int>(classBrush) - 1);
                    RECT client{};
                    if (brush != nullptr && GetObjectType(brush) == OBJ_BRUSH && GetClientRect(window, &client))
                    {
                        FillRect(reinterpret_cast<HDC>(wParam), &client, brush);
                        return 1;
                    }
                }
            }
            return DefSubclassProc(window, message, wParam, lParam);

        // Runs after the parent's own custom-draw handler, on the colours it chose.
        case WM_NOTIFY:
        {
            const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
            RemapCustomDraw(lParam);
            return result;
        }

        case WM_NCDESTROY:
            RemoveWindowSubclass(window, SubclassProc, id);
            g_tracked.erase(std::remove_if(g_tracked.begin(), g_tracked.end(), [window](const Tracked& t) { return t.window == window; }),
                            g_tracked.end());
            ForgetTitleBar(window);
            return DefSubclassProc(window, message, wParam, lParam);

        default:
            return DefSubclassProc(window, message, wParam, lParam);
        }
    }

    struct PendingWindow
    {
        HWND window;
        DWORD classAtom;
    };

    // UI thread only. Windows created while a scheme is active, themed once their creation has returned.
    std::vector<PendingWindow> g_pending;
    // The batch being applied lives here, not on the stack, so a fault's handler can still free it.
    std::vector<PendingWindow> g_batch;
    UINT_PTR g_pendingTimer = 0;
    DeferredFaultHandler g_faultHandler = nullptr;

    void ApplyPendingBatch() noexcept
    {
        g_batch.clear();
        g_batch.swap(g_pending);
        const PaletteResources* p = ActivePalette();
        if (p == nullptr)
        {
            g_batch.clear();
            return;
        }
        for (const PendingWindow& pending : g_batch)
        {
            HWND window = pending.window;
            if (!IsWindow(window) || !OwnedByUiThread(window) || GetClassLongW(window, GCW_ATOM) != pending.classAtom)
            {
                continue;
            }
            ApplyStatusBar(window, p);
            ApplyPaintColours(window);
            ApplyChildCaption(window, p);
            // Judged now rather than at creation: frames are often given their caption, or made top-level,
            // only after they are created.
            const bool frame = IsTopLevel(window) && CaptionedTopLevel(GetWindowLongW(window, GWL_STYLE));
            if (frame && Find(window) == nullptr)
            {
                Adopt(window, Classify(window));
            }
            Tracked* tracked = Find(window);
            if (tracked == nullptr)
            {
                continue;
            }
            if (!frame && Classify(window) != ExplorerSurface::Dialog)
            {
                ApplyThemeTo(window, p, ColourNotice::Skip);
                AllowContextMenuDarkMode(window, p->palette.isDark);
            }
            else if (tracked->tree != p || Classify(window) == ExplorerSurface::Dialog)
            {
                tracked->tree = p;
                ApplyTree(window, p, ColourNotice::Skip);
            }
        }
        g_batch.clear();
    }

    void RecoverFromDeferredFault() noexcept
    {
        g_batch.clear();
        g_pending.clear();
        Log(L"control colours: fault in deferred theming; stock appearance restored");
        if (g_faultHandler != nullptr)
        {
            __try
            {
                g_faultHandler();
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
    }

    void CALLBACK ApplyPending(HWND, UINT, UINT_PTR id, DWORD) noexcept
    {
        KillTimer(nullptr, id);
        g_pendingTimer = 0;
        // A modal loop or other pump inside some window's WM_NCCREATE/WM_CREATE dispatches this timer while
        // that creation is still on the stack; the queue then waits for the outermost creation to return.
        if (t_createDepth > 0)
        {
            g_pendingTimer = SetTimer(nullptr, 0, 10, ApplyPending);
            return;
        }
        __try
        {
            ApplyPendingBatch();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            RecoverFromDeferredFault();
        }
    }

    // SetWindowTheme, DWM and control-colour messages sent while a window is inside its own WM_NCCREATE or
    // WM_CREATE reach application window objects (MFC) that are not yet initialised, so they wait for a
    // thread timer that applies them only when no creation is in progress on this thread.
    void QueueForTheme(HWND window)
    {
        g_pending.push_back({window, GetClassLongW(window, GCW_ATOM)});
        if (g_pendingTimer == 0)
        {
            g_pendingTimer = SetTimer(nullptr, 0, 0, ApplyPending);
        }
    }

    bool IsDialogInitialization(HWND window, UINT message) noexcept
    {
        return message == WM_INITDIALOG || ((message == WM_NCCREATE || message == WM_CREATE) && Classify(window) == ExplorerSurface::Dialog);
    }

    // Count API call lifetimes for creation: internal USER/IME/DDE windows can emit a
    // WH_CALLWNDPROC creation notification without WH_CALLWNDPROCRET. Dialog initialization
    // has real application callbacks and remains guarded separately, including windows
    // created internally by the dialog manager rather than the exported creation APIs.
    LRESULT CALLBACK BeforeMessage(int code, WPARAM wParam, LPARAM lParam)
    {
        const auto* message = reinterpret_cast<const CWPSTRUCT*>(lParam);
        if (code == HC_ACTION && message != nullptr && IsDialogInitialization(message->hwnd, message->message))
        {
            ++t_createDepth;
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    LRESULT CALLBACK AfterMessage(int code, WPARAM wParam, LPARAM lParam)
    {
        const auto* message = reinterpret_cast<const CWPRETSTRUCT*>(lParam);
        if (code == HC_ACTION && message != nullptr && IsDialogInitialization(message->hwnd, message->message) && t_createDepth > 0)
        {
            --t_createDepth;
        }
        if (code == HC_ACTION && message != nullptr && message->message == WM_INITDIALOG && ActivePalette() != nullptr)
        {
            try { QueueForTheme(message->hwnd); } catch (...) {}
        }
        if (code == HC_ACTION && message != nullptr && ActivePalette() != nullptr &&
            message->message == WM_SHOWWINDOW && message->wParam != 0 &&
            CaptionedTopLevel(GetWindowLongW(message->hwnd, GWL_STYLE)))
        {
            try { QueueForTheme(message->hwnd); } catch (...) {}
        }
        // A window given a caption, or made top-level, after creation gets its window-level theming then.
        if (code == HC_ACTION && message != nullptr && message->message == WM_STYLECHANGED && message->wParam == static_cast<WPARAM>(GWL_STYLE) &&
            message->lParam != 0 && ActivePalette() != nullptr)
        {
            const auto* change = reinterpret_cast<const STYLESTRUCT*>(message->lParam);
            const bool becameTopLevel = (change->styleOld & WS_CHILD) != 0 && (change->styleNew & WS_CHILD) == 0;
            const bool gainedCaption = (change->styleOld & WS_CAPTION) != WS_CAPTION && (change->styleNew & WS_CAPTION) == WS_CAPTION;
            if ((becameTopLevel || gainedCaption) && CaptionedTopLevel(static_cast<LONG>(change->styleNew)) && OwnedByUiThread(message->hwnd))
            {
                try
                {
                    QueueForTheme(message->hwnd);
                }
                catch (...)
                {
                }
            }
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    LRESULT CALLBACK CbtProc(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code == HCBT_CREATEWND && ActivePalette() != nullptr)
        {
            try
            {
                HWND window = reinterpret_cast<HWND>(wParam);
                const CREATESTRUCTW* cs = reinterpret_cast<CBT_CREATEWNDW*>(lParam)->lpcs;
                if (cs != nullptr && cs->hwndParent != HWND_MESSAGE)
                {
                    const ExplorerSurface surface = Classify(window);
                    AdoptForColours(window, surface, cs->style, cs->hwndParent);
                    // A list-view creates its header lazily, often long after the list-view itself was themed.
                    if (surface == ExplorerSurface::Unknown && IsListViewHeader(window, cs->hwndParent))
                    {
                        Adopt(window, surface);
                    }
                    if (Find(window) != nullptr || IsStatusBarClass(window) || IsMdiChild(window) || IsPaintColourWindow(window))
                    {
                        QueueForTheme(window);
                    }
                }
            }
            catch (...)
            {
            }
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }
}

bool InstallControlHook(DWORD uiThreadId) noexcept
{
    if (uiThreadId == 0 || uiThreadId != GetCurrentThreadId())
    {
        return false;
    }
    g_uiThread = uiThreadId;
    if (g_cbt != nullptr)
    {
        return true;
    }
    // Creation tracking must be in place before new windows are queued, so all three hooks or none.
    g_beforeMessage = ControlSeams::setWindowsHookEx(WH_CALLWNDPROC, BeforeMessage, nullptr, uiThreadId);
    g_afterMessage = g_beforeMessage != nullptr ? ControlSeams::setWindowsHookEx(WH_CALLWNDPROCRET, AfterMessage, nullptr, uiThreadId) : nullptr;
    g_cbt = g_afterMessage != nullptr ? ControlSeams::setWindowsHookEx(WH_CBT, CbtProc, nullptr, uiThreadId) : nullptr;
    if (g_cbt == nullptr)
    {
        Log(L"control colours: SetWindowsHookEx(WH_CBT) failed (%lu)", GetLastError());
        if (g_afterMessage != nullptr)
        {
            UnhookWindowsHookEx(g_afterMessage);
            g_afterMessage = nullptr;
        }
        if (g_beforeMessage != nullptr)
        {
            UnhookWindowsHookEx(g_beforeMessage);
            g_beforeMessage = nullptr;
        }
        return false;
    }
    if (!InstallCreationHooks())
    {
        Log(L"control colours: creation guards unavailable");
        UnhookWindowsHookEx(g_cbt); g_cbt = nullptr;
        UnhookWindowsHookEx(g_afterMessage); g_afterMessage = nullptr;
        UnhookWindowsHookEx(g_beforeMessage); g_beforeMessage = nullptr;
        return false;
    }
    return true;
}

void SetDeferredFaultHandler(DeferredFaultHandler handler) noexcept
{
    g_faultHandler = handler;
}

bool OwnedByUiThread(HWND window) noexcept
{
    return g_uiThread != 0 && GetCurrentThreadId() == g_uiThread && GetWindowThreadProcessId(window, nullptr) == g_uiThread;
}

void ApplyWindowTree(HWND top, const PaletteResources* resources) noexcept
{
    if (!OwnedByUiThread(top))
    {
        return;
    }
    ApplyTree(top, resources, ColourNotice::Send);
    if (Tracked* tracked = Find(top))
    {
        tracked->tree = resources;
    }
}

void ApplyControlTheme(HWND window, const PaletteResources* resources) noexcept
{
    ApplyThemeTo(window, resources, ColourNotice::Send);
}

static void ApplyTree(HWND top, const PaletteResources* resources, ColourNotice notice) noexcept
{
    ApplyThemeTo(top, resources, notice);
    ApplyPaintColours(top);
    ApplyTitleBar(top, resources);
    AllowContextMenuDarkMode(top, resources != nullptr && resources->palette.isDark);
    ApplyMenuBar(top, resources);
    try
    {
        std::vector<HWND> descendants;
        EnumChildWindows(top, CollectDescendant, reinterpret_cast<LPARAM>(&descendants));
        for (HWND child : descendants)
        {
            if (IsWindow(child))
            {
                ApplyThemeTo(child, resources, notice);
                ApplyStatusBar(child, resources);
                ApplyPaintColours(child);
                ApplyChildCaption(child, resources);
            }
        }
    }
    catch (...)
    {
    }
}

static void ApplyThemeTo(HWND window, const PaletteResources* resources, ColourNotice notice) noexcept
{
    if (!OwnedByUiThread(window))
    {
        return;
    }
    try
    {
        const ExplorerSurface surface = Classify(window);
        HWND header = surface == ExplorerSurface::ListView ? ListView_GetHeader(window) : nullptr;
        if (resources != nullptr)
        {
            AdoptForColours(window, surface, GetWindowLongW(window, GWL_STYLE), GetAncestor(window, GA_PARENT));
            if (header != nullptr && OwnedByUiThread(header))
            {
                Adopt(header, ExplorerSurface::Unknown);
            }
        }
        Tracked* tracked = Find(window);
        if (tracked == nullptr)
        {
            return;
        }

        // Snapshot before SetWindowTheme: comctl32 refreshes its cached/default colours during
        // that call. Saving afterwards records our foreground as the supposedly original colour.
        if (resources != nullptr && (surface == ExplorerSurface::TreeView || surface == ExplorerSurface::ListView) && !tracked->itemText)
        {
            const UINT getText = surface == ExplorerSurface::TreeView ? TVM_GETTEXTCOLOR : LVM_GETTEXTCOLOR;
            const COLORREF original = static_cast<COLORREF>(SendMessageW(window, getText, 0, 0));
            tracked = Find(window);
            if (tracked != nullptr)
            {
                tracked->itemText = original == resources->palette.At(COLOR_WINDOWTEXT) ? CLR_DEFAULT : original;
            }
        }
        tracked = Find(window);
        if (tracked == nullptr)
        {
            return;
        }
        const wchar_t* theme = resources != nullptr ? ThemeFor(window, surface, resources->palette.isDark) : nullptr;
        if (theme != tracked->theme)
        {
            tracked->theme = theme;
            SetWindowTheme(window, theme, theme != nullptr && *theme == L'\0' ? L"" : nullptr);
        }

        tracked = Find(window);
        // Visual-styled tree-views store the theme fill as an explicit background, so it never follows
        // COLOR_WINDOW; -1 selects the system colour until None restores the stored value.
        if (tracked != nullptr && surface == ExplorerSurface::TreeView)
        {
            if (resources != nullptr && !tracked->treeBackground)
            {
                tracked->treeBackground = static_cast<COLORREF>(SendMessageW(window, TVM_GETBKCOLOR, 0, 0));
                SendMessageW(window, TVM_SETBKCOLOR, 0, static_cast<LPARAM>(-1));
            }
            else if (resources == nullptr && tracked->treeBackground)
            {
                const COLORREF original = *tracked->treeBackground;
                tracked->treeBackground.reset();
                SendMessageW(window, TVM_SETBKCOLOR, 0, static_cast<LPARAM>(original));
            }
        }

        tracked = Find(window);
        // The dark tree and list themes leave the default item text black, so the text colour is set explicitly.
        if (tracked != nullptr && (surface == ExplorerSurface::TreeView || surface == ExplorerSurface::ListView))
        {
            const bool tree = surface == ExplorerSurface::TreeView;
            const UINT getText = tree ? TVM_GETTEXTCOLOR : LVM_GETTEXTCOLOR;
            const UINT setText = tree ? TVM_SETTEXTCOLOR : LVM_SETTEXTCOLOR;
            if (resources != nullptr)
            {
                const COLORREF text = *resources->palette.At(COLOR_WINDOWTEXT);
                if (!tracked->itemText)
                {
                    tracked->itemText = static_cast<COLORREF>(SendMessageW(window, getText, 0, 0));
                }
                if (tracked->itemTextApplied != text)
                {
                    tracked->itemTextApplied = text;
                    SendMessageW(window, setText, 0, static_cast<LPARAM>(text));
                }
            }
            else if (tracked->itemText)
            {
                const COLORREF original = *tracked->itemText;
                tracked->itemText.reset();
                tracked->itemTextApplied.reset();
                SendMessageW(window, setText, 0, static_cast<LPARAM>(original));
            }
        }

        tracked = Find(window);
        // comctl32 caches system colours per control and process-wide until it sees WM_SYSCOLORCHANGE.
        // A control created after the switch already sees the refreshed cache, so it is not told again.
        if (tracked != nullptr && UsesComctlColourCache(surface) && tracked->colours != resources)
        {
            tracked->colours = resources;
            if (notice == ColourNotice::Send)
            {
                SendMessageW(window, WM_SYSCOLORCHANGE, 0, 0);
            }
        }

        if (header != nullptr && IsWindow(header))
        {
            ApplyThemeTo(header, resources, notice);
        }
    }
    catch (...)
    {
    }
}
