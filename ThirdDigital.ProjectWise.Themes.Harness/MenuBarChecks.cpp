#include "Fixtures.h"

#include "Features/Persistence/ThemeSettings.h"
#include "Features/ThemeSwitch.h"

#include <dwmapi.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cwchar>
#include <map>
#include <vector>

using namespace Harness;

namespace
{
    struct MenuBarSample
    {
        COLORREF background = 0;
        COLORREF itemBackground = 0;
        COLORREF closestText = 0;
        int textDistance = INT_MAX;
        COLORREF underline = 0;
    };

    std::optional<MenuBarSample> SampleMenuBar(Recorder& r, HWND window, COLORREF expectedText)
    {
        MENUBARINFO bar{sizeof(bar)};
        RECT item{};
        HMENU menu = GetMenu(window);
        const int items = menu != nullptr ? GetMenuItemCount(menu) : 0;
        RECT last{};
        if (items < 1 || !GetMenuBarInfo(window, OBJID_MENU, 0, &bar) || !GetMenuItemRect(window, menu, 0, &item) ||
            !GetMenuItemRect(window, menu, static_cast<UINT>(items - 1), &last))
        {
            r.Invalidate(L"menu-bar geometry unavailable");
            return std::nullopt;
        }
        const RECT barRect = ToWindow(window, bar.rcBar);
        const RECT itemRect = ToWindow(window, item);
        const RECT lastRect = ToWindow(window, last);
        POINT clientOrigin{0, 0};
        ClientToScreen(window, &clientOrigin);
        RECT windowRect{};
        GetWindowRect(window, &windowRect);
        const int underlineY = clientOrigin.y - windowRect.top - 1;

        const RECT empty{lastRect.right + 8, barRect.top + 2, barRect.right - 8, barRect.bottom - 2};
        const RECT text{itemRect.left + 1, itemRect.top + 1, itemRect.right - 1, itemRect.bottom - 1};
        const RECT underline{barRect.left + 4, underlineY, barRect.right - 4, underlineY + 1};
        const auto emptyHistogram = CaptureRegion(r, window, empty);
        const auto textHistogram = CaptureRegion(r, window, text);
        const auto underlineHistogram = CaptureRegion(r, window, underline);
        if (!emptyHistogram || !textHistogram || !underlineHistogram)
        {
            return std::nullopt;
        }
        MenuBarSample sample;
        sample.background = MostCommon(*emptyHistogram);
        sample.itemBackground = MostCommon(*textHistogram);
        sample.underline = MostCommon(*underlineHistogram);
        for (const auto& [c, n] : *textHistogram)
        {
            if (c != sample.itemBackground && n >= 3 && Distance(c, expectedText) < sample.textDistance)
            {
                sample.textDistance = Distance(c, expectedText);
                sample.closestText = c;
            }
        }
        return sample;
    }

    void ExpectThemedBar(Recorder& r, HWND window, const ColourScheme& scheme, const std::optional<MenuBarSample>& stock)
    {
        const SystemColourPalette palette = BuildPalette(scheme);
        const COLORREF bar = *palette.At(COLOR_MENUBAR);
        const COLORREF text = *palette.At(COLOR_MENUTEXT);
        const auto sample = SampleMenuBar(r, window, text);
        if (!sample)
        {
            return;
        }
        r.Note(L"%s menu bar background %06lX, item text %06lX (distance %d), underline %06lX", scheme.name, sample->background, sample->closestText,
               sample->textDistance, sample->underline);
        r.Expect(sample->background == bar, L"%s menu-bar background %06lX want MENUBAR %06lX", scheme.name, sample->background, bar);
        r.Expect(sample->itemBackground == bar, L"%s menu item background %06lX want %06lX", scheme.name, sample->itemBackground, bar);
        r.Expect(sample->textDistance <= 30, L"%s menu item text %06lX near MENUTEXT %06lX", scheme.name, sample->closestText, text);
        r.Expect(sample->underline == bar, L"%s line under the menu bar %06lX want %06lX", scheme.name, sample->underline, bar);
        if (stock && palette.isDark)
        {
            r.Expect(RelativeLuminance(stock->background) - RelativeLuminance(sample->background) >= 0.3, L"%s menu bar differs from stock by at least 0.3 (%.3f)",
                     scheme.name, RelativeLuminance(stock->background) - RelativeLuminance(sample->background));
        }
    }

