#include "Fixtures.h"

#include "Features/ThemeSwitch.h"

#include <vector>

using namespace Harness;

namespace
{
    struct Caption
    {
        RECT window{};
        RECT client{};
        RECT bar{};
        RECT leftFrame{};
        RECT buttons[3]{};
        bool active = false;
    };

    constexpr int kButtonIndices[3] = {2, 3, 5};
    constexpr const wchar_t* kButtonNames[3] = {L"minimize", L"maximize", L"close"};

    // Rectangles in the frame window's coordinates, as Windows itself reports them.
    std::optional<Caption> Measure(Recorder& r, HWND frame, HWND child)
    {
        TITLEBARINFOEX info{sizeof(info)};
        WINDOWINFO window{sizeof(window)};
        SendMessageW(child, WM_GETTITLEBARINFOEX, 0, reinterpret_cast<LPARAM>(&info));
        if (info.rcTitleBar.right <= info.rcTitleBar.left)
        {
            r.Invalidate(L"title bar information unavailable");
            return std::nullopt;
        }
        if (!GetWindowInfo(child, &window))
        {
            r.Invalidate(L"window information unavailable");
            return std::nullopt;
        }
        Caption c;
        c.window = ToWindow(frame, window.rcWindow);
        c.client = ToWindow(frame, window.rcClient);
        c.active = (window.dwWindowStatus & WS_ACTIVECAPTION) != 0;
        for (int i = 0; i < 3; ++i)
        {
            if ((info.rgstate[kButtonIndices[i]] & STATE_SYSTEM_INVISIBLE) != 0)
            {
                r.Invalidate(L"%s button not shown", kButtonNames[i]);
                return std::nullopt;
            }
            c.buttons[i] = ToWindow(frame, info.rgrect[kButtonIndices[i]]);
        }
        RECT bar = ToWindow(frame, info.rcTitleBar);
        bar.left += GetSystemMetrics(SM_CXSMICON) + 6;
        bar.right = c.buttons[0].left - 2;
        c.bar = bar;
        const int frameWidth = window.rcClient.left - window.rcWindow.left;
        r.Note(L"window %ld,%ld-%ld,%ld client %ld,%ld-%ld,%ld title bar %ld,%ld-%ld,%ld", c.window.left, c.window.top, c.window.right, c.window.bottom, c.client.left,
               c.client.top, c.client.right, c.client.bottom, bar.left, bar.top, bar.right, bar.bottom);
        for (int i = 0; i < 3; ++i)
        {
            r.Note(L"%s button %ld,%ld-%ld,%ld state %08lX", kButtonNames[i], c.buttons[i].left, c.buttons[i].top, c.buttons[i].right, c.buttons[i].bottom,
                   info.rgstate[kButtonIndices[i]]);
        }
        c.leftFrame = {c.window.left, c.window.top + (c.window.bottom - c.window.top) / 2 - 10, c.window.left + frameWidth,
                       c.window.top + (c.window.bottom - c.window.top) / 2 + 10};
        if (frameWidth <= 0 || bar.right - bar.left < 40)
        {
            r.Invalidate(L"caption geometry unusable (frame %d, bar %ld wide)", frameWidth, bar.right - bar.left);
            return std::nullopt;
        }
        return c;
    }

    int NearCount(const std::map<COLORREF, int>& histogram, COLORREF colour, int distance)
    {
        int count = 0;
        for (const auto& [c, n] : histogram)
        {
            if (Distance(c, colour) <= distance)
            {
                count += n;
            }
        }
        return count;
    }

    struct Glyph
    {
        COLORREF background = 0;
        COLORREF colour = 0;
        int pixels = 0;
    };

    std::optional<Glyph> SampleButton(Recorder& r, HWND frame, const RECT& button)
    {
        const auto histogram = CaptureRegion(r, frame, button);
        if (!histogram)
        {
            return std::nullopt;
        }
        Glyph g;
        g.background = MostCommon(*histogram);
        int most = 0;
        for (const auto& [c, n] : *histogram)
        {
            if (Distance(c, g.background) > 90)
            {
                g.pixels += n;
                if (n > most)
                {
                    most = n;
                    g.colour = c;
                }
            }
        }
        return g;
    }

    std::optional<COLORREF> CaptionFill(Recorder& r, HWND frame, const Caption& c)
    {
        const auto histogram = CaptureRegion(r, frame, c.bar);
        return histogram ? std::optional<COLORREF>(MostCommon(*histogram)) : std::nullopt;
    }

