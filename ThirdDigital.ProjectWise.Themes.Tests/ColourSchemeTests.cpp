#include "Domain/ColourScheme.h"

#include <CppUnitTest.h>

#include <set>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    const ColourScheme& Require(const wchar_t* name)
    {
        const ColourScheme* scheme = FindScheme(name);
        Assert::IsNotNull(scheme, name);
        return *scheme;
    }

    void AreEqualColour(COLORREF expected, COLORREF actual, const wchar_t* what)
    {
        Assert::AreEqual(static_cast<unsigned long>(expected), static_cast<unsigned long>(actual), what);
    }
}

TEST_CLASS(ColourSchemeTests)
{
public:
    TEST_METHOD(NineUniqueExactNames)
    {
        const wchar_t* expected[] = {
            L"Campbell", L"Campbell Powershell", L"Vintage", L"One Half Dark", L"One Half Light",
            L"Solarized Dark", L"Solarized Light", L"Tango Dark", L"Tango Light",
        };
        const auto schemes = Schemes();
        Assert::AreEqual(std::size(expected), schemes.size());
        std::set<std::wstring> unique;
        for (std::size_t i = 0; i < schemes.size(); ++i)
        {
            Assert::AreEqual(std::wstring(expected[i]), std::wstring(schemes[i].name));
            unique.insert(schemes[i].name);
        }
        Assert::AreEqual(std::size(expected), unique.size());
    }

    TEST_METHOD(RepresentativeColoursMatchSource)
    {
        struct Expectation
        {
            const wchar_t* name;
            COLORREF background;
            COLORREF foreground;
        };
        const Expectation pairs[] = {
            {L"Campbell", RGB(0x0C, 0x0C, 0x0C), RGB(0xCC, 0xCC, 0xCC)},
            {L"Campbell Powershell", RGB(0x01, 0x24, 0x56), RGB(0xCC, 0xCC, 0xCC)},
            {L"Vintage", RGB(0x00, 0x00, 0x00), RGB(0xC0, 0xC0, 0xC0)},
            {L"One Half Dark", RGB(0x28, 0x2C, 0x34), RGB(0xDC, 0xDF, 0xE4)},
            {L"One Half Light", RGB(0xFA, 0xFA, 0xFA), RGB(0x38, 0x3A, 0x42)},
            {L"Solarized Dark", RGB(0x00, 0x2B, 0x36), RGB(0x83, 0x94, 0x96)},
            {L"Solarized Light", RGB(0xFD, 0xF6, 0xE3), RGB(0x65, 0x7B, 0x83)},
            {L"Tango Dark", RGB(0x00, 0x00, 0x00), RGB(0xD3, 0xD7, 0xCF)},
            {L"Tango Light", RGB(0xFF, 0xFF, 0xFF), RGB(0x55, 0x57, 0x53)},
        };
        for (const Expectation& e : pairs)
        {
            const ColourScheme& scheme = Require(e.name);
            AreEqualColour(e.background, scheme.background, e.name);
            AreEqualColour(e.foreground, scheme.foreground, e.name);
        }

        AreEqualColour(RGB(0xFF, 0xFF, 0xFF), Require(L"Campbell").cursor, L"Campbell cursor");
        AreEqualColour(RGB(0x4F, 0x52, 0x5D), Require(L"One Half Light").cursor, L"One Half Light cursor");
        AreEqualColour(RGB(0x00, 0x2B, 0x36), Require(L"Solarized Light").cursor, L"Solarized Light cursor");
        AreEqualColour(RGB(0x00, 0x37, 0xDA), Require(L"Campbell").Colour(Ansi::Blue), L"Campbell blue");
        AreEqualColour(RGB(0x61, 0xD6, 0xD6), Require(L"Campbell Powershell").Colour(Ansi::BrightCyan), L"Campbell Powershell brightCyan");
        AreEqualColour(RGB(0x80, 0x80, 0x00), Require(L"Vintage").Colour(Ansi::Yellow), L"Vintage yellow");
        AreEqualColour(RGB(0x5A, 0x63, 0x74), Require(L"One Half Dark").Colour(Ansi::BrightBlack), L"One Half Dark brightBlack");
        AreEqualColour(RGB(0xFF, 0xFF, 0xFF), Require(L"One Half Light").Colour(Ansi::BrightWhite), L"One Half Light brightWhite");
        AreEqualColour(RGB(0x93, 0xA1, 0xA1), Require(L"Solarized Dark").Colour(Ansi::BrightCyan), L"Solarized Dark brightCyan");
        AreEqualColour(RGB(0x6C, 0x71, 0xC4), Require(L"Solarized Light").Colour(Ansi::BrightPurple), L"Solarized Light brightPurple");
        AreEqualColour(RGB(0x00, 0x00, 0x00), Require(L"Tango Dark").Colour(Ansi::Black), L"Tango Dark black");
        AreEqualColour(RGB(0x34, 0x65, 0xA4), Require(L"Tango Light").Colour(Ansi::Blue), L"Tango Light blue");
        AreEqualColour(RGB(0xEE, 0xEE, 0xEC), Require(L"Tango Light").Colour(Ansi::BrightWhite), L"Tango Light brightWhite");
    }

    TEST_METHOD(FindSchemeIsExact)
    {
        const ColourScheme* found = FindScheme(L"One Half Dark");
        Assert::IsNotNull(found);
        Assert::AreEqual(std::wstring(L"One Half Dark"), std::wstring(found->name));
        Assert::IsNull(FindScheme(L"one half dark"));
        Assert::IsNull(FindScheme(L"CAMPBELL"));
        Assert::IsNull(FindScheme(L""));
        Assert::IsNull(FindScheme(L"Campbell "));
        Assert::IsNull(FindScheme(L"Dimidium"));
        Assert::IsNull(FindScheme(L"None"));
    }
};
