#pragma once

#include "Domain/ColourScheme.h"

#include <array>
#include <cstddef>

constexpr std::size_t kThemeMenuItemCount = 10;
constexpr wchar_t kThemeMenuTitle[] = L"Colour Theme";

// The nine schemes in table order, then None (a null scheme).
struct ThemeMenuItem
{
    const wchar_t* label;
    const ColourScheme* scheme;
};

std::array<ThemeMenuItem, kThemeMenuItemCount> ThemeMenuItems() noexcept;

// Implemented over the ProjectWise menu API by the picker; faked in tests.
class MenuRegistrar
{
public:
    // Returns 0 on failure.
    virtual unsigned ReserveCommandId() = 0;
    virtual unsigned long AddPopup(const wchar_t* title) = 0;
    virtual bool AddCommand(unsigned long parent, const wchar_t* label, unsigned commandId) = 0;

protected:
    ~MenuRegistrar() = default;
};

// Reserves all ten command IDs before adding anything; on any failure the commands stay inert.
bool RegisterThemeMenuItems(MenuRegistrar& registrar) noexcept;

bool IsThemeCommandChecked(unsigned commandId, const ColourScheme* active) noexcept;

using SwitchFunction = bool (*)(const ColourScheme*) noexcept;

// Returns false, without switching, for a command that is not one of the registered ten.
bool ExecuteThemeCommand(unsigned commandId, SwitchFunction switchTo) noexcept;

// The execute callback's parameters as Explorer reports them. Explorer 10.00.03.434 reports command type 0
// and parameter type 1 for these Tools items, so only the reserved ID decides. A rejected ID is logged.
bool HandleThemeCommand(long commandId, long commandType, unsigned short parameterType, SwitchFunction switchTo) noexcept;
