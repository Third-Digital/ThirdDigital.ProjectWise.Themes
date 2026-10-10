#include "ColourScheme.h"

namespace
{
    constexpr COLORREF Hex(unsigned long rgb) noexcept
    {
        return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    }

    // Transcribed from microsoft/terminal src/cascadia/TerminalSettingsModel/defaults.json at
    // commit 1282d56a33bb15206fcb3a09d3bea0a33036f2cd. ANSI order: black, red, green, yellow, blue,
    // purple, cyan, white, then the bright variants in the same order.
    constexpr ColourScheme kSchemes[] = {
        {L"Campbell", Hex(0x0C0C0C), Hex(0xCCCCCC), Hex(0xFFFFFF),
         {Hex(0x0C0C0C), Hex(0xC50F1F), Hex(0x13A10E), Hex(0xC19C00), Hex(0x0037DA), Hex(0x881798), Hex(0x3A96DD), Hex(0xCCCCCC),
          Hex(0x767676), Hex(0xE74856), Hex(0x16C60C), Hex(0xF9F1A5), Hex(0x3B78FF), Hex(0xB4009E), Hex(0x61D6D6), Hex(0xF2F2F2)}},
        {L"Campbell Powershell", Hex(0x012456), Hex(0xCCCCCC), Hex(0xFFFFFF),
         {Hex(0x0C0C0C), Hex(0xC50F1F), Hex(0x13A10E), Hex(0xC19C00), Hex(0x0037DA), Hex(0x881798), Hex(0x3A96DD), Hex(0xCCCCCC),
          Hex(0x767676), Hex(0xE74856), Hex(0x16C60C), Hex(0xF9F1A5), Hex(0x3B78FF), Hex(0xB4009E), Hex(0x61D6D6), Hex(0xF2F2F2)}},
        {L"Vintage", Hex(0x000000), Hex(0xC0C0C0), Hex(0xFFFFFF),
         {Hex(0x000000), Hex(0x800000), Hex(0x008000), Hex(0x808000), Hex(0x000080), Hex(0x800080), Hex(0x008080), Hex(0xC0C0C0),
          Hex(0x808080), Hex(0xFF0000), Hex(0x00FF00), Hex(0xFFFF00), Hex(0x0000FF), Hex(0xFF00FF), Hex(0x00FFFF), Hex(0xFFFFFF)}},
        {L"One Half Dark", Hex(0x282C34), Hex(0xDCDFE4), Hex(0xFFFFFF),
         {Hex(0x282C34), Hex(0xE06C75), Hex(0x98C379), Hex(0xE5C07B), Hex(0x61AFEF), Hex(0xC678DD), Hex(0x56B6C2), Hex(0xDCDFE4),
          Hex(0x5A6374), Hex(0xE06C75), Hex(0x98C379), Hex(0xE5C07B), Hex(0x61AFEF), Hex(0xC678DD), Hex(0x56B6C2), Hex(0xDCDFE4)}},
        {L"One Half Light", Hex(0xFAFAFA), Hex(0x383A42), Hex(0x4F525D),
         {Hex(0x383A42), Hex(0xE45649), Hex(0x50A14F), Hex(0xC18301), Hex(0x0184BC), Hex(0xA626A4), Hex(0x0997B3), Hex(0xFAFAFA),
          Hex(0x4F525D), Hex(0xDF6C75), Hex(0x98C379), Hex(0xE4C07A), Hex(0x61AFEF), Hex(0xC577DD), Hex(0x56B5C1), Hex(0xFFFFFF)}},
        {L"Solarized Dark", Hex(0x002B36), Hex(0x839496), Hex(0xFFFFFF),
         {Hex(0x002B36), Hex(0xDC322F), Hex(0x859900), Hex(0xB58900), Hex(0x268BD2), Hex(0xD33682), Hex(0x2AA198), Hex(0xEEE8D5),
          Hex(0x073642), Hex(0xCB4B16), Hex(0x586E75), Hex(0x657B83), Hex(0x839496), Hex(0x6C71C4), Hex(0x93A1A1), Hex(0xFDF6E3)}},
        {L"Solarized Light", Hex(0xFDF6E3), Hex(0x657B83), Hex(0x002B36),
         {Hex(0x002B36), Hex(0xDC322F), Hex(0x859900), Hex(0xB58900), Hex(0x268BD2), Hex(0xD33682), Hex(0x2AA198), Hex(0xEEE8D5),
          Hex(0x073642), Hex(0xCB4B16), Hex(0x586E75), Hex(0x657B83), Hex(0x839496), Hex(0x6C71C4), Hex(0x93A1A1), Hex(0xFDF6E3)}},
        {L"Tango Dark", Hex(0x000000), Hex(0xD3D7CF), Hex(0xFFFFFF),
         {Hex(0x000000), Hex(0xCC0000), Hex(0x4E9A06), Hex(0xC4A000), Hex(0x3465A4), Hex(0x75507B), Hex(0x06989A), Hex(0xD3D7CF),
          Hex(0x555753), Hex(0xEF2929), Hex(0x8AE234), Hex(0xFCE94F), Hex(0x729FCF), Hex(0xAD7FA8), Hex(0x34E2E2), Hex(0xEEEEEC)}},
        {L"Tango Light", Hex(0xFFFFFF), Hex(0x555753), Hex(0x000000),
         {Hex(0x000000), Hex(0xCC0000), Hex(0x4E9A06), Hex(0xC4A000), Hex(0x3465A4), Hex(0x75507B), Hex(0x06989A), Hex(0xD3D7CF),
          Hex(0x555753), Hex(0xEF2929), Hex(0x8AE234), Hex(0xFCE94F), Hex(0x729FCF), Hex(0xAD7FA8), Hex(0x34E2E2), Hex(0xEEEEEC)}},
    };
}

std::span<const ColourScheme> Schemes() noexcept
{
    return kSchemes;
}

const ColourScheme* FindScheme(std::wstring_view name) noexcept
{
    for (const ColourScheme& scheme : kSchemes)
    {
        if (name == scheme.name)
        {
            return &scheme;
        }
    }
    return nullptr;
}