    void ExpectThemed(Recorder& r, HWND frame, const Caption& c, const SystemColourPalette& palette, const wchar_t* which)
    {
        const COLORREF fill = *palette.At(c.active ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION);
        const COLORREF text = *palette.At(c.active ? COLOR_CAPTIONTEXT : COLOR_INACTIVECAPTIONTEXT);
        const COLORREF border = *palette.At(c.active ? COLOR_ACTIVEBORDER : COLOR_INACTIVEBORDER);
        const wchar_t* state = c.active ? L"active" : L"inactive";
        if (const auto bar = CaptureRegion(r, frame, c.bar))
        {
            const COLORREF seen = MostCommon(*bar);
            r.Expect(seen == fill, L"%s (%s) caption fill %06lX want %06lX", which, state, seen, fill);
            const int textPixels = NearCount(*bar, text, 30);
            r.Expect(textPixels >= 15, L"%s (%s) caption text near %06lX (%d pixels)", which, state, text, textPixels);
        }
        if (const auto edge = CaptureRegion(r, frame, c.leftFrame))
        {
            const COLORREF seen = MostCommon(*edge);
            r.Expect(seen == border, L"%s (%s) frame %06lX want %06lX", which, state, seen, border);
        }
        for (int i = 0; i < 3; ++i)
        {
            if (const auto g = SampleButton(r, frame, c.buttons[i]))
            {
                r.Expect(g->background == fill, L"%s (%s) %s button sits on the caption (%06lX)", which, state, kButtonNames[i], g->background);
                r.Expect(g->pixels >= 10 && Contrast(g->colour, fill) >= 3.0, L"%s (%s) %s glyph %06lX visible on %06lX (%d pixels, %.2f:1)", which, state,
                         kButtonNames[i], g->colour, fill, g->pixels, Contrast(g->colour, fill));
            }
        }
    }

    struct HitPoints
    {
        LRESULT caption = 0;
        LRESULT close = 0;
        LRESULT minimize = 0;
        LRESULT left = 0;
        LRESULT corner = 0;
    };

    HitPoints HitTest(HWND child)
    {
        TITLEBARINFOEX info{sizeof(info)};
        SendMessageW(child, WM_GETTITLEBARINFOEX, 0, reinterpret_cast<LPARAM>(&info));
        RECT window{};
        GetWindowRect(child, &window);
        const auto at = [child](LONG x, LONG y) { return SendMessageW(child, WM_NCHITTEST, 0, MAKELPARAM(x, y)); };
        const RECT& close = info.rgrect[5];
        HitPoints h;
        h.caption = at(info.rgrect[2].left - 20, (info.rcTitleBar.top + info.rcTitleBar.bottom) / 2);
        h.close = at((close.left + close.right) / 2, (close.top + close.bottom) / 2);
        const RECT& minimize = info.rgrect[2];
        h.minimize = at((minimize.left + minimize.right) / 2, (minimize.top + minimize.bottom) / 2);
        h.left = at(window.left + 1, (window.top + window.bottom) / 2);
        h.corner = at(window.right - 2, window.bottom - 2);
        return h;
    }

