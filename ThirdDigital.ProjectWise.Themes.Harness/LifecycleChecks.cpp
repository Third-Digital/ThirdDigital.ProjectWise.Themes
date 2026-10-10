#include "Fixtures.h"

#include "Features/ControlColours/ControlSubclass.h"
#include "Features/Persistence/ThemeSettings.h"
#include "Features/ThemeSwitch.h"
#include "Shared/ActivePalette.h"

#include <dwmapi.h>
#include <uxtheme.h>

#include <atomic>
#include <thread>

using namespace Harness;

namespace
{
    const ColourScheme* SchemeArgument(Recorder& r, const CheckContext& context)
    {
        const ColourScheme* scheme = FindScheme(context.scheme);
        if (scheme == nullptr)
        {
            r.Invalidate(L"unknown scheme '%s'", context.scheme.c_str());
        }
        return scheme;
    }

    bool Save(Recorder& r, const ColourScheme* scheme)
    {
        if (!SaveSelection(scheme))
        {
            r.Invalidate(L"cannot write the test settings key");
            return false;
        }
        return true;
    }

    bool SchemeValueExists()
    {
        return RegGetValueW(HKEY_CURRENT_USER, SettingsKey().c_str(), L"Scheme", RRF_RT_ANY, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    }

    std::size_t Count(HWND window, RecordedCall::Kind kind, const wchar_t* text = nullptr)
    {
        std::size_t n = 0;
        for (const RecordedCall& call : RecordedCalls(window))
        {
            if (call.kind == kind && (text == nullptr || call.text == text))
            {
                ++n;
            }
        }
        return n;
    }

    std::optional<DWORD> LastDwm(HWND window, DWORD attribute)
    {
        std::optional<DWORD> value;
        for (const RecordedCall& call : RecordedCalls(window))
        {
            if (call.kind == RecordedCall::Kind::DwmSetWindowAttribute && call.attribute == attribute)
            {
                value = call.value;
            }
        }
        return value;
    }

    bool AnyWindowChange()
    {
        for (const RecordedCall& call : RecordedCalls(nullptr))
        {
            if (call.kind == RecordedCall::Kind::SetWindowTheme || call.kind == RecordedCall::Kind::SetWindowSubclass ||
                call.kind == RecordedCall::Kind::DwmSetWindowAttribute)
            {
                return true;
            }
        }
        return false;
    }

    COLORREF StockText()
    {
        return GetSysColor(COLOR_WINDOWTEXT);
    }

    void ExpectModalDialog(Recorder& r, COLORREF want, const wchar_t* label)
    {
        if (const auto background = ModalDialogBackground(r))
        {
            r.Expect(*background == want, L"%s modal dialog background %06lX want %06lX", label, *background, want);
        }
    }

    void ExpectDarkTitleBar(Recorder& r, HWND top, const SystemColourPalette& palette, const wchar_t* label)
    {
        if (palette.isDark)
        {
            r.Expect(LastDwm(top, DWMWA_USE_IMMERSIVE_DARK_MODE) == 1u, L"%s dark title bar applied", label);
            r.Expect(LastDwm(top, DWMWA_CAPTION_COLOR) == *palette.At(COLOR_ACTIVECAPTION), L"%s caption colour applied", label);
        }
        else
        {
            r.Expect(Count(top, RecordedCall::Kind::DwmSetWindowAttribute) == 0, L"%s light scheme leaves DWM alone", label);
        }
    }

    int PreInit(const CheckContext& context)
    {
        Recorder r(L"PreInit");
        const ColourScheme* scheme = SchemeArgument(r, context);
        if (scheme == nullptr)
        {
            return r.Result();
        }
        const ApiSnapshot baseline = TakeSnapshot(WindowTheme());
        ProductionModule module;
        if (!Save(r, scheme) || !LoadProductionModule(r, context, module))
        {
            return r.Result();
        }
        InstallCallRecorder(module.handle);
        r.Expect(module.customInitialize(kExplorerMask, nullptr) == IDOK, L"CustomInitialize returns IDOK");
        Pump(300);
        const SystemColourPalette palette = BuildPalette(*scheme);
        ExpectSnapshot(r, baseline, TakeSnapshot(WindowTheme()), &palette, L"after initialisation");

        Fixture f = CreateFixture(L"PreInit");
        Pump(500);
        ExpectThemedPixels(r, SamplePixels(r, f, *palette.At(COLOR_WINDOWTEXT)), palette, scheme->name);
        ExpectModalDialog(r, *palette.At(COLOR_3DFACE), scheme->name);
        ExpectDarkTitleBar(r, f.top, palette, scheme->name);
        DestroyFixture(f);
        return r.Result();
    }

    int PostInit(const CheckContext& context)
    {
        Recorder r(L"PostInit");
        const ColourScheme* scheme = SchemeArgument(r, context);
        if (scheme == nullptr)
        {
            return r.Result();
        }
        Fixture f = CreateFixture(L"PostInit");
        Pump(500);
        const Pixels stock = SamplePixels(r, f, StockText());
        const ApiSnapshot baseline = TakeSnapshot(WindowTheme());
        const COLORREF stockWindow = GetSysColor(COLOR_WINDOW);
        const int hiddenBefore = HiddenSysColourChanges();
        ProductionModule module;
        if (!Save(r, scheme) || !LoadProductionModule(r, context, module))
        {
            return r.Result();
        }
        InstallCallRecorder(module.handle);
        r.Expect(module.customInitialize(kExplorerMask, nullptr) == IDOK, L"CustomInitialize returns IDOK");
        Pump(600);
        const SystemColourPalette palette = BuildPalette(*scheme);
        ExpectSnapshot(r, baseline, TakeSnapshot(WindowTheme()), &palette, L"after initialisation");
        r.Expect(stock.listBackground == stockWindow, L"fixture sampled in stock colours before initialisation");
        ExpectThemedPixels(r, SamplePixels(r, f, *palette.At(COLOR_WINDOWTEXT)), palette, scheme->name);
        ExpectModalDialog(r, *palette.At(COLOR_3DFACE), scheme->name);
        ExpectDarkTitleBar(r, f.top, palette, scheme->name);
        r.Expect(HiddenSysColourChanges() > hiddenBefore, L"hidden top-level window received WM_SYSCOLORCHANGE");
        DestroyFixture(f);
        return r.Result();
    }

    int Stock(const CheckContext& context)
    {
        Recorder r(L"Stock");
        Fixture f = CreateFixture(L"Stock");
        Pump(500);
        const Pixels before = SamplePixels(r, f, StockText());
        const ApiSnapshot baseline = TakeSnapshot(WindowTheme());
        const COLORREF stockFace = GetSysColor(COLOR_3DFACE);
        ProductionModule module;
        if (!Save(r, nullptr) || !LoadProductionModule(r, context, module))
        {
            return r.Result();
        }
        InstallCallRecorder(module.handle);
        r.Expect(module.customInitialize(kExplorerMask, nullptr) == IDOK, L"CustomInitialize returns IDOK");
        Pump(600);
        ExpectSnapshot(r, baseline, TakeSnapshot(WindowTheme()), nullptr, L"no selection");
        ExpectSamePixels(r, before, SamplePixels(r, f, StockText()), L"no selection");
        Fixture later = CreateFixture(L"Stock later");
        Pump(400);
        ExpectSamePixels(r, before, SamplePixels(r, later, StockText()), L"windows created after initialisation");
        ExpectModalDialog(r, stockFace, L"no selection");
        r.Expect(!AnyWindowChange(), L"no subclass, window theme or DWM change without a selection");
        r.Expect(LogLines(L"colour hooks installed") == 0, L"no hooks installed without a selection");
        DestroyFixture(later);
        DestroyFixture(f);
        return r.Result();
    }

    int InitReturnsIdok(const CheckContext& context)
    {
        Recorder r(L"InitReturnsIdok");
        ProductionModule module;
        if (!Save(r, FindScheme(L"Campbell")) || !LoadProductionModule(r, context, module))
        {
            return r.Result();
        }
        r.Expect(module.customInitialize(kExplorerMask, nullptr) == IDOK, L"first call returns IDOK");
        r.Expect(module.customInitialize(kAdministratorMask, nullptr) == IDOK, L"later call with another mask returns IDOK");
        r.Expect(module.customInitialize(kExplorerMask, reinterpret_cast<LPVOID>(1)) == IDOK, L"later call returns IDOK");
        Pump(200);
        return r.Result();
    }

    int InitRunsOnce(const CheckContext& context)
    {
        Recorder r(L"InitRunsOnce");
        ProductionModule module;
        if (!Save(r, FindScheme(L"Campbell")) || !LoadProductionModule(r, context, module))
        {
            return r.Result();
        }
        module.customInitialize(kExplorerMask, nullptr);
        module.customInitialize(kExplorerMask, nullptr);
        Pump(300);
        r.Expect(LogLines(L"CustomInitialize: thread") == 1, L"initialisation logged once (%d)", LogLines(L"CustomInitialize: thread"));
        r.Expect(LogLines(L"colour hooks installed") == 1, L"hooks installed once (%d)", LogLines(L"colour hooks installed"));
        r.Expect(GetSysColor(COLOR_WINDOW) == FindScheme(L"Campbell")->background, L"theme active after the first call");
        return r.Result();
    }

    int OtherMaskIsIgnored(const CheckContext& context)
    {
        Recorder r(L"OtherMaskIsIgnored");
        const ApiSnapshot baseline = TakeSnapshot(WindowTheme());
        ProductionModule module;
        if (!Save(r, FindScheme(L"Campbell")) || !LoadProductionModule(r, context, module))
        {
            return r.Result();
        }
        InstallCallRecorder(module.handle);
        r.Expect(module.customInitialize(kAdministratorMask, nullptr) == IDOK, L"returns IDOK");
        Pump(300);
        Fixture f = CreateFixture(L"OtherMaskIsIgnored");
        Pump(300);
        ExpectSnapshot(r, baseline, TakeSnapshot(WindowTheme()), nullptr, L"other mask");
        r.Expect(!AnyWindowChange(), L"no window changes for another application mask");
        r.Expect(LogLines(L"not ProjectWise Explorer") == 1, L"ignored mask logged");
        DestroyFixture(f);
        return r.Result();
    }

    int PickerSkippedWithoutDmawin(const CheckContext& context)
    {
        Recorder r(L"PickerSkippedWithoutDmawin");
        ProductionModule module;
        if (!Save(r, FindScheme(L"Vintage")) || !LoadProductionModule(r, context, module))
        {
            return r.Result();
        }
        r.Expect(GetModuleHandleW(L"dmawin.dll") == nullptr, L"dmawin.dll absent before initialisation");
        r.Expect(module.customInitialize(kExplorerMask, nullptr) == IDOK, L"returns IDOK");
        Pump(300);
        r.Expect(GetModuleHandleW(L"dmawin.dll") == nullptr, L"dmawin.dll not loaded by initialisation");
        r.Expect(LogLines(L"picker skipped") == 1, L"skipped picker logged");
        r.Expect(GetSysColor(COLOR_WINDOW) == FindScheme(L"Vintage")->background, L"theming continues without the picker");
        return r.Result();
    }

    using GetModuleHandleExFn = BOOL(WINAPI*)(DWORD, LPCWSTR, HMODULE*);
    GetModuleHandleExFn g_realGetModuleHandleEx = nullptr;

    BOOL WINAPI FaultingGetModuleHandleEx(DWORD flags, LPCWSTR name, HMODULE* module)
    {
        if ((flags & GET_MODULE_HANDLE_EX_FLAG_PIN) != 0)
        {
            RaiseException(0xE0545450, 0, 0, nullptr);
        }
        return g_realGetModuleHandleEx(flags, name, module);
    }

    int FaultInFirstInitDoesNotHangSecond(const CheckContext& context)
    {
        Recorder r(L"FaultInFirstInitDoesNotHangSecond");
        const ApiSnapshot baseline = TakeSnapshot(WindowTheme());
        ProductionModule module;
        if (!Save(r, FindScheme(L"Campbell")) || !LoadProductionModule(r, context, module))
        {
            return r.Result();
        }
        g_realGetModuleHandleEx = &GetModuleHandleExW;
        if (!r.Expect(PatchImports(module.handle, reinterpret_cast<void*>(g_realGetModuleHandleEx), reinterpret_cast<void*>(FaultingGetModuleHandleEx)),
                      L"module imports GetModuleHandleExW to pin itself, where the fault is injected"))
        {
            return r.Result();
        }
        r.Expect(module.customInitialize(kExplorerMask, nullptr) == IDOK, L"faulting first call returns IDOK");
        const ULONGLONG start = GetTickCount64();
        const LONG second = module.customInitialize(kExplorerMask, nullptr);
        const ULONGLONG elapsed = GetTickCount64() - start;
        r.Expect(second == IDOK && elapsed < 1000, L"second call returns IDOK in %llu ms", elapsed);
        Pump(300);
        ExpectSnapshot(r, baseline, TakeSnapshot(WindowTheme()), nullptr, L"after faulted initialisation");
        r.Expect(LogLines(L"initialisation fault") == 1, L"fault logged");
        return r.Result();
    }

    int NonExplorerHostIsUntouched(const CheckContext& context)
    {
        Recorder r(L"NonExplorerHostIsUntouched");
        Fixture f = CreateFixture(L"NonExplorerHostIsUntouched");
        Pump(500);
        const Pixels before = SamplePixels(r, f, StockText());
        const ApiSnapshot baseline = TakeSnapshot(WindowTheme());
        ProductionModule module;
        if (!Save(r, FindScheme(L"Campbell")) || !LoadProductionModule(r, context, module))
        {
            return r.Result();
        }
        InstallCallRecorder(module.handle);
        r.Expect(module.customInitialize(kExplorerMask, nullptr) == IDOK, L"returns IDOK");
        Pump(500);
        ExpectSnapshot(r, baseline, TakeSnapshot(WindowTheme()), nullptr, L"non-Explorer host");
        ExpectSamePixels(r, before, SamplePixels(r, f, StockText()), L"non-Explorer host");
        r.Expect(!AnyWindowChange(), L"no window changes in a non-Explorer host");
        r.Expect(LogLines(L"not pwc.exe") == 1, L"host rejection logged");
        DestroyFixture(f);
        return r.Result();
    }

    void ExpectRestored(Recorder& r, const Fixture& f, bool wasDark, const wchar_t* label)
    {
        if (wasDark)
        {
            for (HWND window : {f.list, f.tree, f.edit})
            {
                r.Expect(Count(window, RecordedCall::Kind::SetWindowTheme, L"(null)") == 1, L"%s window theme reset once", label);
            }
            r.Expect(LastDwm(f.top, DWMWA_USE_IMMERSIVE_DARK_MODE) == 0u && LastDwm(f.top, DWMWA_CAPTION_COLOR) == DWMWA_COLOR_DEFAULT &&
                         LastDwm(f.top, DWMWA_BORDER_COLOR) == DWMWA_COLOR_DEFAULT && LastDwm(f.top, DWMWA_TEXT_COLOR) == DWMWA_COLOR_DEFAULT,
                     L"%s title bar reset to defaults", label);
        }
        else
        {
            r.Expect(Count(nullptr, RecordedCall::Kind::SetWindowTheme) == 0 && Count(nullptr, RecordedCall::Kind::DwmSetWindowAttribute) == 0,
                     L"%s light scheme had nothing to reset", label);
        }
    }

    int NoneRestoresApisAndPixels(const CheckContext& context)
    {
        Recorder r(L"NoneRestoresApisAndPixels");
        const ColourScheme* scheme = SchemeArgument(r, context);
        if (scheme == nullptr)
        {
            return r.Result();
        }
        const ApiSnapshot baseline = TakeSnapshot(WindowTheme());
        HBRUSH stockBrushes[COLOR_MENUBAR + 1]{};
        for (int i = 0; i <= COLOR_MENUBAR; ++i)
        {
            stockBrushes[i] = GetSysColorBrush(i);
        }
        Fixture f = CreateFixture(L"NoneRestoresApisAndPixels");
        Pump(500);
        const Pixels stock = SamplePixels(r, f, StockText());
        const COLORREF stockFace = GetSysColor(COLOR_3DFACE);

        r.Expect(!StartTheming(), L"no saved selection: nothing activated");
        if (!r.Expect(Switch(scheme), L"switch to %s", scheme->name))
        {
            return r.Result();
        }
        Pump(500);
        const SystemColourPalette palette = BuildPalette(*scheme);
        ExpectThemedPixels(r, SamplePixels(r, f, *palette.At(COLOR_WINDOWTEXT)), palette, scheme->name);

        InstallCallRecorder();
        ClearRecordedCalls();
        r.Expect(Switch(nullptr), L"switch to None");
        Pump(600);
        ExpectSnapshot(r, baseline, TakeSnapshot(WindowTheme()), nullptr, L"after None");
        int brushMismatches = 0;
        for (int i = 0; i <= COLOR_MENUBAR; ++i)
        {
            brushMismatches += GetSysColorBrush(i) == stockBrushes[i] ? 0 : 1;
        }
        r.Expect(brushMismatches == 0, L"original system brushes returned after None (%d differ)", brushMismatches);
        ExpectSamePixels(r, stock, SamplePixels(r, f, StockText()), L"after None");
        ExpectModalDialog(r, stockFace, L"after None");
        ExpectRestored(r, f, palette.isDark, L"after None");
        r.Expect(ReadSelection() == nullptr && !SchemeValueExists(), L"Scheme value deleted");
        r.Expect(ActivePalette() == nullptr, L"palette unpublished");
        DestroyFixture(f);
        return r.Result();
    }

    int DarkToLightRefreshes(const CheckContext&)
    {
        Recorder r(L"DarkToLightRefreshes");
        const ApiSnapshot baseline = TakeSnapshot(WindowTheme());
        Fixture f = CreateFixture(L"DarkToLightRefreshes");
        Pump(400);
        StartTheming();
        if (!r.Expect(Switch(FindScheme(L"Campbell")), L"switch to Campbell"))
        {
            return r.Result();
        }
        Pump(400);
        InstallCallRecorder();
        ClearRecordedCalls();
        const ColourScheme* light = FindScheme(L"Tango Light");
        r.Expect(Switch(light), L"switch to Tango Light");
        Pump(600);
        const SystemColourPalette palette = BuildPalette(*light);
        ExpectSnapshot(r, baseline, TakeSnapshot(WindowTheme()), &palette, L"Tango Light");
        ExpectThemedPixels(r, SamplePixels(r, f, *palette.At(COLOR_WINDOWTEXT)), palette, L"Tango Light");
        ExpectRestored(r, f, true, L"dark to light");
        bool themeChanged = false;
        for (const RecordedCall& call : RecordedCalls(f.top))
        {
            themeChanged |= call.kind == RecordedCall::Kind::SendMessageTimeout && call.attribute == WM_THEMECHANGED;
        }
        r.Expect(themeChanged, L"WM_THEMECHANGED sent when the theme class changes");
        r.Expect(ReadSelection() == light, L"preference is Tango Light");
        DestroyFixture(f);
        return r.Result();
    }

    int NoneToSchemeTransition(const CheckContext&)
    {
        Recorder r(L"NoneToSchemeTransition");
        const ApiSnapshot baseline = TakeSnapshot(WindowTheme());
        Fixture f = CreateFixture(L"NoneToSchemeTransition");
        Pump(400);
        StartTheming();
        r.Expect(Switch(FindScheme(L"Vintage")), L"switch to Vintage");
        Pump(300);
        r.Expect(Switch(nullptr), L"switch to None");
        Pump(300);
        ExpectSnapshot(r, baseline, TakeSnapshot(WindowTheme()), nullptr, L"None");
        const ColourScheme* again = FindScheme(L"One Half Dark");
        r.Expect(Switch(again), L"switch to One Half Dark without restarting");
        Pump(600);
        const SystemColourPalette palette = BuildPalette(*again);
        ExpectSnapshot(r, baseline, TakeSnapshot(WindowTheme()), &palette, L"One Half Dark");
        ExpectThemedPixels(r, SamplePixels(r, f, *palette.At(COLOR_WINDOWTEXT)), palette, L"One Half Dark");
        r.Expect(ReadSelection() == again, L"preference is One Half Dark");
        DestroyFixture(f);
        return r.Result();
    }

    int RefreshIsProcessBounded(const CheckContext&)
    {
        Recorder r(L"RefreshIsProcessBounded");
        Fixture f = CreateFixture(L"RefreshIsProcessBounded");
        Pump(300);
        StartTheming();
        r.Expect(Switch(FindScheme(L"Campbell")), L"switch to Campbell");
        Pump(300);
        InstallCallRecorder();
        ClearRecordedCalls();
        const int hiddenBefore = HiddenSysColourChanges();
        r.Expect(Switch(FindScheme(L"Vintage")), L"switch to Vintage");
        r.Expect(Switch(nullptr), L"switch to None");
        Pump(300);
        std::size_t refreshCalls = 0;
        std::size_t foreign = 0;
        for (const RecordedCall& call : RecordedCalls(nullptr))
        {
            if (call.kind != RecordedCall::Kind::SendMessageTimeout && call.kind != RecordedCall::Kind::SendNotifyMessage &&
                call.kind != RecordedCall::Kind::RedrawWindow)
            {
                continue;
            }
            ++refreshCalls;
            DWORD pid = 0;
            GetWindowThreadProcessId(call.window, &pid);
            if (call.window == HWND_BROADCAST || call.window == nullptr || pid != GetCurrentProcessId())
            {
                ++foreign;
            }
        }
        r.Expect(refreshCalls > 0, L"refresh messages were sent (%u)", static_cast<unsigned>(refreshCalls));
        r.Expect(foreign == 0, L"every refresh targets a window of this process (%u outside)", static_cast<unsigned>(foreign));
        r.Expect(HiddenSysColourChanges() >= hiddenBefore + 2, L"hidden top-level window refreshed on each switch");
        DestroyFixture(f);
        return r.Result();
    }

    int StalledSecondaryThreadDoesNotBlock(const CheckContext&)
    {
        Recorder r(L"StalledSecondaryThreadDoesNotBlock");
        Fixture f = CreateFixture(L"StalledSecondaryThreadDoesNotBlock");
        Pump(300);
        std::atomic<HWND> stalled{nullptr};
        std::thread worker([&] {
            HWND window = CreateWindowExW(0, L"TDPWHarnessWindow", L"stalled", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 400, 400, 200, 120, nullptr, nullptr,
                                          GetModuleHandleW(nullptr), nullptr);
            stalled = window;
            Sleep(5000);
            Pump(200);
            DestroyWindow(window);
        });
        while (stalled.load() == nullptr)
        {
            Sleep(10);
        }
        StartTheming();
        ULONGLONG start = GetTickCount64();
        const bool themed = Switch(FindScheme(L"Campbell"));
        const ULONGLONG toScheme = GetTickCount64() - start;
        start = GetTickCount64();
        const bool none = Switch(nullptr);
        const ULONGLONG toNone = GetTickCount64() - start;
        r.Expect(themed && toScheme < 2000, L"switch to a scheme completed in %llu ms with a stalled UI thread", toScheme);
        r.Expect(none && toNone < 2000, L"switch to None completed in %llu ms with a stalled UI thread", toNone);
        worker.join();
        DestroyFixture(f);
        return r.Result();
    }

    bool g_nestedRequested = false;
    bool g_nestedResult = false;
    bool g_paletteDuringNested = false;

    LRESULT CALLBACK NestedProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_SYSCOLORCHANGE && !g_nestedRequested && ActivePalette() != nullptr)
        {
            g_nestedRequested = true;
            g_nestedResult = Switch(nullptr);
            g_paletteDuringNested = ActivePalette() != nullptr;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    int NestedNoneDuringRefreshIsDeferred(const CheckContext&)
    {
        Recorder r(L"NestedNoneDuringRefreshIsDeferred");
        WNDCLASSW wc{};
        wc.lpfnWndProc = NestedProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_3DFACE + 1);
        wc.lpszClassName = L"TDPWHarnessNested";
        RegisterClassW(&wc);
        const ApiSnapshot baseline = TakeSnapshot(WindowTheme());
        Fixture f = CreateFixture(L"NestedNoneDuringRefreshIsDeferred");
        HWND nested = CreateWindowExW(0, L"TDPWHarnessNested", L"nested", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 500, 500, 200, 120, nullptr, nullptr,
                                      GetModuleHandleW(nullptr), nullptr);
        Pump(400);
        const Pixels stock = SamplePixels(r, f, StockText());
        StartTheming();
        Switch(FindScheme(L"Campbell"));
        Pump(600);
        r.Expect(g_nestedRequested, L"a window requested None during the refresh");
        r.Expect(g_nestedResult && g_paletteDuringNested, L"the nested request was deferred, not applied inside the refresh");
        r.Expect(ReadSelection() == nullptr && !SchemeValueExists(), L"final preference is None");
        r.Expect(ActivePalette() == nullptr, L"final palette is unpublished");
        ExpectSnapshot(r, baseline, TakeSnapshot(WindowTheme()), nullptr, L"final state");
        ExpectSamePixels(r, stock, SamplePixels(r, f, StockText()), L"final state");
        DestroyWindow(nested);
        DestroyFixture(f);
        return r.Result();
    }

