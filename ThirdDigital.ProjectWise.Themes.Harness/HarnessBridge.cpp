#include "Domain/ColourScheme.h"
#include "Features/ThemeSwitch.h"

#include <cwchar>

// Test support for the WinForms harness: the production feature sources compiled into a DLL with two
// entry points. Never installed or registered; the shipped module exports only CustomInitialize.

extern "C" __declspec(dllexport) int __cdecl InitializeHarness()
{
    return StartTheming() ? 1 : 0;
}

extern "C" __declspec(dllexport) int __cdecl SwitchHarnessScheme(const wchar_t* name)
{
    if (name == nullptr || *name == L'\0' || std::wcscmp(name, L"None") == 0)
    {
        return Switch(nullptr) ? 1 : 0;
    }
    const ColourScheme* scheme = FindScheme(name);
    return scheme != nullptr && Switch(scheme) ? 1 : 0;
}
