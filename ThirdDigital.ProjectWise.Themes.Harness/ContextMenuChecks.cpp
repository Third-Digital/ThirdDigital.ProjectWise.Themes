#include "Fixtures.h"

#include "Features/ContextMenus/ContextMenus.h"
#include "Features/ThemeSwitch.h"
#include "Shared/ActivePalette.h"

#include <dwmapi.h>

#include <algorithm>
#include <cwchar>
#include <map>
#include <vector>

using namespace Harness;

namespace
{
    int g_getProcCalls = 0;
    int g_modeCalls = 0;
    int g_lastMode = -1;
    int g_flushCalls = 0;
    std::vector<std::pair<HWND, bool>> g_allows;
    WORD g_missingOrdinal = 0;

    int WINAPI FakeSetPreferredAppMode(int mode)
    {
        ++g_modeCalls;
        g_lastMode = mode;
        return 0;
    }

    bool WINAPI FakeAllowDarkModeForWindow(HWND window, bool allow)
    {
        g_allows.emplace_back(window, allow);
        return true;
    }

    void WINAPI FakeFlushMenuThemes()
    {
        ++g_flushCalls;
    }

    FARPROC WINAPI FakeGetProcAddress(HMODULE module, LPCSTR name)
    {
        ++g_getProcCalls;
        if (IS_INTRESOURCE(name))
        {
            const WORD ordinal = LOWORD(reinterpret_cast<ULONG_PTR>(name));
            if (ordinal == g_missingOrdinal)
            {
                return nullptr;
            }
            switch (ordinal)
            {
            case 133:
                return reinterpret_cast<FARPROC>(FakeAllowDarkModeForWindow);
            case 135:
                return reinterpret_cast<FARPROC>(FakeSetPreferredAppMode);
            case 136:
                return reinterpret_cast<FARPROC>(FakeFlushMenuThemes);
            default:
                break;
            }
        }
        return GetProcAddress(module, name);
    }

    DWORD OldBuild() noexcept
    {
        return 17763;
    }

    DWORD SupportedBuild() noexcept
    {
        return 19045;
    }

    int FakeCalls()
    {
        return g_modeCalls + g_flushCalls + static_cast<int>(g_allows.size());
    }

    int OldBuildMakesNoCalls(const CheckContext&)
    {
        Recorder r(L"OldBuildMakesNoCalls");
        ContextMenuSeams::buildNumber = OldBuild;
        ContextMenuSeams::getProcAddress = FakeGetProcAddress;
        HWND top = CreateTopLevel(L"OldBuildMakesNoCalls");
        r.Expect(!InitializeContextMenus(), L"slice disabled below build 18362");
        ApplyContextMenus(true);
        AllowContextMenuDarkMode(top, true);
        ApplyContextMenus(false);
        AllowContextMenuDarkMode(top, false);
        r.Expect(g_getProcCalls == 0 && FakeCalls() == 0, L"no ordinals resolved or called (%d lookups, %d calls)", g_getProcCalls, FakeCalls());
        DestroyWindow(top);
        return r.Result();
    }

    int MissingOrdinalMakesNoCalls(const CheckContext&)
    {
        Recorder r(L"MissingOrdinalMakesNoCalls");
        ContextMenuSeams::buildNumber = SupportedBuild;
        ContextMenuSeams::getProcAddress = FakeGetProcAddress;
        g_missingOrdinal = 136;
        HWND top = CreateTopLevel(L"MissingOrdinalMakesNoCalls");
        r.Expect(!InitializeContextMenus(), L"slice disabled when #136 is missing");
        ApplyContextMenus(true);
        AllowContextMenuDarkMode(top, true);
        ApplyContextMenus(false);
        r.Expect(FakeCalls() == 0, L"no ordinal called (%d calls)", FakeCalls());
        DestroyWindow(top);
        return r.Result();
    }

    int LightAndNoneRestoreDefault(const CheckContext&)
    {
        Recorder r(L"LightAndNoneRestoreDefault");
        ContextMenuSeams::buildNumber = SupportedBuild;
        ContextMenuSeams::getProcAddress = FakeGetProcAddress;
        HWND top = CreateTopLevel(L"LightAndNoneRestoreDefault");
        HWND other = CreateTopLevel(L"LightAndNoneRestoreDefault other");
        if (!r.Expect(InitializeContextMenus(), L"slice enabled with all three ordinals"))
        {
            return r.Result();
        }
        ApplyContextMenus(true);
        AllowContextMenuDarkMode(top, true);
        r.Expect(g_lastMode == 2 && g_flushCalls >= 1, L"dark scheme: ForceDark then flush (mode %d, %d flushes)", g_lastMode, g_flushCalls);
        r.Expect(g_allows.size() == 1 && g_allows[0].first == top && g_allows[0].second, L"window permission granted");
        const int flushes = g_flushCalls;
        ApplyContextMenus(false);
        AllowContextMenuDarkMode(top, false);
        AllowContextMenuDarkMode(other, false);
        r.Expect(g_lastMode == 0 && g_flushCalls > flushes, L"light or None: Default then flush (mode %d)", g_lastMode);
        r.Expect(g_allows.size() == 2 && g_allows[1].first == top && !g_allows[1].second, L"permission withdrawn only where granted");
        DestroyWindow(other);
        DestroyWindow(top);
        return r.Result();
    }

