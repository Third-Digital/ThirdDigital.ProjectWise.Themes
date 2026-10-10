#include "Features/Persistence/ThemeSettings.h"
#include "Shared/ActivePalette.h"
#include "Shared/Log.h"

#include <CppUnitTest.h>

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    const wchar_t kRealKey[] = L"Software\\Third Digital\\ProjectWise Themes";

    class ScopedEnvironment
    {
    public:
        ScopedEnvironment(const wchar_t* name, const std::wstring& value) : name_(name)
        {
            wchar_t buffer[2048]{};
            const DWORD length = GetEnvironmentVariableW(name, buffer, ARRAYSIZE(buffer));
            if (length > 0 && length < ARRAYSIZE(buffer))
            {
                previous_ = buffer;
            }
            SetEnvironmentVariableW(name, value.c_str());
        }

        ~ScopedEnvironment()
        {
            SetEnvironmentVariableW(name_, previous_ ? previous_->c_str() : nullptr);
        }

    private:
        const wchar_t* name_;
        std::optional<std::wstring> previous_;
    };

    class TestSettingsKey
    {
    public:
        TestSettingsKey()
            : path_(L"Software\\TDPWThemesTests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++counter_)),
              redirect_(L"TDPWTHEMES_SETTINGS_KEY", path_)
        {
        }

        ~TestSettingsKey()
        {
            RegDeleteTreeW(HKEY_CURRENT_USER, path_.c_str());
            RegDeleteKeyW(HKEY_CURRENT_USER, path_.c_str());
        }

        const std::wstring& Path() const
        {
            return path_;
        }

        void WriteRaw(DWORD type, const void* data, DWORD bytes) const
        {
            HKEY key = nullptr;
            Assert::AreEqual(ERROR_SUCCESS, static_cast<long>(RegCreateKeyExW(HKEY_CURRENT_USER, path_.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)));
            Assert::AreEqual(ERROR_SUCCESS, static_cast<long>(RegSetValueExW(key, L"Scheme", 0, type, static_cast<const BYTE*>(data), bytes)));
            RegCloseKey(key);
        }

        bool ValueExists() const
        {
            return RegGetValueW(HKEY_CURRENT_USER, path_.c_str(), L"Scheme", RRF_RT_ANY, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
        }

    private:
        static inline int counter_ = 0;
        std::wstring path_;
        ScopedEnvironment redirect_;
    };

    std::optional<std::wstring> RealPreference()
    {
        wchar_t buffer[512]{};
        DWORD bytes = sizeof(buffer);
        DWORD type = 0;
        const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, kRealKey, L"Scheme", RRF_RT_ANY | RRF_NOEXPAND, &type, buffer, &bytes);
        if (status != ERROR_SUCCESS)
        {
            return std::nullopt;
        }
        return std::to_wstring(type) + L":" + std::wstring(buffer);
    }

    bool RealKeyExists()
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRealKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        {
            return false;
        }
        RegCloseKey(key);
        return true;
    }

    const ColourScheme& Scheme(const wchar_t* name)
    {
        const ColourScheme* scheme = FindScheme(name);
        Assert::IsNotNull(scheme, name);
        return *scheme;
    }

    COLORREF BrushColour(HBRUSH brush)
    {
        LOGBRUSH info{};
        Assert::AreEqual(static_cast<int>(sizeof(info)), GetObjectW(brush, sizeof(info), &info));
        Assert::AreEqual(static_cast<unsigned>(BS_SOLID), info.lbStyle);
        return info.lbColor;
    }

    std::vector<HBRUSH> g_created;
    std::vector<HGDIOBJ> g_deleted;
    int g_failOnCall = 0;

    BOOL WINAPI RecordingDeleteObject(HGDIOBJ object)
    {
        g_deleted.push_back(object);
        return DeleteObject(object);
    }

    HBRUSH WINAPI FailingCreateBrush(COLORREF colour)
    {
        if (static_cast<int>(g_created.size()) + 1 == g_failOnCall)
        {
            return nullptr;
        }
        HBRUSH brush = CreateSolidBrush(colour);
        g_created.push_back(brush);
        return brush;
    }

    std::wstring TempDirectory(const wchar_t* tag)
    {
        wchar_t base[MAX_PATH]{};
        GetTempPathW(MAX_PATH, base);
        std::wstring path = std::wstring(base) + L"tdpwthemes-tests-" + tag + L"-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        CreateDirectoryW(path.c_str(), nullptr);
        return path;
    }
}