    HHOOK WINAPI FailingSetHook(int, HOOKPROC, HINSTANCE, DWORD)
    {
        SetLastError(ERROR_ACCESS_DENIED);
        return nullptr;
    }

    int CbtFailureRollsBackToStock(const CheckContext&)
    {
        Recorder r(L"CbtFailureRollsBackToStock");
        const ApiSnapshot baseline = TakeSnapshot(WindowTheme());
        Fixture f = CreateFixture(L"CbtFailureRollsBackToStock");
        Pump(400);
        const Pixels stock = SamplePixels(r, f, StockText());
        ControlSeams::setWindowsHookEx = FailingSetHook;
        StartTheming();
        const bool switched = Switch(FindScheme(L"Campbell"));
        ControlSeams::setWindowsHookEx = SetWindowsHookExW;
        Pump(500);
        r.Expect(!switched, L"activation reports failure");
        r.Expect(ActivePalette() == nullptr, L"palette unpublished");
        ExpectSnapshot(r, baseline, TakeSnapshot(WindowTheme()), nullptr, L"after CBT failure");
        ExpectSamePixels(r, stock, SamplePixels(r, f, StockText()), L"after CBT failure");
        r.Expect(LogLines(L"SetWindowsHookEx(WH_CBT) failed") == 1, L"CBT failure logged");
        DestroyFixture(f);
        return r.Result();
    }

