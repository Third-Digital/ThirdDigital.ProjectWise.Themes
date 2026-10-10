#include "SystemColourMapping.h"

#include <cmath>

namespace
{
    enum class Source
    {
        Background,
        Foreground,
        Blend,
        Hotlight,
    };

    struct Rule
    {
        int index;
        Source source;
        double weight;
    };

    // Selection is a subtle fill with the text left in the foreground colour; 0.20 keeps the foreground at
    // 3:1 or better on the fill for all nine schemes (lowest: Solarized Light, 3.26:1).
    constexpr double kSelectionFill = 0.20;

    constexpr Rule kRules[] = {
        {COLOR_WINDOW, Source::Background, 0.0},
        {COLOR_3DHIGHLIGHT, Source::Background, 0.0},
        {COLOR_WINDOWTEXT, Source::Foreground, 0.0},
        {COLOR_BTNTEXT, Source::Foreground, 0.0},
        {COLOR_MENUTEXT, Source::Foreground, 0.0},
        {COLOR_CAPTIONTEXT, Source::Foreground, 0.0},
        {COLOR_INFOTEXT, Source::Foreground, 0.0},
        {COLOR_HIGHLIGHTTEXT, Source::Foreground, 0.0},
        {COLOR_3DFACE, Source::Blend, 0.08},
        {COLOR_MENU, Source::Blend, 0.08},
        {COLOR_MENUBAR, Source::Blend, 0.08},
        {COLOR_SCROLLBAR, Source::Blend, 0.08},
        {COLOR_APPWORKSPACE, Source::Blend, 0.08},
        {COLOR_ACTIVECAPTION, Source::Blend, 0.08},
        {COLOR_INACTIVECAPTION, Source::Blend, 0.08},
        {COLOR_GRADIENTACTIVECAPTION, Source::Blend, 0.08},
        {COLOR_GRADIENTINACTIVECAPTION, Source::Blend, 0.08},
        {COLOR_HIGHLIGHT, Source::Blend, kSelectionFill},
        {COLOR_MENUHILIGHT, Source::Blend, kSelectionFill},
        {COLOR_3DLIGHT, Source::Blend, 0.04},
        {COLOR_INFOBK, Source::Blend, 0.12},
        {COLOR_3DSHADOW, Source::Blend, 0.35},
        {COLOR_ACTIVEBORDER, Source::Blend, 0.35},
        {COLOR_INACTIVEBORDER, Source::Blend, 0.35},
        {COLOR_WINDOWFRAME, Source::Blend, 0.35},
        {COLOR_3DDKSHADOW, Source::Blend, 0.55},
        {COLOR_GRAYTEXT, Source::Blend, 0.5},
        {COLOR_INACTIVECAPTIONTEXT, Source::Blend, 0.5},
        {COLOR_HOTLIGHT, Source::Hotlight, 0.0},
    };

    COLORREF Hotlight(const ColourScheme& scheme) noexcept
    {
        const Ansi candidates[] = {Ansi::Blue, Ansi::BrightBlue, Ansi::Cyan, Ansi::BrightCyan};
        COLORREF best = scheme.Colour(candidates[0]);
        double bestContrast = Contrast(best, scheme.background);
        for (Ansi candidate : candidates)
        {
            const COLORREF colour = scheme.Colour(candidate);
            const double contrast = Contrast(colour, scheme.background);
            if (contrast > bestContrast)
            {
                best = colour;
                bestContrast = contrast;
            }
        }
        return best;
    }

    double Linear(unsigned channel) noexcept
    {
        const double c = channel / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    }
}

SystemColourPalette BuildPalette(const ColourScheme& scheme) noexcept
{
    SystemColourPalette palette;
    for (const Rule& rule : kRules)
    {
        COLORREF colour = 0;
        switch (rule.source)
        {
        case Source::Background:
            colour = scheme.background;
            break;
        case Source::Foreground:
            colour = scheme.foreground;
            break;
        case Source::Blend:
            colour = Mix(scheme.background, scheme.foreground, rule.weight);
            break;
        case Source::Hotlight:
            colour = Hotlight(scheme);
            break;
        }
        palette.colours[static_cast<std::size_t>(rule.index)] = colour;
    }
    palette.isDark = RelativeLuminance(scheme.background) < RelativeLuminance(scheme.foreground);
    return palette;
}

COLORREF Mix(COLORREF from, COLORREF to, double weight) noexcept
{
    auto channel = [&](unsigned shift) noexcept {
        const double a = static_cast<double>((from >> shift) & 0xFF);
        const double b = static_cast<double>((to >> shift) & 0xFF);
        return static_cast<COLORREF>(std::lround(a + (b - a) * weight)) << shift;
    };
    return channel(0) | channel(8) | channel(16);
}

double RelativeLuminance(COLORREF colour) noexcept
{
    return 0.2126 * Linear(GetRValue(colour)) + 0.7152 * Linear(GetGValue(colour)) + 0.0722 * Linear(GetBValue(colour));
}

double Contrast(COLORREF first, COLORREF second) noexcept
{
    const double a = RelativeLuminance(first);
    const double b = RelativeLuminance(second);
    return a > b ? (a + 0.05) / (b + 0.05) : (b + 0.05) / (a + 0.05);
}
