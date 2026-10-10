#include <windows.h>

#include <aawindms.h>

#include "ThemeMenu.h"

#include "Features/ThemePicker/ThemeMenuPolicy.h"
#include "Features/ThemeSwitch.h"
#include "Shared/ActivePalette.h"
#include "Shared/Log.h"

namespace
{
    LONG CALLBACK ExecuteTheme(LPAACMDPARAM command)
    {
        __try
        {
            if (command == nullptr)
            {
                return -1;
            }
            return HandleThemeCommand(command->lCommandId, command->lCommandType, command->ushParamType, Switch) ? 0 : -1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    LONG CALLBACK ThemeState(LPAACMDSTATE state)
    {
        __try
        {
            if (state == nullptr)
            {
                return 0;
            }
            const PaletteResources* active = ActivePalette();
            return IsThemeCommandChecked(static_cast<unsigned>(state->lCommandId), active != nullptr ? active->scheme : nullptr) ? AAMS_CHECKED : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    class ToolsMenu final : public MenuRegistrar
    {
    public:
        unsigned ReserveCommandId() override
        {
            return aaApi_ReserveCommandIdByType(AAMENU_TOOLS);
        }

        unsigned long AddPopup(const wchar_t* title) override
        {
            AADMSMENUITEM item{};
            item.ulMask = AADMSMIF_FLAGS | AADMSMIF_NAME | AADMSMIF_CMDTYPE;
            item.uiFlags = AAMIF_POPUP;
            item.uiCmdType = AAMENU_TOOLS;
            item.lptstrName = const_cast<LPWSTR>(title);
            return aaApi_AddCustomMenuItem(0, AADMSMI_LAST, &item);
        }

        bool AddCommand(unsigned long parent, const wchar_t* label, unsigned commandId) override
        {
            AADMSMENUITEM item{};
            item.ulMask = AADMSMIF_FLAGS | AADMSMIF_NAME | AADMSMIF_CMDTYPE | AADMSMIF_CMDID | AADMSMIF_CALLBACK | AADMSMIF_STATEPROC;
            item.uiFlags = AAMIF_COMMAND;
            item.uiCmdType = AAMENU_TOOLS;
            item.lptstrName = const_cast<LPWSTR>(label);
            item.uiCommandId = commandId;
            item.ushParamType = AACMDPT_GENERIC;
            item.fpExecute = ExecuteTheme;
            item.fpGetState = ThemeState;
            return aaApi_AddCustomMenuItem(parent, AADMSMI_LAST, &item) != 0;
        }
    };

    bool Register() noexcept
    {
        ToolsMenu menu;
        return RegisterThemeMenuItems(menu);
    }
}

void RegisterThemeMenu() noexcept
{
    bool registered = false;
    // dmawin.dll is delay-loaded; a missing or mismatched export raises an SEH exception here.
    __try
    {
        registered = Register();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        registered = false;
    }
    Log(registered ? L"Tools menu picker registered" : L"Tools menu picker could not be registered");
}