    int NoneLeavesUnmodifiedWindowsAlone(const CheckContext&)
    {
        Recorder r(L"NoneLeavesUnmodifiedWindowsAlone");
        HWND top = CreateTopLevel(L"NoneLeavesUnmodifiedWindowsAlone");
        HWND tree = ShortTree(top, 10, 10);
        InstallCallRecorder();
        SetWindowTheme(tree, L"Explorer", nullptr);
        Pump(200);
        ClearRecordedCalls();
        StartTheming();
        r.Expect(Switch(FindScheme(L"Tango Light")), L"switch to Tango Light");
        Pump(300);
        r.Expect(Switch(nullptr), L"switch to None");
        Pump(300);
        r.Expect(Count(tree, RecordedCall::Kind::SetWindowTheme) == 0, L"light scheme and None leave the application's Explorer theme alone");

        r.Expect(Switch(FindScheme(L"Campbell")), L"switch to Campbell");
        Pump(300);
        r.Expect(Count(tree, RecordedCall::Kind::SetWindowTheme, L"DarkMode_Explorer") == 1, L"dark scheme themes the tree-view");
        r.Expect(Switch(nullptr), L"switch to None");
        Pump(300);
        if (Count(tree, RecordedCall::Kind::SetWindowTheme, L"(null)") == 1)
        {
            r.Note(L"LIMITATION after a dark scheme then None the tree-view's own Explorer theme is reset to the default theme");
        }
        DestroyWindow(top);
        return r.Result();
    }