TEST_CLASS(SettingsAndPaletteTests)
{
public:
    TEST_METHOD(SettingsRoundTrip)
    {
        TestSettingsKey key;
        Assert::IsNull(ReadSelection());
        Assert::IsTrue(SaveSelection(&Scheme(L"Solarized Dark")));
        Assert::IsTrue(ReadSelection() == &Scheme(L"Solarized Dark"));
        Assert::IsTrue(SaveSelection(&Scheme(L"One Half Light")));
        Assert::IsTrue(ReadSelection() == &Scheme(L"One Half Light"));

        wchar_t stored[64]{};
        DWORD bytes = sizeof(stored);
        DWORD type = 0;
        Assert::AreEqual(ERROR_SUCCESS, static_cast<long>(RegGetValueW(HKEY_CURRENT_USER, key.Path().c_str(), L"Scheme", RRF_RT_ANY | RRF_NOEXPAND, &type, stored, &bytes)));
        Assert::AreEqual(static_cast<DWORD>(REG_SZ), type);
        Assert::AreEqual(std::wstring(L"One Half Light"), std::wstring(stored));
    }

    TEST_METHOD(InvalidSettingsMeanStock)
    {
        TestSettingsKey key;
        Assert::IsNull(ReadSelection());

        const DWORD number = 1;
        key.WriteRaw(REG_DWORD, &number, sizeof(number));
        Assert::IsNull(ReadSelection(), L"REG_DWORD");

        key.WriteRaw(REG_SZ, L"", sizeof(wchar_t));
        Assert::IsNull(ReadSelection(), L"empty");

        key.WriteRaw(REG_SZ, L"campbell", sizeof(L"campbell"));
        Assert::IsNull(ReadSelection(), L"wrong case");

        key.WriteRaw(REG_SZ, L"Dimidium", sizeof(L"Dimidium"));
        Assert::IsNull(ReadSelection(), L"unknown scheme");

        key.WriteRaw(REG_EXPAND_SZ, L"Campbell", sizeof(L"Campbell"));
        Assert::IsNull(ReadSelection(), L"REG_EXPAND_SZ");

        // RegQueryValueExW appends a missing terminator when the buffer has room, so only a value that fills the
        // read buffer exactly reaches the termination check.
        const std::wstring unterminated(64, L'C');
        key.WriteRaw(REG_SZ, unterminated.c_str(), static_cast<DWORD>(unterminated.size() * sizeof(wchar_t)));
        Assert::IsNull(ReadSelection(), L"unterminated");

        key.WriteRaw(REG_SZ, L"Campbell", sizeof(L"Campbell") - 1);
        Assert::IsNull(ReadSelection(), L"odd length");

        const std::wstring longName(2000, L'C');
        key.WriteRaw(REG_SZ, longName.c_str(), static_cast<DWORD>((longName.size() + 1) * sizeof(wchar_t)));
        Assert::IsNull(ReadSelection(), L"oversized");

        const wchar_t embedded[] = L"Campbell\0Vintage";
        key.WriteRaw(REG_SZ, embedded, sizeof(embedded));
        Assert::IsNull(ReadSelection(), L"embedded terminator");

        key.WriteRaw(REG_SZ, L"Campbell", sizeof(L"Campbell"));
        Assert::IsTrue(ReadSelection() == &Scheme(L"Campbell"), L"valid value after invalid ones");
    }

    TEST_METHOD(NoneDeletesValue)
    {
        TestSettingsKey key;
        Assert::IsTrue(SaveSelection(nullptr), L"absent key is already None");
        Assert::IsTrue(SaveSelection(&Scheme(L"Vintage")));
        Assert::IsTrue(key.ValueExists());
        Assert::IsTrue(SaveSelection(nullptr));
        Assert::IsFalse(key.ValueExists());
        Assert::IsNull(ReadSelection());
        Assert::IsTrue(SaveSelection(nullptr), L"absent value is already None");
    }

    TEST_METHOD(RedirectNeverTouchesRealPreference)
    {
        const bool existedBefore = RealKeyExists();
        const auto before = RealPreference();
        {
            TestSettingsKey key;
            Assert::IsTrue(SaveSelection(&Scheme(L"Tango Dark")));
            Assert::IsTrue(ReadSelection() == &Scheme(L"Tango Dark"));
            Assert::IsTrue(key.ValueExists());
            Assert::IsTrue(SaveSelection(nullptr));
        }
        Assert::AreEqual(existedBefore, RealKeyExists());
        Assert::IsTrue(before == RealPreference());
    }

    TEST_METHOD(BrushesMatchPalette)
    {
        const ColourScheme& scheme = Scheme(L"Campbell Powershell");
        const PaletteResources* resources = PreparePalette(scheme);
        Assert::IsNotNull(resources);
        Assert::IsTrue(resources->scheme == &scheme);
        Assert::IsTrue(resources == PreparePalette(scheme), L"prepared resources are cached per scheme");
        const SystemColourPalette expected = BuildPalette(scheme);
        Assert::AreEqual(expected.isDark, resources->palette.isDark);
        for (int index = -2; index <= COLOR_MENUBAR + 2; ++index)
        {
            const auto colour = expected.At(index);
            if (colour)
            {
                Assert::AreEqual(static_cast<unsigned long>(*colour), static_cast<unsigned long>(resources->palette.At(index).value()));
                Assert::AreEqual(static_cast<unsigned long>(*colour), static_cast<unsigned long>(BrushColour(resources->Brush(index))));
                Assert::AreEqual(static_cast<unsigned long>(*colour), static_cast<unsigned long>(BrushColour(resources->ControlBrush(index))));
                Assert::IsTrue(resources->Brush(index) != resources->ControlBrush(index), L"control brushes are distinct handles");
            }
            else
            {
                Assert::IsNull(resources->Brush(index));
                Assert::IsNull(resources->ControlBrush(index));
            }
        }
    }

    TEST_METHOD(PreparationFailureDoesNotPublish)
    {
        // Uses a scheme no other test prepares, because successful preparations are cached for the process.
        const ColourScheme& scheme = Scheme(L"Tango Light");
        const PaletteResources* active = ActivePalette();
        g_created.clear();
        g_deleted.clear();
        g_failOnCall = 5;
        PaletteSeams::createSolidBrush = FailingCreateBrush;
        PaletteSeams::deleteObject = RecordingDeleteObject;
        const PaletteResources* failed = PreparePalette(scheme);
        PaletteSeams::createSolidBrush = ::CreateSolidBrush;
        PaletteSeams::deleteObject = ::DeleteObject;
        Assert::IsNull(failed);
        Assert::IsTrue(active == ActivePalette());
        Assert::AreEqual(std::size_t{4}, g_created.size());
        Assert::AreEqual(g_created.size(), g_deleted.size(), L"brushes of a failed preparation are freed");
        for (HBRUSH brush : g_created)
        {
            Assert::IsTrue(std::find(g_deleted.begin(), g_deleted.end(), brush) != g_deleted.end());
        }

        const PaletteResources* retried = PreparePalette(scheme);
        Assert::IsNotNull(retried, L"a failed preparation is not cached");
    }

    TEST_METHOD(PublishedResourcesRemainValid)
    {
        const PaletteResources* first = PreparePalette(Scheme(L"Solarized Light"));
        const PaletteResources* second = PreparePalette(Scheme(L"One Half Dark"));
        Assert::IsNotNull(first);
        Assert::IsNotNull(second);
        PublishPalette(first);
        Assert::IsTrue(first == ActivePalette());
        PublishPalette(second);
        Assert::IsTrue(second == ActivePalette());
        PublishPalette(nullptr);
        Assert::IsNull(ActivePalette());
        for (const PaletteResources* resources : {first, second})
        {
            for (int index = 0; index <= COLOR_MENUBAR; ++index)
            {
                if (resources->palette.At(index))
                {
                    Assert::AreEqual(static_cast<DWORD>(OBJ_BRUSH), GetObjectType(resources->Brush(index)));
                    Assert::AreEqual(static_cast<DWORD>(OBJ_BRUSH), GetObjectType(resources->ControlBrush(index)));
                }
            }
        }
    }

    TEST_METHOD(LogFailureIsNonFatal)
    {
        const std::wstring good = TempDirectory(L"log");
        {
            ScopedEnvironment tmp(L"TMP", good);
            ScopedEnvironment temp(L"TEMP", good);
            Log(L"unit test entry %d", 42);
        }
        std::ifstream file(good + L"\\ThirdDigital\\projectwise-themes.log", std::ios::binary);
        std::stringstream text;
        text << file.rdbuf();
        Assert::IsTrue(text.str().find("unit test entry 42") != std::string::npos, L"log line is appended as UTF-8");
        file.close();
        DeleteFileW((good + L"\\ThirdDigital\\projectwise-themes.log").c_str());
        RemoveDirectoryW((good + L"\\ThirdDigital").c_str());

        const std::wstring blocker = good + L"\\blocker";
        HANDLE handle = CreateFileW(blocker.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        Assert::IsTrue(handle != INVALID_HANDLE_VALUE);
        CloseHandle(handle);
        {
            ScopedEnvironment tmp(L"TMP", blocker + L"\\nested");
            ScopedEnvironment temp(L"TEMP", blocker + L"\\nested");
            Log(L"cannot be written %s", L"anywhere");
        }
        DeleteFileW(blocker.c_str());
        RemoveDirectoryW(good.c_str());
    }
};