    int MenuBarFollowsPalette(const CheckContext& context)
    {
        Recorder r(L"MenuBarFollowsPalette");
        HWND window = CreateTopLevel(L"MenuBarFollowsPalette", 520, 300);
        HMENU menu = CreateMenu();
        AppendMenuW(menu, MF_STRING, 1, L"&File");
        AppendMenuW(menu, MF_STRING, 2, L"&Edit");
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 3, L"&Tools");
        SetMenu(window, menu);
        DrawMenuBar(window);
        Pump(400);

        const auto stock = SampleMenuBar(r, window, GetSysColor(COLOR_MENUTEXT));
        if (stock)
        {
            r.Note(L"stock menu bar background %06lX (luminance %.3f), underline %06lX", stock->background, RelativeLuminance(stock->background),
                   stock->underline);
            if (RelativeLuminance(stock->background) <= 0.5)
            {
                r.Invalidate(L"stock menu bar is not light (%.3f), so it cannot serve as the comparison", RelativeLuminance(stock->background));
            }
        }

        StartTheming();
        for (const wchar_t* name : {L"Tango Dark", L"One Half Dark", L"One Half Light"})
        {
            r.Expect(Switch(FindScheme(name)), L"switch to %s", name);
            Pump(500);
            ExpectThemedBar(r, window, *FindScheme(name), stock);
        }