    using DwmSetFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    DwmSetFn g_realDwmSet = nullptr;
    bool g_dwmRaised = false;

    HRESULT WINAPI RaiseOnceDwmSet(HWND window, DWORD attribute, LPCVOID data, DWORD size)
    {
        if (!g_dwmRaised)
        {
            g_dwmRaised = true;
            RaiseException(0xE0545451, 0, 0, nullptr);
        }
        return g_realDwmSet(window, attribute, data, size);
    }

    bool ArmDwmFault(Recorder& r)
    {
        g_realDwmSet = &DwmSetWindowAttribute;
        g_dwmRaised = false;
        return r.Expect(PatchImports(GetModuleHandleW(nullptr), reinterpret_cast<void*>(g_realDwmSet), reinterpret_cast<void*>(RaiseOnceDwmSet)),
                        L"DwmSetWindowAttribute import patched to raise once");
    }

    // Object-free so a fault that escapes the module is recorded as a failure instead of ending the process.
    bool SwitchCatchingFault(const ColourScheme* scheme, bool& escaped) noexcept
    {
        __try
        {
            return Switch(scheme);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            escaped = true;
            return false;
        }
    }

    void PumpCatchingFault(DWORD milliseconds, bool& escaped) noexcept
    {
        __try
        {
            Pump(milliseconds);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            escaped = true;
        }
    }