    int ContextFailureDoesNotBreakOtherSlices(const CheckContext&)
    {
        Recorder r(L"ContextFailureDoesNotBreakOtherSlices");
        ContextMenuSeams::buildNumber = SupportedBuild;
        ContextMenuSeams::getProcAddress = FakeGetProcAddress;
        g_missingOrdinal = 135;
        Fixture f = CreateFixture(L"ContextFailureDoesNotBreakOtherSlices");
        Pump(300);
        StartTheming();
        r.Expect(Switch(FindScheme(L"Campbell")), L"switch succeeds with the context-menu slice disabled");
        Pump(500);
        const SystemColourPalette palette = BuildPalette(*FindScheme(L"Campbell"));
        r.Expect(GetSysColor(COLOR_WINDOW) == *palette.At(COLOR_WINDOW), L"system colours themed");
        ExpectThemedPixels(r, SamplePixels(r, f, *palette.At(COLOR_WINDOWTEXT)), palette, L"slice disabled");
        r.Expect(FakeCalls() == 0, L"disabled slice made no calls");
        DestroyFixture(f);
        return r.Result();
    }

    struct MenuProbe
    {
        Recorder* recorder = nullptr;
        bool opened = false;
        std::optional<COLORREF> background;
        int width = 0;
        int height = 0;
    };

    MenuProbe* g_probe = nullptr;

    BOOL CALLBACK FindMenuWindow(HWND window, LPARAM found)
    {
        wchar_t name[16]{};
        GetClassNameW(window, name, ARRAYSIZE(name));
        if (wcscmp(name, L"#32768") == 0 && IsWindowVisible(window))
        {
            *reinterpret_cast<HWND*>(found) = window;
            return FALSE;
        }
        return TRUE;
    }