        r.Expect(Switch(nullptr), L"switch to None");
        Pump(500);
        if (const auto after = SampleMenuBar(r, window, GetSysColor(COLOR_MENUTEXT)); after && stock)
        {
            r.Expect(after->background == stock->background && after->underline == stock->underline,
                     L"menu bar back to stock after None (%06lX, underline %06lX; was %06lX, %06lX)", after->background, after->underline, stock->background,
                     stock->underline);
        }
        wchar_t evidence[256]{};
        swprintf_s(evidence, L"stock_background=%06lX\nstock_luminance=%.4f\n", stock ? stock->background : 0, stock ? RelativeLuminance(stock->background) : -1.0);
        WriteEvidence(context, L"menu-bar-gate.txt", evidence);
        DestroyWindow(window);
        return r.Result();
    }

    struct GlyphSample
    {
        COLORREF background = 0;
        COLORREF glyph = 0;
        int pixels = 0;
    };

    // Pixels of the item that differ clearly from its most common colour: the glyph.
    std::optional<GlyphSample> SampleGlyph(Recorder& r, HWND window, UINT position)
    {
        RECT item{};
        if (!GetMenuItemRect(window, GetMenu(window), position, &item))
        {
            r.Invalidate(L"bitmap item rectangle unavailable");
            return std::nullopt;
        }
        const auto histogram = CaptureRegion(r, window, ToWindow(window, item));
        if (!histogram)
        {
            return std::nullopt;
        }
        GlyphSample sample;
        sample.background = MostCommon(*histogram);
        int most = 0;
        for (const auto& [c, n] : *histogram)
        {
            if (Distance(c, sample.background) > 90)
            {
                sample.pixels += n;
                if (n > most)
                {
                    most = n;
                    sample.glyph = c;
                }
            }
        }
        return sample;
    }

    struct GlyphItem
    {
        const wchar_t* name;
        UINT position;
        double contrast;
    };

    // Each glyph keeps at least its stock pixel count and stands out from the bar under a dark scheme.
    void ExpectGlyphs(Recorder& r, HWND window, const std::vector<GlyphItem>& items, const wchar_t* schemeName)
    {
        std::vector<std::optional<GlyphSample>> stock;
        for (const GlyphItem& item : items)
        {
            stock.push_back(SampleGlyph(r, window, item.position));
            if (stock.back())
            {
                r.Note(L"stock %s glyph pixels %d (%06lX on %06lX)", item.name, stock.back()->pixels, stock.back()->glyph, stock.back()->background);
                if (stock.back()->pixels < 10)
                {
                    r.Invalidate(L"stock capture shows no %s glyph (%d pixels)", item.name, stock.back()->pixels);
                }
            }
        }
        StartTheming();
        const ColourScheme* scheme = FindScheme(schemeName);
        r.Expect(Switch(scheme), L"switch to %s", schemeName);
        Pump(500);
        const SystemColourPalette palette = BuildPalette(*scheme);
        const COLORREF bar = *palette.At(COLOR_MENUBAR);
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            const auto themed = SampleGlyph(r, window, items[i].position);
            if (!themed || !stock[i])
            {
                continue;
            }
            r.Note(L"themed %s glyph pixels %d (%06lX on %06lX)", items[i].name, themed->pixels, themed->glyph, themed->background);
            r.Expect(themed->pixels >= stock[i]->pixels, L"%s glyph keeps its stock size (%d of %d pixels)", items[i].name, themed->pixels, stock[i]->pixels);
            r.Expect(Distance(themed->background, bar) <= 24, L"%s item sits on the bar colour (%06lX, bar %06lX)", items[i].name, themed->background, bar);
            r.Expect(Contrast(themed->glyph, bar) >= items[i].contrast, L"%s glyph %06lX stands out from the bar %06lX (%.2f:1)", items[i].name, themed->glyph, bar,
                     Contrast(themed->glyph, bar));
        }
        Switch(nullptr);
        Pump(300);
    }

    int MenuBarKeepsBitmapItems(const CheckContext&)
    {
        Recorder r(L"MenuBarKeepsBitmapItems");
        HWND window = CreateTopLevel(L"MenuBarKeepsBitmapItems", 520, 300);
        HMENU menu = CreateMenu();
        AppendMenuW(menu, MF_STRING, 1, L"&File");
        const HBITMAP glyphs[] = {HBMMENU_MBAR_MINIMIZE, HBMMENU_MBAR_MINIMIZE_D, HBMMENU_MBAR_RESTORE, HBMMENU_MBAR_CLOSE, HBMMENU_MBAR_CLOSE_D};
        UINT id = 2;
        for (HBITMAP glyph : glyphs)
        {
            MENUITEMINFOW info{sizeof(info)};
            info.fMask = MIIM_BITMAP | MIIM_ID;
            info.wID = id++;
            info.hbmpItem = glyph;
            InsertMenuItemW(menu, static_cast<UINT>(GetMenuItemCount(menu)), TRUE, &info);
        }
        SetMenu(window, menu);
        DrawMenuBar(window);
        Pump(400);
        // Disabled glyphs are deliberately dim, so they only need to be distinguishable.
        ExpectGlyphs(r, window,
                     {{L"minimize", 1, 3.0}, {L"inactive minimize", 2, 1.5}, {L"restore", 3, 3.0}, {L"close", 4, 3.0}, {L"inactive close", 5, 1.5}},
                     L"Tango Dark");
        DestroyWindow(window);
        return r.Result();
    }

    int MenuBarKeepsMdiGlyphs(const CheckContext&)
    {
        Recorder r(L"MenuBarKeepsMdiGlyphs");
        HMENU menu = CreateMenu();
        AppendMenuW(menu, MF_STRING, 1, L"&File");
        const MdiFrame mdi = CreateMdiFrame(L"MenuBarKeepsMdiGlyphs", menu, 560, 360);
        HWND frame = mdi.frame;
        HWND child = mdi.client != nullptr ? CreateMdiChild(mdi, L"Child", WS_MAXIMIZE, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT) : nullptr;
        DrawMenuBar(frame);
        Pump(400);
        const int count = GetMenuItemCount(GetMenu(frame));
        if (child == nullptr || count < 5)
        {
            r.Invalidate(L"maximised MDI child menu not built (%d items)", count);
            if (frame != nullptr)
            {
                DestroyWindow(frame);
            }
            return r.Result();
        }
        for (int i = 0; i < count; ++i)
        {
            MENUITEMINFOW info{sizeof(info)};
            info.fMask = MIIM_FTYPE | MIIM_BITMAP | MIIM_DATA;
            GetMenuItemInfoW(GetMenu(frame), static_cast<UINT>(i), TRUE, &info);
            r.Note(L"item %d type %08X bitmap %p data %p", i, info.fType, info.hbmpItem, reinterpret_cast<void*>(info.dwItemData));
        }
        const UINT last = static_cast<UINT>(count - 1);
        ExpectGlyphs(r, frame, {{L"system icon", 0, 3.0}, {L"minimize", last - 2, 3.0}, {L"restore", last - 1, 3.0}, {L"close", last, 3.0}}, L"Tango Dark");
        DestroyWindow(frame);
        return r.Result();
    }

    const CheckRegistration registrations[] = {
        CheckRegistration{{L"MenuBar", L"MenuBarKeepsBitmapItems", L"-", Host::Self, false, MenuBarKeepsBitmapItems}},
        CheckRegistration{{L"MenuBar", L"MenuBarKeepsMdiGlyphs", L"-", Host::Self, false, MenuBarKeepsMdiGlyphs}},
        CheckRegistration{{L"MenuBar", L"MenuBarFollowsPalette", L"-", Host::Self, false, MenuBarFollowsPalette}},
    };
}