    void ExpectRecoveredAfterFault(Recorder& r, const ApiSnapshot& baseline, const Fixture& f, const Pixels& stock)
    {
        r.Expect(ActivePalette() == nullptr, L"palette unpublished after the fault");
        ExpectSnapshot(r, baseline, TakeSnapshot(WindowTheme()), nullptr, L"after the fault");
        ExpectSamePixels(r, stock, SamplePixels(r, f, StockText()), L"after the fault");
        r.Expect(LogLines(L"fault during theme switch") >= 1, L"fault logged");

        const ColourScheme* vintage = FindScheme(L"Vintage");
        bool escaped = false;
        r.Expect(SwitchCatchingFault(vintage, escaped) && !escaped, L"a later switch to Vintage works");
        Pump(500);
        const SystemColourPalette palette = BuildPalette(*vintage);
        ExpectSnapshot(r, baseline, TakeSnapshot(WindowTheme()), &palette, L"Vintage after the fault");
        ExpectThemedPixels(r, SamplePixels(r, f, *palette.At(COLOR_WINDOWTEXT)), palette, L"Vintage after the fault");
        r.Expect(SwitchCatchingFault(nullptr, escaped) && !escaped, L"a later switch to None works");
        Pump(500);
        ExpectSnapshot(r, baseline, TakeSnapshot(WindowTheme()), nullptr, L"None after the fault");
        ExpectSamePixels(r, stock, SamplePixels(r, f, StockText()), L"None after the fault");
        r.Expect(ReadSelection() == nullptr && !SchemeValueExists(), L"Scheme value deleted by the later None");
    }

