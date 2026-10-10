#include "Domain/ExplorerSurface.h"
#include "Domain/SystemColourMapping.h"

#include <CppUnitTest.h>

#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Microsoft::VisualStudio::CppUnitTestFramework
{
    template <>
    std::wstring ToString<ExplorerSurface>(const ExplorerSurface& surface)
    {
        return std::to_wstring(static_cast<int>(surface));
    }
}

namespace
{
    const ExplorerSurface kKnown[] = {
        ExplorerSurface::Dialog, ExplorerSurface::Static, ExplorerSurface::Edit, ExplorerSurface::ListBox,
        ExplorerSurface::ComboBox, ExplorerSurface::Button, ExplorerSurface::ListView, ExplorerSurface::TreeView,
        ExplorerSurface::Tab, ExplorerSurface::ScrollBar, ExplorerSurface::Tooltip,
    };

    std::wstring Text(const wchar_t* value)
    {
        return value == nullptr ? L"(null)" : value;
    }
}

TEST_CLASS(ExplorerSurfaceTests)
{
public:
    TEST_METHOD(KnownClassesClassify)
    {
        struct Case
        {
            const wchar_t* className;
            ExplorerSurface surface;
        };
        const Case cases[] = {
            {L"#32770", ExplorerSurface::Dialog},
            {L"Static", ExplorerSurface::Static},
            {L"Edit", ExplorerSurface::Edit},
            {L"RichEdit20W", ExplorerSurface::Edit},
            {L"RICHEDIT50W", ExplorerSurface::Edit},
            {L"ListBox", ExplorerSurface::ListBox},
            {L"ComboLBox", ExplorerSurface::ListBox},
            {L"ComboBox", ExplorerSurface::ComboBox},
            {L"Button", ExplorerSurface::Button},
            {L"SysListView32", ExplorerSurface::ListView},
            {L"SysTreeView32", ExplorerSurface::TreeView},
            {L"SysTabControl32", ExplorerSurface::Tab},
            {L"ScrollBar", ExplorerSurface::ScrollBar},
            {L"tooltips_class32", ExplorerSurface::Tooltip},
            {L"edit", ExplorerSurface::Edit},
            {L"SYSTREEVIEW32", ExplorerSurface::TreeView},
        };
        for (const Case& c : cases)
        {
            Assert::AreEqual(c.surface, ClassifyWindowClass(c.className), c.className);
        }
    }

    TEST_METHOD(UnknownClassesRemainUnknown)
    {
        for (const wchar_t* name : {L"", L"AfxWnd140u", L"Afx:00400000:8", L"#32768", L"Edit2", L"SysListView", L"SysHeader32",
                                    L"msctls_statusbar32", L"Chrome_WidgetWin_0", L"WindowsForms10.EDIT.app.0.141b42a_r6_ad1"})
        {
            Assert::AreEqual(ExplorerSurface::Unknown, ClassifyWindowClass(name), name);
        }
        const SurfaceColours none = ColoursFor(ExplorerSurface::Unknown);
        Assert::AreEqual(kNoColour, none.background);
        Assert::AreEqual(kNoColour, none.text);
        Assert::IsNull(DarkThemeClassFor(ExplorerSurface::Unknown, true));
    }

    TEST_METHOD(SurfaceRolesUseSystemMapping)
    {
        Assert::AreEqual(COLOR_3DFACE, ColoursFor(ExplorerSurface::Dialog).background);
        Assert::AreEqual(COLOR_BTNTEXT, ColoursFor(ExplorerSurface::Dialog).text);
        Assert::AreEqual(COLOR_3DFACE, ColoursFor(ExplorerSurface::Static).background);
        Assert::AreEqual(COLOR_WINDOWTEXT, ColoursFor(ExplorerSurface::Static).text);
        Assert::AreEqual(COLOR_WINDOW, ColoursFor(ExplorerSurface::Edit).background);
        Assert::AreEqual(COLOR_WINDOWTEXT, ColoursFor(ExplorerSurface::Edit).text);
        Assert::AreEqual(COLOR_WINDOW, ColoursFor(ExplorerSurface::ListBox).background);
        Assert::AreEqual(COLOR_WINDOW, ColoursFor(ExplorerSurface::ComboBox).background);
        Assert::AreEqual(COLOR_3DFACE, ColoursFor(ExplorerSurface::Button).background);
        Assert::AreEqual(COLOR_BTNTEXT, ColoursFor(ExplorerSurface::Button).text);
        Assert::AreEqual(COLOR_WINDOW, ColoursFor(ExplorerSurface::ListView).background);
        Assert::AreEqual(COLOR_WINDOW, ColoursFor(ExplorerSurface::TreeView).background);
        Assert::AreEqual(COLOR_3DFACE, ColoursFor(ExplorerSurface::Tab).background);
        Assert::AreEqual(COLOR_SCROLLBAR, ColoursFor(ExplorerSurface::ScrollBar).background);
        Assert::AreEqual(COLOR_INFOBK, ColoursFor(ExplorerSurface::Tooltip).background);
        Assert::AreEqual(COLOR_INFOTEXT, ColoursFor(ExplorerSurface::Tooltip).text);

        for (const ColourScheme& scheme : Schemes())
        {
            const SystemColourPalette palette = BuildPalette(scheme);
            for (ExplorerSurface surface : kKnown)
            {
                const SurfaceColours roles = ColoursFor(surface);
                Assert::IsTrue(palette.At(roles.background).has_value(), scheme.name);
                Assert::IsTrue(roles.text == kNoColour || palette.At(roles.text).has_value(), scheme.name);
            }
        }
    }

    TEST_METHOD(DarkThemeNamesMatchSurface)
    {
        for (ExplorerSurface surface : kKnown)
        {
            const wchar_t* expected = nullptr;
            if (surface == ExplorerSurface::TreeView || surface == ExplorerSurface::ScrollBar || surface == ExplorerSurface::Tooltip)
            {
                expected = L"DarkMode_Explorer";
            }
            else if (surface == ExplorerSurface::ListView)
            {
                expected = L"DarkMode_DarkTheme";
            }
            else if (surface == ExplorerSurface::Edit || surface == ExplorerSurface::ComboBox)
            {
                expected = L"DarkMode_CFD";
            }
            Assert::AreEqual(Text(expected), Text(DarkThemeClassFor(surface, true)));
        }
    }

    TEST_METHOD(LightRequestsNoThemeChange)
    {
        for (ExplorerSurface surface : kKnown)
        {
            Assert::IsNull(DarkThemeClassFor(surface, false));
        }
        Assert::IsNull(DarkThemeClassFor(ExplorerSurface::Unknown, false));
        Assert::IsNotNull(DarkThemeClassFor(ExplorerSurface::TreeView, true));
    }
};
