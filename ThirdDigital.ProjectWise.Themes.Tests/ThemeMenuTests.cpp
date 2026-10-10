#include "Features/ThemePicker/ThemeMenuPolicy.h"

#include <CppUnitTest.h>

#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    struct FakeRegistrar final : MenuRegistrar
    {
        struct Command
        {
            unsigned long parent;
            std::wstring label;
            unsigned id;
        };

        int reserveFailsAt = 0;
        bool popupFails = false;
        int commandFailsAt = 0;
        std::vector<std::wstring> events;
        std::vector<unsigned> reserved;
        std::vector<std::wstring> popups;
        std::vector<Command> commands;
        unsigned nextId = 41000;

        unsigned ReserveCommandId() override
        {
            events.push_back(L"reserve");
            if (static_cast<int>(reserved.size()) + 1 == reserveFailsAt)
            {
                return 0;
            }
            reserved.push_back(nextId++);
            return reserved.back();
        }

        unsigned long AddPopup(const wchar_t* title) override
        {
            events.push_back(L"popup");
            if (popupFails)
            {
                return 0;
            }
            popups.push_back(title);
            return 0x7700;
        }

        bool AddCommand(unsigned long parent, const wchar_t* label, unsigned commandId) override
        {
            events.push_back(L"command");
            if (static_cast<int>(commands.size()) + 1 == commandFailsAt)
            {
                return false;
            }
            commands.push_back({parent, label, commandId});
            return true;
        }
    };

    std::vector<const ColourScheme*> g_switched;
    int g_switchCalls = 0;

    bool RecordingSwitch(const ColourScheme* scheme) noexcept
    {
        ++g_switchCalls;
        g_switched.push_back(scheme);
        return true;
    }

    FakeRegistrar Registered()
    {
        FakeRegistrar fake;
        Assert::IsTrue(RegisterThemeMenuItems(fake));
        return fake;
    }

    unsigned IdOf(const FakeRegistrar& fake, const wchar_t* label)
    {
        for (const auto& c : fake.commands)
        {
            if (c.label == label)
            {
                return c.id;
            }
        }
        Assert::Fail(label);
    }
}