    int FaultDuringSwitchRollsBackToStock(const CheckContext&)
    {
        Recorder r(L"FaultDuringSwitchRollsBackToStock");
        const ApiSnapshot baseline = TakeSnapshot(WindowTheme());
        Fixture f = CreateFixture(L"FaultDuringSwitchRollsBackToStock");
        Pump(400);
        const Pixels stock = SamplePixels(r, f, StockText());
        StartTheming();
        if (!ArmDwmFault(r))
        {
            return r.Result();
        }
        bool escaped = false;
        const bool switched = SwitchCatchingFault(FindScheme(L"Campbell"), escaped);
        r.Expect(!escaped, L"the fault did not escape Switch");
        r.Expect(!switched, L"the faulted switch reports failure");
        r.Expect(g_dwmRaised, L"the fault was raised during the switch");
        Pump(500);
        ExpectRecoveredAfterFault(r, baseline, f, stock);
        DestroyFixture(f);
        return r.Result();
    }

    int FaultInDeferredRefreshRollsBackToStock(const CheckContext&)
    {
        Recorder r(L"FaultInDeferredRefreshRollsBackToStock");
        const ApiSnapshot baseline = TakeSnapshot(WindowTheme());
        Fixture f = CreateFixture(L"FaultInDeferredRefreshRollsBackToStock");
        Pump(400);
        const Pixels stock = SamplePixels(r, f, StockText());
        if (!Save(r, FindScheme(L"Campbell")) || !ArmDwmFault(r))
        {
            return r.Result();
        }
        r.Expect(StartTheming(), L"saved Campbell activated with a deferred refresh");
        bool escaped = false;
        PumpCatchingFault(600, escaped);
        r.Expect(!escaped, L"the fault did not escape the deferred refresh");
        r.Expect(g_dwmRaised, L"the fault was raised during the deferred refresh");
        ExpectRecoveredAfterFault(r, baseline, f, stock);
        DestroyFixture(f);
        return r.Result();
    }

