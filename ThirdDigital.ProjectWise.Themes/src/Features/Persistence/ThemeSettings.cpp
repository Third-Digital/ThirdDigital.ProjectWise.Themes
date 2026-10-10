#include "ThemeSettings.h"

#include <cwchar>

namespace
{
    constexpr wchar_t kDefaultKey[] = L"Software\\Third Digital\\ProjectWise Themes";
    constexpr wchar_t kValue[] = L"Scheme";
    constexpr DWORD kMaxKeyChars = 256;
    constexpr DWORD kMaxNameChars = 64;

    void KeyPath(wchar_t (&path)[kMaxKeyChars]) noexcept
    {
        const DWORD length = GetEnvironmentVariableW(L"TDPWTHEMES_SETTINGS_KEY", path, kMaxKeyChars);
        if (length == 0 || length >= kMaxKeyChars)
        {
            wcscpy_s(path, kDefaultKey);
        }
    }
}

const ColourScheme* ReadSelection() noexcept
{
    wchar_t path[kMaxKeyChars]{};
    KeyPath(path);

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
    {
        return nullptr;
    }
    wchar_t name[kMaxNameChars + 1]{};
    DWORD type = 0;
    DWORD bytes = kMaxNameChars * sizeof(wchar_t);
    const LSTATUS status = RegQueryValueExW(key, kValue, nullptr, &type, reinterpret_cast<BYTE*>(name), &bytes);
    RegCloseKey(key);

    if (status != ERROR_SUCCESS || type != REG_SZ || bytes < 2 * sizeof(wchar_t) || bytes % sizeof(wchar_t) != 0)
    {
        return nullptr;
    }
    const std::size_t chars = bytes / sizeof(wchar_t);
    if (name[chars - 1] != L'\0' || wcslen(name) != chars - 1)
    {
        return nullptr;
    }
    return FindScheme(std::wstring_view(name, chars - 1));
}

bool SaveSelection(const ColourScheme* scheme) noexcept
{
    wchar_t path[kMaxKeyChars]{};
    KeyPath(path);

    if (scheme == nullptr)
    {
        HKEY key = nullptr;
        const LSTATUS open = RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_SET_VALUE, &key);
        if (open == ERROR_FILE_NOT_FOUND)
        {
            return true;
        }
        if (open != ERROR_SUCCESS)
        {
            return false;
        }
        const LSTATUS removed = RegDeleteValueW(key, kValue);
        RegCloseKey(key);
        return removed == ERROR_SUCCESS || removed == ERROR_FILE_NOT_FOUND;
    }

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
    {
        return false;
    }
    const DWORD bytes = static_cast<DWORD>((wcslen(scheme->name) + 1) * sizeof(wchar_t));
    const LSTATUS status = RegSetValueExW(key, kValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(scheme->name), bytes);
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}
