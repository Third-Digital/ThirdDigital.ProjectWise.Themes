#include "Fixtures.h"

#include "Features/SystemColours/SystemColourHooks.h"
#include "Shared/ActivePalette.h"

#include <vssym32.h>

#include <atomic>
#include <thread>

using namespace Harness;

namespace
{
    const PaletteResources* Campbell()
    {
        return PreparePalette(*FindScheme(L"Campbell"));
    }

    bool Mapped(const PaletteResources* p, int index)
    {
        return p != nullptr && p->palette.At(index).has_value();
    }

    template <typename T>
    bool Same(const std::optional<T>& a, const std::optional<T>& b)
    {
        return a.has_value() == b.has_value() && (!a || *a == *b);
    }

    void ExpectApis(Recorder& r, const ApiSnapshot& baseline, const ApiSnapshot& now, const PaletteResources* palette, const wchar_t* label)
    {
        Harness::ExpectSnapshot(r, baseline, now, palette != nullptr ? &palette->palette : nullptr, label);
    }

    int FourApisReturnPalette(const CheckContext&)
    {
        Recorder r(L"FourApisReturnPalette");
        const ApiSnapshot baseline = TakeSnapshot(WindowTheme());
        const PaletteResources* p = Campbell();
        if (!r.Expect(p != nullptr, L"palette prepared") || !r.Expect(InstallSystemColourHooks(), L"hooks installed"))
        {
            return r.Result();
        }
        PublishPalette(p);
        ExpectApis(r, baseline, TakeSnapshot(WindowTheme()), p, L"published");
        r.Expect(GetSysColor(COLOR_3DFACE) == RGB(0x1B, 0x1B, 0x1B), L"GetSysColor(COLOR_3DFACE) is Campbell #1B1B1B");
        return r.Result();
    }

    int BrushObjectsHaveExpectedColours(const CheckContext&)
    {
        Recorder r(L"BrushObjectsHaveExpectedColours");
        const PaletteResources* p = Campbell();
        if (!r.Expect(p != nullptr && InstallSystemColourHooks(), L"hooks installed"))
        {
            return r.Result();
        }
        PublishPalette(p);
        for (int i = 0; i <= COLOR_MENUBAR; ++i)
        {
            if (!Mapped(p, i))
            {
                continue;
            }
            const COLORREF want = *p->palette.At(i);
            HBRUSH shared = GetSysColorBrush(i);
            r.Expect(shared == p->Brush(i), L"GetSysColorBrush(%d) is the published shared brush", i);
            r.Expect(GetObjectType(shared) == OBJ_BRUSH && Same(BrushColour(shared), std::optional<COLORREF>(want)), L"shared brush %d colour", i);
            HBRUSH first = GetThemeSysColorBrush(WindowTheme(), i);
            HBRUSH second = GetThemeSysColorBrush(WindowTheme(), i);
            r.Expect(first != nullptr && first != second && first != shared, L"GetThemeSysColorBrush(%d) returns a new caller-owned brush", i);
            r.Expect(Same(BrushColour(first), std::optional<COLORREF>(want)), L"theme brush %d colour", i);
            DeleteObject(first);
            DeleteObject(second);
        }
        return r.Result();
    }

    int DeletingThemeBrushKeepsSysBrushValid(const CheckContext&)
    {
        Recorder r(L"DeletingThemeBrushKeepsSysBrushValid");
        const PaletteResources* p = Campbell();
        if (!r.Expect(p != nullptr && InstallSystemColourHooks(), L"hooks installed"))
        {
            return r.Result();
        }
        PublishPalette(p);
        for (int i : {COLOR_WINDOW, COLOR_3DFACE, COLOR_HIGHLIGHT})
        {
            HBRUSH owned = GetThemeSysColorBrush(WindowTheme(), i);
            r.Expect(DeleteObject(owned) != FALSE, L"caller deletes its theme brush %d", i);
            HBRUSH shared = GetSysColorBrush(i);
            r.Expect(GetObjectType(shared) == OBJ_BRUSH && Same(BrushColour(shared), p->palette.At(i)), L"shared brush %d still valid", i);
        }
        return r.Result();
    }