    using SetTimerFn = UINT_PTR(WINAPI*)(HWND, UINT_PTR, UINT, TIMERPROC);
    SetTimerFn g_realSetTimer = nullptr;

    UINT_PTR WINAPI RaisingSetTimer(HWND, UINT_PTR, UINT, TIMERPROC)
    {
        RaiseException(0xE0545452, 0, 0, nullptr);
        return 0;
    }

    int FaultAfterPublicationInFirstInit(const CheckContext& context)
    {
        Recorder r(L"FaultAfterPublicationInFirstInit");
        const ApiSnapshot baseline = TakeSnapshot(WindowTheme());
        ProductionModule module;
        if (!Save(r, FindScheme(L"Campbell")) || !LoadProductionModule(r, context, module))
        {
            return r.Result();
        }
        g_realSetTimer = &SetTimer;
        if (!r.Expect(PatchImports(module.handle, reinterpret_cast<void*>(g_realSetTimer), reinterpret_cast<void*>(RaisingSetTimer)),
                      L"module imports SetTimer, called after the palette is published"))
        {
            return r.Result();
        }
        r.Expect(module.customInitialize(kExplorerMask, nullptr) == IDOK, L"faulting first call returns IDOK");
        const ULONGLONG start = GetTickCount64();
        const LONG second = module.customInitialize(kExplorerMask, nullptr);
        const ULONGLONG elapsed = GetTickCount64() - start;
        r.Expect(second == IDOK && elapsed < 1000, L"second call returns IDOK in %llu ms", elapsed);
        Pump(300);
        r.Expect(LogLines(L"colour hooks installed") == 1, L"the fault came after the hooks were installed");
        ExpectSnapshot(r, baseline, TakeSnapshot(WindowTheme()), nullptr, L"after a fault following publication");
        r.Expect(LogLines(L"initialisation fault") == 1, L"fault logged");
        return r.Result();
    }

    using SetWindowThemeFn = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
    SetWindowThemeFn g_realSetWindowTheme = nullptr;
    bool g_themeRaised = false;

    HRESULT WINAPI RaiseOnceSetWindowTheme(HWND window, LPCWSTR app, LPCWSTR idList)
    {
        if (!g_themeRaised)
        {
            g_themeRaised = true;
            RaiseException(0xE0545454, 0, 0, nullptr);
        }
        return g_realSetWindowTheme(window, app, idList);
    }

