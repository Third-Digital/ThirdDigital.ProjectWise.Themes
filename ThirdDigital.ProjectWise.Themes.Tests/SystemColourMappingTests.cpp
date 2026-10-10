#include "Domain/SystemColourMapping.h"

#include <CppUnitTest.h>

#include <cmath>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    unsigned long U(COLORREF c)
    {
        return static_cast<unsigned long>(c);
    }

    unsigned long Expected(COLORREF from, COLORREF to, double weight)
    {
        auto channel = [&](int shift) {
            const double a = static_cast<double>((from >> shift) & 0xFF);
            const double b = static_cast<double>((to >> shift) & 0xFF);
            return static_cast<unsigned long>(std::floor(a + (b - a) * weight + 0.5)) << shift;
        };
        return channel(0) | channel(8) | channel(16);
    }

    COLORREF Required(const SystemColourPalette& palette, int index, const wchar_t* scheme)
    {
        const auto value = palette.At(index);
        Assert::IsTrue(value.has_value(), (std::wstring(scheme) + L" index " + std::to_wstring(index)).c_str());
        return *value;
    }

    struct TextPair
    {
        const wchar_t* label;
        int text;
        int background;
    };

    const TextPair kReadablePairs[] = {
        {L"body", COLOR_WINDOWTEXT, COLOR_WINDOW},
        {L"button", COLOR_BTNTEXT, COLOR_3DFACE},
        {L"menu", COLOR_MENUTEXT, COLOR_MENU},
        {L"active caption", COLOR_CAPTIONTEXT, COLOR_ACTIVECAPTION},
        {L"info", COLOR_INFOTEXT, COLOR_INFOBK},
        {L"selection", COLOR_HIGHLIGHTTEXT, COLOR_HIGHLIGHT},
    };
}