    void CaptureMenu(MenuProbe& probe)
    {
        HWND menu = nullptr;
        EnumThreadWindows(GetCurrentThreadId(), FindMenuWindow, reinterpret_cast<LPARAM>(&menu));
        if (menu == nullptr)
        {
            probe.recorder->Invalidate(L"no visible #32768 menu window of this thread");
            return;
        }
        RECT rect{};
        GetWindowRect(menu, &rect);
        probe.width = rect.right - rect.left;
        probe.height = rect.bottom - rect.top;
        DwmFlush();
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = probe.width;
        info.bmiHeader.biHeight = -probe.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HDC screen = GetDC(nullptr);
        HDC memory = CreateCompatibleDC(screen);
        HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, screen);
        if (memory == nullptr || bitmap == nullptr || probe.width <= 16 || probe.height <= 16)
        {
            probe.recorder->Invalidate(L"menu capture buffer could not be created");
            return;
        }
        constexpr DWORD sentinel = 0x00FE01FE;
        DWORD* raw = static_cast<DWORD*>(bits);
        const std::size_t count = static_cast<std::size_t>(probe.width) * static_cast<std::size_t>(probe.height);
        std::fill(raw, raw + count, sentinel);
        HGDIOBJ previous = SelectObject(memory, bitmap);
        const BOOL printed = PrintWindow(menu, memory, PW_RENDERFULLCONTENT);
        GdiFlush();
        std::map<DWORD, int> histogram;
        int undrawn = 0;
        for (int y = 8; y < probe.height - 8; ++y)
        {
            for (int x = 8; x < probe.width - 8; ++x)
            {
                const DWORD p = raw[static_cast<std::size_t>(y) * static_cast<std::size_t>(probe.width) + static_cast<std::size_t>(x)];
                if (p == sentinel)
                {
                    ++undrawn;
                }
                ++histogram[p & 0x00FFFFFF];
            }
        }
        SelectObject(memory, previous);
        DeleteObject(bitmap);
        DeleteDC(memory);
        if (!printed || undrawn > 0)
        {
            probe.recorder->Invalidate(L"menu capture invalid (PrintWindow %d, %d undrawn pixels)", printed, undrawn);
            return;
        }
        DWORD mode = 0;
        int most = 0;
        for (const auto& [p, n] : histogram)
        {
            if (n > most)
            {
                most = n;
                mode = p;
            }
        }
        probe.background = RGB((mode >> 16) & 0xFF, (mode >> 8) & 0xFF, mode & 0xFF);
    }

    LRESULT CALLBACK GateProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_TIMER && wParam == 1 && g_probe != nullptr)
        {
            KillTimer(window, 1);
            SetTimer(window, 2, 700, nullptr);
            HMENU menu = CreatePopupMenu();
            for (const wchar_t* item : {L"Open", L"Copy", L"Properties", L"Refresh", L"Check Out", L"Check In"})
            {
                AppendMenuW(menu, MF_STRING, 1, item);
            }
            RECT rect{};
            GetWindowRect(window, &rect);
            SetForegroundWindow(window);
            g_probe->opened = true;
            TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, rect.left + 40, rect.top + 60, 0, window, nullptr);
            DestroyMenu(menu);
            return 0;
        }
        if (message == WM_TIMER && wParam == 2 && g_probe != nullptr)
        {
            KillTimer(window, 2);
            CaptureMenu(*g_probe);
            EndMenu();
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    std::optional<COLORREF> OpenAndCaptureMenu(Recorder& r, HWND owner)
    {
        MenuProbe probe;
        probe.recorder = &r;
        g_probe = &probe;
        SetTimer(owner, 1, 300, nullptr);
        Pump(1800);
        g_probe = nullptr;
        if (!probe.opened)
        {
            r.Invalidate(L"menu was not opened");
        }
        return probe.background;
    }

    DWORD RealBuild()
    {
        using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
        auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
        OSVERSIONINFOW info{sizeof(info)};
        return rtlGetVersion != nullptr && rtlGetVersion(&info) == 0 ? info.dwBuildNumber : 0;
    }

    int TrackPopupMenuBackgroundIsDark(const CheckContext& context)
    {
        Recorder r(L"TrackPopupMenuBackgroundIsDark");
        WNDCLASSW wc{};
        wc.lpfnWndProc = GateProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_3DFACE + 1);
        wc.lpszClassName = L"TDPWHarnessMenuOwner";
        RegisterClassW(&wc);
        HWND owner = CreateWindowExW(WS_EX_TOPMOST, L"TDPWHarnessMenuOwner", L"TrackPopupMenuBackgroundIsDark", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 120, 120,
                                     420, 320, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Pump(400);

        DWORD light = 0xFFFFFFFF;
        DWORD bytes = sizeof(light);
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD,
                     nullptr, &light, &bytes);
        const DWORD build = RealBuild();

        const auto stock = OpenAndCaptureMenu(r, owner);
        StartTheming();
        const bool switched = Switch(FindScheme(L"Campbell"));
        Pump(500);
        const auto themed = OpenAndCaptureMenu(r, owner);
        Switch(nullptr);
        Pump(300);

        const double stockLum = stock ? RelativeLuminance(*stock) : -1.0;
        const double themedLum = themed ? RelativeLuminance(*themed) : -1.0;
        wchar_t evidence[512]{};
        swprintf_s(evidence,
                   L"windows_build=%lu\napps_use_light_theme=%ld\nscheme=Campbell\nswitched=%d\nstock_background=%06lX\nstock_luminance=%.4f\n"
                   L"themed_background=%06lX\nthemed_luminance=%.4f\n",
                   build, static_cast<long>(light), switched ? 1 : 0, stock.value_or(0), stockLum, themed.value_or(0), themedLum);
        WriteEvidence(context, L"context-menu-gate.txt", evidence);
        r.Note(L"%s", evidence);

        if (stock && themed)
        {
            if (stockLum <= 0.5)
            {
                r.Invalidate(L"stock menu capture is not light (%.3f), so it cannot serve as the comparison", stockLum);
            }
            r.Expect(switched, L"switched to Campbell");
            r.Expect(themedLum < 0.2, L"themed menu background is dark (%.3f)", themedLum);
            r.Expect(stockLum - themedLum >= 0.3, L"themed menu differs from stock by at least 0.3 (%.3f)", stockLum - themedLum);
        }
        DestroyWindow(owner);
        return r.Result();
    }

    const CheckRegistration registrations[] = {
        CheckRegistration{{L"ContextMenus", L"OldBuildMakesNoCalls", L"-", Host::Self, false, OldBuildMakesNoCalls}},
        CheckRegistration{{L"ContextMenus", L"MissingOrdinalMakesNoCalls", L"-", Host::Self, false, MissingOrdinalMakesNoCalls}},
        CheckRegistration{{L"ContextMenus", L"LightAndNoneRestoreDefault", L"-", Host::Self, false, LightAndNoneRestoreDefault}},
        CheckRegistration{{L"ContextMenus", L"ContextFailureDoesNotBreakOtherSlices", L"-", Host::Self, false, ContextFailureDoesNotBreakOtherSlices}},
        CheckRegistration{{L"ContextMenus", L"TrackPopupMenuBackgroundIsDark", L"-", Host::Self, false, TrackPopupMenuBackgroundIsDark}},
    };
}