TEST_CLASS(ThemeMenuTests)
{
public:
    TEST_METHOD(ToolsPopupHasTenChildren)
    {
        const FakeRegistrar fake = Registered();
        Assert::AreEqual(std::size_t{1}, fake.popups.size());
        Assert::AreEqual(std::wstring(L"Colour Theme"), fake.popups[0]);
        Assert::AreEqual(std::size_t{10}, fake.commands.size());
        const auto schemes = Schemes();
        for (std::size_t i = 0; i < schemes.size(); ++i)
        {
            Assert::AreEqual(std::wstring(schemes[i].name), fake.commands[i].label);
        }
        Assert::AreEqual(std::wstring(L"None"), fake.commands[9].label);
        for (const auto& c : fake.commands)
        {
            Assert::AreEqual(0x7700ul, c.parent, L"children are added under the returned popup handle");
        }
        const auto items = ThemeMenuItems();
        Assert::IsNull(items[9].scheme);
        Assert::IsTrue(items[0].scheme == &schemes[0]);
    }

    TEST_METHOD(IdsAreReserved)
    {
        const FakeRegistrar fake = Registered();
        Assert::AreEqual(std::size_t{10}, fake.reserved.size());
        for (std::size_t i = 0; i < 10; ++i)
        {
            Assert::AreEqual(std::wstring(L"reserve"), fake.events[i], L"all ten IDs are reserved before anything is added");
            Assert::AreEqual(fake.reserved[i], fake.commands[i].id);
        }
        Assert::AreEqual(std::wstring(L"popup"), fake.events[10]);
    }

    TEST_METHOD(OnlyActiveItemIsChecked)
    {
        const FakeRegistrar fake = Registered();
        const ColourScheme* vintage = FindScheme(L"Vintage");
        for (const auto& c : fake.commands)
        {
            Assert::AreEqual(c.label == L"Vintage", IsThemeCommandChecked(c.id, vintage), c.label.c_str());
        }
    }

    TEST_METHOD(NoneIsCheckedForStock)
    {
        const FakeRegistrar fake = Registered();
        for (const auto& c : fake.commands)
        {
            Assert::AreEqual(c.label == L"None", IsThemeCommandChecked(c.id, nullptr), c.label.c_str());
        }
    }

    TEST_METHOD(NoneCallsSwitchWithNull)
    {
        const FakeRegistrar fake = Registered();
        g_switched.clear();
        g_switchCalls = 0;
        Assert::IsTrue(ExecuteThemeCommand(IdOf(fake, L"None"), RecordingSwitch));
        Assert::IsTrue(ExecuteThemeCommand(IdOf(fake, L"Solarized Light"), RecordingSwitch));
        Assert::AreEqual(2, g_switchCalls);
        Assert::IsNull(g_switched[0]);
        Assert::IsTrue(g_switched[1] == FindScheme(L"Solarized Light"));
    }

    TEST_METHOD(UnknownCommandDoesNothing)
    {
        const FakeRegistrar fake = Registered();
        g_switchCalls = 0;
        Assert::IsFalse(ExecuteThemeCommand(1, RecordingSwitch));
        Assert::IsFalse(ExecuteThemeCommand(0, RecordingSwitch));
        Assert::IsFalse(ExecuteThemeCommand(fake.nextId, RecordingSwitch));
        Assert::AreEqual(0, g_switchCalls);
        Assert::IsFalse(IsThemeCommandChecked(1, nullptr));
        Assert::IsFalse(IsThemeCommandChecked(0, nullptr));
    }

    TEST_METHOD(RegistrationFailureIsIsolated)
    {
        for (int failAt = 1; failAt <= 10; ++failAt)
        {
            FakeRegistrar fake;
            fake.reserveFailsAt = failAt;
            Assert::IsFalse(RegisterThemeMenuItems(fake));
            Assert::IsTrue(fake.popups.empty() && fake.commands.empty(), L"nothing added when a reservation fails");
        }

        FakeRegistrar noPopup;
        noPopup.popupFails = true;
        Assert::IsFalse(RegisterThemeMenuItems(noPopup));
        Assert::IsTrue(noPopup.commands.empty());
        Assert::AreEqual(std::size_t{10}, noPopup.reserved.size());
        g_switchCalls = 0;
        Assert::IsFalse(ExecuteThemeCommand(noPopup.reserved[0], RecordingSwitch), L"commands stay inert after a failure");
        Assert::IsFalse(IsThemeCommandChecked(noPopup.reserved[9], nullptr));

        FakeRegistrar brokenChild;
        brokenChild.commandFailsAt = 4;
        Assert::IsFalse(RegisterThemeMenuItems(brokenChild));
        Assert::AreEqual(std::size_t{10}, brokenChild.reserved.size());
        Assert::IsFalse(ExecuteThemeCommand(brokenChild.reserved[0], RecordingSwitch));
        Assert::AreEqual(0, g_switchCalls);
    }

    TEST_METHOD(ExplorerReportedTypesStillSwitch)
    {
        const FakeRegistrar fake = Registered();
        g_switched.clear();
        g_switchCalls = 0;
        Assert::IsTrue(HandleThemeCommand(static_cast<long>(IdOf(fake, L"Tango Dark")), 0, 1, RecordingSwitch));
        Assert::AreEqual(1, g_switchCalls);
        Assert::IsTrue(g_switched[0] == FindScheme(L"Tango Dark"));
        Assert::IsFalse(HandleThemeCommand(1, 0, 1, RecordingSwitch));
        Assert::AreEqual(1, g_switchCalls, L"an unreserved ID does not switch");
    }
};