    int DeletedSysBrushFallsBackToStock(const CheckContext&)
    {
        Recorder r(L"DeletedSysBrushFallsBackToStock");
        const HBRUSH stock = GetSysColorBrush(COLOR_WINDOW);
        const PaletteResources* prepared = Campbell();
        if (!r.Expect(prepared != nullptr && InstallSystemColourHooks(), L"hooks installed")) return r.Result();
        // Solid brushes can survive DeleteObject in GDI's cache. Use a hatch brush to exercise
        // the invalid-handle fallback deterministically, without changing production allocation.
        PaletteResources palette = *prepared;
        HBRUSH shared = CreateHatchBrush(HS_CROSS, RGB(10, 20, 30));
        palette.brushes[COLOR_WINDOW] = shared;
        PublishPalette(&palette);
        r.Expect(GetSysColorBrush(COLOR_WINDOW) == shared, L"published brush returned before deletion");
        r.Expect(DeleteObject(shared) != FALSE, L"test brush deleted");
        GdiFlush();
        LOGBRUSH info{};
        r.Expect(GetObjectW(shared, sizeof(info), &info) == 0, L"fixture has an invalid brush handle");
        r.Expect(GetSysColorBrush(COLOR_WINDOW) == stock, L"a deleted shared brush falls through to the stock brush");
        r.Expect(GetSysColorBrush(COLOR_3DFACE) == palette.Brush(COLOR_3DFACE), L"other shared brushes unaffected");
        PublishPalette(nullptr);
        return r.Result();
    }

    int RuntimeResolvedCallsAreHooked(const CheckContext&)
    {
        Recorder r(L"RuntimeResolvedCallsAreHooked");
        const PaletteResources* p = Campbell();
        if (!r.Expect(p != nullptr && InstallSystemColourHooks(), L"hooks installed"))
        {
            return r.Result();
        }
        PublishPalette(p);
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        HMODULE uxtheme = GetModuleHandleW(L"uxtheme.dll");
        auto getSysColor = reinterpret_cast<DWORD(WINAPI*)(int)>(GetProcAddress(user32, "GetSysColor"));
        auto getSysColorBrush = reinterpret_cast<HBRUSH(WINAPI*)(int)>(GetProcAddress(user32, "GetSysColorBrush"));
        auto getThemeSysColor = reinterpret_cast<COLORREF(WINAPI*)(HTHEME, int)>(GetProcAddress(uxtheme, "GetThemeSysColor"));
        auto getThemeSysColorBrush = reinterpret_cast<HBRUSH(WINAPI*)(HTHEME, int)>(GetProcAddress(uxtheme, "GetThemeSysColorBrush"));
        if (getSysColor == nullptr || getSysColorBrush == nullptr || getThemeSysColor == nullptr || getThemeSysColorBrush == nullptr)
        {
            r.Invalidate(L"exports not resolved");
            return r.Result();
        }
        const COLORREF want = *p->palette.At(COLOR_WINDOW);
        r.Expect(getSysColor(COLOR_WINDOW) == want, L"GetProcAddress GetSysColor");
        r.Expect(getSysColorBrush(COLOR_WINDOW) == p->Brush(COLOR_WINDOW), L"GetProcAddress GetSysColorBrush");
        r.Expect(getThemeSysColor(WindowTheme(), COLOR_WINDOW) == want, L"GetProcAddress GetThemeSysColor");
        HBRUSH brush = getThemeSysColorBrush(WindowTheme(), COLOR_WINDOW);
        r.Expect(Same(BrushColour(brush), std::optional<COLORREF>(want)), L"GetProcAddress GetThemeSysColorBrush");
        DeleteObject(brush);
        return r.Result();
    }

    int UnmappedCallsMatchBaseline(const CheckContext& context)
    {
        Recorder r(L"UnmappedCallsMatchBaseline");
        HTHEME theme = WindowTheme();
        if (theme == nullptr)
        {
            r.Invalidate(L"no theme handle");
            return r.Result();
        }
        const ApiSnapshot baseline = TakeSnapshot(theme);

        // Which colour-ID range the real uxtheme functions accept: an accepted ID differs from the
        // invalid-ID result (9999) somewhere in the range.
        const std::size_t invalid = baseline.indices.size() - 1;
        bool colourAccepted = false;
        bool tmtColourAccepted = false;
        bool tmtBrushAccepted = false;
        bool colourBrushAccepted = false;
        for (std::size_t k = 0; k < invalid; ++k)
        {
            const int i = baseline.indices[k];
            const bool colourDiffers = baseline.themeColour[k] != baseline.themeColour[invalid];
            const bool brushDiffers = !Same(baseline.themeBrush[k], baseline.themeBrush[invalid]);
            if (i >= 0 && i <= COLOR_MENUBAR)
            {
                colourAccepted |= colourDiffers;
                colourBrushAccepted |= brushDiffers;
            }
            if (i >= TMT_FIRSTCOLOR && i <= TMT_LASTCOLOR)
            {
                tmtColourAccepted |= colourDiffers;
                tmtBrushAccepted |= brushDiffers;
            }
        }
        std::wstring ranges = L"GetThemeSysColor: COLOR_* " + std::wstring(colourAccepted ? L"accepted" : L"rejected") + L", TMT_* " +
                              (tmtColourAccepted ? L"accepted" : L"rejected") + L"\nGetThemeSysColorBrush: COLOR_* " +
                              (colourBrushAccepted ? L"accepted" : L"rejected") + L", TMT_* " + (tmtBrushAccepted ? L"accepted" : L"rejected") + L"\n";
        WriteEvidence(context, L"uxtheme-colour-id-ranges.txt", ranges);
        r.Note(L"%s", ranges.c_str());
        r.Expect(colourAccepted && colourBrushAccepted, L"both uxtheme functions accept COLOR_* IDs");
        r.Expect(!tmtColourAccepted && !tmtBrushAccepted, L"neither uxtheme function accepts TMT_* IDs, so the detours map only COLOR_*");

        const PaletteResources* p = Campbell();
        if (!r.Expect(p != nullptr && InstallSystemColourHooks(), L"hooks installed"))
        {
            return r.Result();
        }
        PublishPalette(p);
        ExpectApis(r, baseline, TakeSnapshot(theme), p, L"published");
        return r.Result();
    }