    int MdiChildCaptionFollowsPalette(const CheckContext& context)
    {
        Recorder r(L"MdiChildCaptionFollowsPalette");
        const MdiFrame mdi = CreateMdiFrame(L"MdiChildCaptionFollowsPalette", nullptr, 760, 520);
        const DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
        HWND first = mdi.client != nullptr ? CreateMdiChild(mdi, L"WWWWWWWWWWWWWWWW", style, 10, 10, 340, 220) : nullptr;
        HWND second = mdi.client != nullptr ? CreateMdiChild(mdi, L"WWWWWWWWWWWWWWWW", style, 370, 10, 340, 220) : nullptr;
        if (first == nullptr || second == nullptr)
        {
            r.Invalidate(L"MDI children not created");
            if (mdi.frame != nullptr)
            {
                DestroyWindow(mdi.frame);
            }
            return r.Result();
        }
        SetActiveWindow(mdi.frame);
        SendMessageW(mdi.client, WM_MDIACTIVATE, reinterpret_cast<WPARAM>(first), 0);
        Pump(400);
        HWND frame = mdi.frame;
        const auto stockFirst = Measure(r, frame, first);
        const auto stockSecond = Measure(r, frame, second);
        if (!stockFirst || !stockSecond)
        {
            DestroyWindow(frame);
            return r.Result();
        }
        if (stockFirst->active == stockSecond->active)
        {
            r.Invalidate(L"one MDI child must draw active and one inactive (both %s)", stockFirst->active ? L"active" : L"inactive");
            DestroyWindow(frame);
            return r.Result();
        }
        WriteCaptureBitmap(context, L"mdi-child-stock.bmp", frame);
        const auto stockFillFirst = CaptionFill(r, frame, *stockFirst);
        const auto stockFillSecond = CaptionFill(r, frame, *stockSecond);
        const HitPoints stockHits = HitTest(first);
        if (stockFillFirst && stockFillSecond)
        {
            r.Note(L"stock caption fills %06lX (%s), %06lX (%s)", *stockFillFirst, stockFirst->active ? L"active" : L"inactive", *stockFillSecond,
                   stockSecond->active ? L"active" : L"inactive");
        }

        StartTheming();
        const ColourScheme* scheme = FindScheme(L"One Half Dark");
        r.Expect(Switch(scheme), L"switch to One Half Dark");
        Pump(500);
        const SystemColourPalette palette = BuildPalette(*scheme);
        WriteCaptureBitmap(context, L"mdi-child-themed.bmp", frame);
        const auto themedFirst = Measure(r, frame, first);
        const auto themedSecond = Measure(r, frame, second);
        if (themedFirst && themedSecond)
        {
            r.Expect(EqualRect(&themedFirst->window, &stockFirst->window) && EqualRect(&themedFirst->client, &stockFirst->client),
                     L"window and client rectangles unchanged");
            ExpectThemed(r, frame, *themedFirst, palette, L"first child");
            ExpectThemed(r, frame, *themedSecond, palette, L"second child");
        }
        const HitPoints themedHits = HitTest(first);
        r.Expect(themedHits.caption == stockHits.caption && themedHits.caption == HTCAPTION, L"caption hit-test %Id (stock %Id)", themedHits.caption, stockHits.caption);
        r.Expect(themedHits.close == stockHits.close && themedHits.close == HTCLOSE, L"close button hit-test %Id (stock %Id)", themedHits.close, stockHits.close);
        r.Expect(themedHits.minimize == stockHits.minimize && themedHits.minimize == HTMINBUTTON, L"minimize button hit-test %Id (stock %Id)", themedHits.minimize,
                 stockHits.minimize);
        r.Expect(themedHits.left == stockHits.left && themedHits.left == HTLEFT, L"left edge hit-test %Id (stock %Id)", themedHits.left, stockHits.left);
        r.Expect(themedHits.corner == stockHits.corner && themedHits.corner == HTBOTTOMRIGHT, L"corner hit-test %Id (stock %Id)", themedHits.corner,
                 stockHits.corner);

        SendMessageW(mdi.client, WM_MDIACTIVATE, reinterpret_cast<WPARAM>(second), 0);
        Pump(400);
        const auto swappedFirst = Measure(r, frame, first);
        const auto swappedSecond = Measure(r, frame, second);
        if (swappedFirst && swappedSecond)
        {
            r.Expect(swappedFirst->active != themedFirst.value_or(Caption{}).active, L"activation moved to the other child");
            ExpectThemed(r, frame, *swappedFirst, palette, L"first child after activation change");
            ExpectThemed(r, frame, *swappedSecond, palette, L"second child after activation change");
        }

        r.Expect(Switch(nullptr), L"switch to None");
        SendMessageW(mdi.client, WM_MDIACTIVATE, reinterpret_cast<WPARAM>(first), 0);
        Pump(500);
        WriteCaptureBitmap(context, L"mdi-child-none.bmp", frame);
        if (const auto after = Measure(r, frame, first); after && stockFillFirst)
        {
            const auto fill = CaptionFill(r, frame, *after);
            r.Expect(fill && *fill == *stockFillFirst, L"first child caption stock after None (%06lX, was %06lX)", fill.value_or(0), *stockFillFirst);
        }
        if (const auto after = Measure(r, frame, second); after && stockFillSecond)
        {
            const auto fill = CaptionFill(r, frame, *after);
            r.Expect(fill && *fill == *stockFillSecond, L"second child caption stock after None (%06lX, was %06lX)", fill.value_or(0), *stockFillSecond);
        }
        DestroyWindow(frame);
        return r.Result();
    }

    const CheckRegistration registrations[] = {
        CheckRegistration{{L"ChildCaption", L"MdiChildCaptionFollowsPalette", L"-", Host::Self, false, MdiChildCaptionFollowsPalette}},
    };
}