TEST_CLASS(SystemColourMappingTests)
{
public:
    TEST_METHOD(EveryMappedIndexMatchesRule)
    {
        const int background[] = {COLOR_WINDOW, COLOR_3DHIGHLIGHT};
        const int foreground[] = {COLOR_WINDOWTEXT, COLOR_BTNTEXT, COLOR_MENUTEXT, COLOR_CAPTIONTEXT, COLOR_INFOTEXT, COLOR_HIGHLIGHTTEXT};
        const int selection[] = {COLOR_HIGHLIGHT, COLOR_MENUHILIGHT};
        const int mix08[] = {COLOR_3DFACE, COLOR_MENU, COLOR_MENUBAR, COLOR_SCROLLBAR, COLOR_APPWORKSPACE, COLOR_ACTIVECAPTION,
                             COLOR_INACTIVECAPTION, COLOR_GRADIENTACTIVECAPTION, COLOR_GRADIENTINACTIVECAPTION};
        const int mix35[] = {COLOR_3DSHADOW, COLOR_ACTIVEBORDER, COLOR_INACTIVEBORDER, COLOR_WINDOWFRAME};
        const int mix50[] = {COLOR_GRAYTEXT, COLOR_INACTIVECAPTIONTEXT};

        for (const ColourScheme& scheme : Schemes())
        {
            const SystemColourPalette palette = BuildPalette(scheme);
            const COLORREF bg = scheme.background;
            const COLORREF fg = scheme.foreground;
            for (int i : background) Assert::AreEqual(U(bg), U(Required(palette, i, scheme.name)), scheme.name);
            for (int i : foreground) Assert::AreEqual(U(fg), U(Required(palette, i, scheme.name)), scheme.name);
            for (int i : mix08) Assert::AreEqual(Expected(bg, fg, 0.08), U(Required(palette, i, scheme.name)), scheme.name);
            for (int i : mix35) Assert::AreEqual(Expected(bg, fg, 0.35), U(Required(palette, i, scheme.name)), scheme.name);
            for (int i : mix50) Assert::AreEqual(Expected(bg, fg, 0.5), U(Required(palette, i, scheme.name)), scheme.name);
            for (int i : selection) Assert::AreEqual(Expected(bg, fg, 0.20), U(Required(palette, i, scheme.name)), scheme.name);
            Assert::AreEqual(Expected(bg, fg, 0.04), U(Required(palette, COLOR_3DLIGHT, scheme.name)), scheme.name);
            Assert::AreEqual(Expected(bg, fg, 0.12), U(Required(palette, COLOR_INFOBK, scheme.name)), scheme.name);
            Assert::AreEqual(Expected(bg, fg, 0.55), U(Required(palette, COLOR_3DDKSHADOW, scheme.name)), scheme.name);
            Required(palette, COLOR_HOTLIGHT, scheme.name);
        }
    }

    TEST_METHOD(UnmappedIndicesStayEmpty)
    {
        for (const ColourScheme& scheme : Schemes())
        {
            const SystemColourPalette palette = BuildPalette(scheme);
            for (int i : {COLOR_DESKTOP, 25, -1, -100, COLOR_MENUBAR + 1, 1000})
            {
                Assert::IsFalse(palette.At(i).has_value(), (std::wstring(scheme.name) + L" index " + std::to_wstring(i)).c_str());
            }
        }
        Assert::AreNotEqual(std::size_t{0}, Schemes().size());
    }

    TEST_METHOD(CampbellMixIs1B)
    {
        const ColourScheme* campbell = FindScheme(L"Campbell");
        Assert::IsNotNull(campbell);
        Assert::AreEqual(U(RGB(0x1B, 0x1B, 0x1B)), U(Mix(campbell->background, campbell->foreground, 0.08)));
        Assert::AreEqual(U(RGB(0x1B, 0x1B, 0x1B)), U(Required(BuildPalette(*campbell), COLOR_3DFACE, L"Campbell")));
        Assert::AreEqual(U(RGB(0x80, 0x80, 0x80)), U(Mix(RGB(0, 0, 0), RGB(0xFF, 0xFF, 0xFF), 0.5)));
    }

    TEST_METHOD(DarkClassificationIsSixAndThree)
    {
        const wchar_t* dark[] = {L"Campbell", L"Campbell Powershell", L"Vintage", L"One Half Dark", L"Solarized Dark", L"Tango Dark"};
        const wchar_t* light[] = {L"One Half Light", L"Solarized Light", L"Tango Light"};
        for (const wchar_t* name : dark)
        {
            Assert::IsTrue(BuildPalette(*FindScheme(name)).isDark, name);
        }
        for (const wchar_t* name : light)
        {
            Assert::IsFalse(BuildPalette(*FindScheme(name)).isDark, name);
        }
    }

    TEST_METHOD(HotlightUsesExpectedCandidate)
    {
        for (const ColourScheme& scheme : Schemes())
        {
            const SystemColourPalette palette = BuildPalette(scheme);
            const bool blue = !palette.isDark || std::wstring(scheme.name) == L"One Half Dark";
            const COLORREF expected = blue ? scheme.Colour(Ansi::Blue) : scheme.Colour(Ansi::BrightCyan);
            Assert::AreEqual(U(expected), U(Required(palette, COLOR_HOTLIGHT, scheme.name)), scheme.name);
        }
    }

    TEST_METHOD(ReadableTextPairsMeetThreeToOne)
    {
        for (const ColourScheme& scheme : Schemes())
        {
            const SystemColourPalette palette = BuildPalette(scheme);
            for (const TextPair& pair : kReadablePairs)
            {
                const double ratio = Contrast(Required(palette, pair.text, scheme.name), Required(palette, pair.background, scheme.name));
                Assert::IsTrue(ratio >= 3.0, (std::wstring(scheme.name) + L" " + pair.label + L" " + std::to_wstring(ratio)).c_str());
            }
        }
    }

    TEST_METHOD(LowestTextPairIsSolarizedLightSelection)
    {
        double lowest = 1e9;
        std::wstring where;
        for (const ColourScheme& scheme : Schemes())
        {
            const SystemColourPalette palette = BuildPalette(scheme);
            for (const TextPair& pair : kReadablePairs)
            {
                const double ratio = Contrast(Required(palette, pair.text, scheme.name), Required(palette, pair.background, scheme.name));
                if (ratio < lowest)
                {
                    lowest = ratio;
                    where = std::wstring(scheme.name) + L"/" + pair.label;
                }
            }
        }
        Assert::AreEqual(std::wstring(L"Solarized Light/selection"), where);
        Assert::AreEqual(3.26, std::round(lowest * 100.0) / 100.0, 1e-9);
        Assert::AreEqual(21.0, Contrast(RGB(0, 0, 0), RGB(0xFF, 0xFF, 0xFF)), 1e-9);
    }
};