    int NullPalettePassesThrough(const CheckContext&)
    {
        Recorder r(L"NullPalettePassesThrough");
        const ApiSnapshot baseline = TakeSnapshot(WindowTheme());
        const PaletteResources* p = Campbell();
        if (!r.Expect(p != nullptr && InstallSystemColourHooks(), L"hooks installed"))
        {
            return r.Result();
        }
        ExpectApis(r, baseline, TakeSnapshot(WindowTheme()), nullptr, L"installed, unpublished");
        PublishPalette(p);
        r.Expect(GetSysColor(COLOR_WINDOW) == *p->palette.At(COLOR_WINDOW), L"published palette is visible");
        PublishPalette(nullptr);
        ExpectApis(r, baseline, TakeSnapshot(WindowTheme()), nullptr, L"published then nulled");
        return r.Result();
    }

    int g_failAt = 0;
    int g_calls = 0;

    MH_STATUS WINAPI FailingInitialize()
    {
        return MH_ERROR_MEMORY_ALLOC;
    }

    MH_STATUS WINAPI FailingCreate(LPCWSTR module, LPCSTR name, LPVOID detour, LPVOID* original, LPVOID* target)
    {
        if (++g_calls == g_failAt)
        {
            return MH_ERROR_FUNCTION_NOT_FOUND;
        }
        return MH_CreateHookApiEx(module, name, detour, original, target);
    }

    int EachInstallFailureLeavesStock(const CheckContext&)
    {
        Recorder r(L"EachInstallFailureLeavesStock");
        const ApiSnapshot baseline = TakeSnapshot(WindowTheme());
        const PaletteResources* p = Campbell();
        if (!r.Expect(p != nullptr, L"palette prepared"))
        {
            return r.Result();
        }

        HookSeams::initialize = FailingInitialize;
        r.Expect(!InstallSystemColourHooks(), L"initialisation failure reported");
        HookSeams::initialize = MH_Initialize;
        ExpectApis(r, baseline, TakeSnapshot(WindowTheme()), nullptr, L"after initialisation failure");

        for (int failAt = 1; failAt <= 4; ++failAt)
        {
            g_calls = 0;
            g_failAt = failAt;
            HookSeams::createHookApiEx = FailingCreate;
            const bool installed = InstallSystemColourHooks();
            HookSeams::createHookApiEx = MH_CreateHookApiEx;
            r.Expect(!installed, L"create failure %d reported", failAt);
            r.Expect(MH_Initialize() == MH_OK && MH_Uninitialize() == MH_OK, L"MinHook uninitialised after create failure %d", failAt);
            PublishPalette(p);
            ExpectApis(r, baseline, TakeSnapshot(WindowTheme()), nullptr, L"after create failure");
            PublishPalette(nullptr);
        }

        r.Expect(InstallSystemColourHooks(), L"a later complete installation still succeeds");
        PublishPalette(p);
        r.Expect(GetSysColor(COLOR_WINDOW) == *p->palette.At(COLOR_WINDOW), L"hooks active after recovery");
        return r.Result();
    }

    int g_enableCalls = 0;
    int g_seamCalls = 0;

    MH_STATUS WINAPI FailThirdEnable(LPVOID target)
    {
        ++g_seamCalls;
        if (++g_enableCalls == 3)
        {
            return MH_ERROR_MEMORY_PROTECT;
        }
        return MH_EnableHook(target);
    }

    MH_STATUS WINAPI CountingInitialize()
    {
        ++g_seamCalls;
        return MH_Initialize();
    }

    MH_STATUS WINAPI CountingCreate(LPCWSTR module, LPCSTR name, LPVOID detour, LPVOID* original, LPVOID* target)
    {
        ++g_seamCalls;
        return MH_CreateHookApiEx(module, name, detour, original, target);
    }

    MH_STATUS WINAPI CountingEnable(LPVOID target)
    {
        ++g_seamCalls;
        return MH_EnableHook(target);
    }

