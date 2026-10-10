#include "ThemeMenuPolicy.h"

#include "Shared/Log.h"

namespace
{
    // UI thread only: written during registration, read by Explorer's menu callbacks.
    std::array<unsigned, kThemeMenuItemCount> g_commandIds{};

    int IndexOf(unsigned commandId) noexcept
    {
        if (commandId == 0)
        {
            return -1;
        }
        for (std::size_t i = 0; i < g_commandIds.size(); ++i)
        {
            if (g_commandIds[i] == commandId)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
}

std::array<ThemeMenuItem, kThemeMenuItemCount> ThemeMenuItems() noexcept
{
    std::array<ThemeMenuItem, kThemeMenuItemCount> items{};
    const auto schemes = Schemes();
    for (std::size_t i = 0; i + 1 < items.size() && i < schemes.size(); ++i)
    {
        items[i] = {schemes[i].name, &schemes[i]};
    }
    items[kThemeMenuItemCount - 1] = {L"None", nullptr};
    return items;
}

bool RegisterThemeMenuItems(MenuRegistrar& registrar) noexcept
{
    g_commandIds = {};
    try
    {
        std::array<unsigned, kThemeMenuItemCount> ids{};
        for (unsigned& id : ids)
        {
            id = registrar.ReserveCommandId();
            if (id == 0)
            {
                return false;
            }
        }
        const unsigned long popup = registrar.AddPopup(kThemeMenuTitle);
        if (popup == 0)
        {
            return false;
        }
        const auto items = ThemeMenuItems();
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            if (!registrar.AddCommand(popup, items[i].label, ids[i]))
            {
                return false;
            }
        }
        g_commandIds = ids;
        return true;
    }
    catch (...)
    {
        g_commandIds = {};
        return false;
    }
}

bool IsThemeCommandChecked(unsigned commandId, const ColourScheme* active) noexcept
{
    const int index = IndexOf(commandId);
    return index >= 0 && ThemeMenuItems()[static_cast<std::size_t>(index)].scheme == active;
}

bool ExecuteThemeCommand(unsigned commandId, SwitchFunction switchTo) noexcept
{
    const int index = IndexOf(commandId);
    if (index < 0 || switchTo == nullptr)
    {
        return false;
    }
    switchTo(ThemeMenuItems()[static_cast<std::size_t>(index)].scheme);
    return true;
}

bool HandleThemeCommand(long commandId, long commandType, unsigned short parameterType, SwitchFunction switchTo) noexcept
{
    if (ExecuteThemeCommand(static_cast<unsigned>(commandId), switchTo))
    {
        return true;
    }
    Log(L"Tools menu command %ld rejected (type %ld, parameter type %u)", commandId, commandType, static_cast<unsigned>(parameterType));
    return false;
}