    int FaultInDeferredQueueRollsBackToStock(const CheckContext&)
    {
        Recorder r(L"FaultInDeferredQueueRollsBackToStock");
        const ApiSnapshot baseline = TakeSnapshot(WindowTheme());
        Fixture f = CreateFixture(L"FaultInDeferredQueueRollsBackToStock");
        Pump(400);
        StartTheming();
        if (!r.Expect(Switch(FindScheme(L"Campbell")), L"switch to Campbell"))
        {
            return r.Result();
        }
        Pump(300);
        g_realSetWindowTheme = &SetWindowTheme;
        g_themeRaised = false;
        if (!r.Expect(PatchImports(GetModuleHandleW(nullptr), reinterpret_cast<void*>(g_realSetWindowTheme), reinterpret_cast<void*>(RaiseOnceSetWindowTheme)),
                      L"SetWindowTheme import patched to raise once"))
        {
            return r.Result();
        }
        HWND tree = ShortTree(f.top, 330, 190, 200, 100);
        bool escaped = false;
        PumpCatchingFault(500, escaped);
        r.Expect(g_themeRaised, L"the fault was raised while the queued tree-view was themed");
        r.Expect(!escaped, L"the fault did not escape the deferred queue");
        r.Expect(ActivePalette() == nullptr, L"palette unpublished after the fault");
        ExpectSnapshot(r, baseline, TakeSnapshot(WindowTheme()), nullptr, L"after a fault in the deferred queue");
        r.Expect(LogLines(L"fault in deferred theming") >= 1, L"fault logged");
        bool later = false;
        r.Expect(SwitchCatchingFault(FindScheme(L"Vintage"), later) && !later, L"a later switch still works");
        Pump(300);
        r.Expect(GetSysColor(COLOR_WINDOW) == FindScheme(L"Vintage")->background, L"Vintage applied after the fault");
        Switch(nullptr);
        Pump(200);
        DestroyWindow(tree);
        DestroyFixture(f);
        return r.Result();
    }

    const CheckRegistration registrations[] = {
        CheckRegistration{{L"Lifecycle", L"PreInit", L"pre-init", Host::Pwc, true, PreInit}},
        CheckRegistration{{L"Lifecycle", L"PostInit", L"post-init", Host::Pwc, true, PostInit}},
        CheckRegistration{{L"Lifecycle", L"Stock", L"stock", Host::Pwc, false, Stock}},
        CheckRegistration{{L"Lifecycle", L"NoneRestoresApisAndPixels", L"none", Host::Self, true, NoneRestoresApisAndPixels}},
        CheckRegistration{{L"Lifecycle", L"DarkToLightRefreshes", L"none", Host::Self, false, DarkToLightRefreshes}},
        CheckRegistration{{L"Lifecycle", L"NoneToSchemeTransition", L"none", Host::Self, false, NoneToSchemeTransition}},
        CheckRegistration{{L"Lifecycle", L"InitReturnsIdok", L"-", Host::Pwc, false, InitReturnsIdok}},
        CheckRegistration{{L"Lifecycle", L"InitRunsOnce", L"-", Host::Pwc, false, InitRunsOnce}},
        CheckRegistration{{L"Lifecycle", L"OtherMaskIsIgnored", L"-", Host::Pwc, false, OtherMaskIsIgnored}},
        CheckRegistration{{L"Lifecycle", L"PickerSkippedWithoutDmawin", L"-", Host::Pwc, false, PickerSkippedWithoutDmawin}},
        CheckRegistration{{L"Lifecycle", L"FaultInFirstInitDoesNotHangSecond", L"-", Host::Pwc, false, FaultInFirstInitDoesNotHangSecond}},
        CheckRegistration{{L"Lifecycle", L"NonExplorerHostIsUntouched", L"-", Host::Self, false, NonExplorerHostIsUntouched}},
        CheckRegistration{{L"Lifecycle", L"RefreshIsProcessBounded", L"-", Host::Self, false, RefreshIsProcessBounded}},
        CheckRegistration{{L"Lifecycle", L"StalledSecondaryThreadDoesNotBlock", L"-", Host::Self, false, StalledSecondaryThreadDoesNotBlock}},
        CheckRegistration{{L"Lifecycle", L"NestedNoneDuringRefreshIsDeferred", L"-", Host::Self, false, NestedNoneDuringRefreshIsDeferred}},
        CheckRegistration{{L"Lifecycle", L"CbtFailureRollsBackToStock", L"-", Host::Self, false, CbtFailureRollsBackToStock}},
        CheckRegistration{{L"Lifecycle", L"NoneLeavesUnmodifiedWindowsAlone", L"-", Host::Self, false, NoneLeavesUnmodifiedWindowsAlone}},
        CheckRegistration{{L"Lifecycle", L"FaultDuringSwitchRollsBackToStock", L"-", Host::Self, false, FaultDuringSwitchRollsBackToStock}},
        CheckRegistration{{L"Lifecycle", L"FaultInDeferredRefreshRollsBackToStock", L"-", Host::Self, false, FaultInDeferredRefreshRollsBackToStock}},
        CheckRegistration{{L"Lifecycle", L"FaultAfterPublicationInFirstInit", L"-", Host::Pwc, false, FaultAfterPublicationInFirstInit}},
        CheckRegistration{{L"Lifecycle", L"FaultInDeferredQueueRollsBackToStock", L"-", Host::Self, false, FaultInDeferredQueueRollsBackToStock}},
    };
}