    int PartialEnableLeavesPassThrough(const CheckContext&)
    {
        Recorder r(L"PartialEnableLeavesPassThrough");
        const ApiSnapshot baseline = TakeSnapshot(WindowTheme());
        const PaletteResources* p = Campbell();
        std::atomic<bool> stop{false};
        std::atomic<long> calls{0};
        std::atomic<bool> sawOther{false};
        const DWORD stockWindow = GetSysColor(COLOR_WINDOW);
        std::thread caller([&] {
            while (!stop.load())
            {
                if (GetSysColor(COLOR_WINDOW) != stockWindow)
                {
                    sawOther = true;
                }
                ++calls;
            }
        });
        while (calls.load() < 1000)
        {
            Sleep(1);
        }

        HookSeams::enableHook = FailThirdEnable;
        const bool installed = InstallSystemColourHooks();
        HookSeams::enableHook = MH_EnableHook;
        r.Expect(!installed, L"third enable failure reported");
        r.Expect(g_enableCalls == 3, L"enables attempted one at a time (%d)", g_enableCalls);

        const long before = calls.load();
        Sleep(200);
        r.Expect(calls.load() > before, L"the looping caller keeps running");
        stop = true;
        caller.join();
        r.Expect(!sawOther.load(), L"the looping caller only ever saw the stock colour");

        PublishPalette(p);
        ExpectApis(r, baseline, TakeSnapshot(WindowTheme()), nullptr, L"after partial enable, palette published");
        PublishPalette(nullptr);

        g_seamCalls = 0;
        HookSeams::initialize = CountingInitialize;
        HookSeams::createHookApiEx = CountingCreate;
        HookSeams::enableHook = CountingEnable;
        r.Expect(!InstallSystemColourHooks(), L"a later attempt stays in pass-through");
        HookSeams::initialize = MH_Initialize;
        HookSeams::createHookApiEx = MH_CreateHookApiEx;
        HookSeams::enableHook = MH_EnableHook;
        r.Expect(g_seamCalls == 0, L"a later attempt makes no MinHook calls (%d)", g_seamCalls);
        return r.Result();
    }

    int RepeatedActivationDoesNotReinstall(const CheckContext&)
    {
        Recorder r(L"RepeatedActivationDoesNotReinstall");
        if (!r.Expect(InstallSystemColourHooks(), L"first installation"))
        {
            return r.Result();
        }
        g_seamCalls = 0;
        HookSeams::initialize = CountingInitialize;
        HookSeams::createHookApiEx = CountingCreate;
        HookSeams::enableHook = CountingEnable;
        const bool again = InstallSystemColourHooks();
        HookSeams::initialize = MH_Initialize;
        HookSeams::createHookApiEx = MH_CreateHookApiEx;
        HookSeams::enableHook = MH_EnableHook;
        r.Expect(again, L"second activation reports success");
        r.Expect(g_seamCalls == 0, L"second activation makes no MinHook calls (%d)", g_seamCalls);
        return r.Result();
    }

    const CheckRegistration registrations[] = {
        CheckRegistration{{L"SystemColours", L"FourApisReturnPalette", L"-", Host::Self, false, FourApisReturnPalette}},
        CheckRegistration{{L"SystemColours", L"BrushObjectsHaveExpectedColours", L"-", Host::Self, false, BrushObjectsHaveExpectedColours}},
        CheckRegistration{{L"SystemColours", L"DeletingThemeBrushKeepsSysBrushValid", L"-", Host::Self, false, DeletingThemeBrushKeepsSysBrushValid}},
        CheckRegistration{{L"SystemColours", L"DeletedSysBrushFallsBackToStock", L"-", Host::Self, false, DeletedSysBrushFallsBackToStock}},
        CheckRegistration{{L"SystemColours", L"RuntimeResolvedCallsAreHooked", L"-", Host::Self, false, RuntimeResolvedCallsAreHooked}},
        CheckRegistration{{L"SystemColours", L"UnmappedCallsMatchBaseline", L"-", Host::Self, false, UnmappedCallsMatchBaseline}},
        CheckRegistration{{L"SystemColours", L"NullPalettePassesThrough", L"-", Host::Self, false, NullPalettePassesThrough}},
        CheckRegistration{{L"SystemColours", L"EachInstallFailureLeavesStock", L"-", Host::Self, false, EachInstallFailureLeavesStock}},
        CheckRegistration{{L"SystemColours", L"PartialEnableLeavesPassThrough", L"-", Host::Self, false, PartialEnableLeavesPassThrough}},
        CheckRegistration{{L"SystemColours", L"RepeatedActivationDoesNotReinstall", L"-", Host::Self, false, RepeatedActivationDoesNotReinstall}},
    };
}
