#include "Fixtures.h"

#include "Domain/ColourScheme.h"

#include <cstdio>
#include <cwchar>
#include <io.h>
#include <fcntl.h>

using namespace Harness;

namespace
{
    int ProcessIsX86(const CheckContext&)
    {
        Recorder r(L"ProcessIsX86");
        r.Expect(sizeof(void*) == 4, L"pointer size %u", static_cast<unsigned>(sizeof(void*)));
        USHORT processMachine = 0;
        USHORT nativeMachine = 0;
        if (!IsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine))
        {
            r.Invalidate(L"IsWow64Process2 failed (%lu)", GetLastError());
            return r.Result();
        }
        const bool x86Image = processMachine == IMAGE_FILE_MACHINE_I386 ||
                              (processMachine == IMAGE_FILE_MACHINE_UNKNOWN && nativeMachine == IMAGE_FILE_MACHINE_I386);
        r.Expect(x86Image, L"process machine 0x%04X native 0x%04X", processMachine, nativeMachine);
        return r.Result();
    }

    int SettingsKeyIsIsolated(const CheckContext&)
    {
        Recorder r(L"SettingsKeyIsIsolated");
        const std::wstring key = SettingsKey();
        if (!r.Expect(key.rfind(L"Software\\TDPWThemesHarness-", 0) == 0, L"settings key '%s'", key.c_str()))
        {
            return r.Result();
        }
        HKEY handle = nullptr;
        const LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &handle, nullptr);
        if (status != ERROR_SUCCESS)
        {
            r.Invalidate(L"cannot create test key (%ld)", status);
            return r.Result();
        }
        const wchar_t marker[] = L"smoke";
        r.Expect(RegSetValueExW(handle, L"Marker", 0, REG_SZ, reinterpret_cast<const BYTE*>(marker), sizeof(marker)) == ERROR_SUCCESS,
                 L"marker written for the runner's cleanup check");
        RegCloseKey(handle);
        return r.Result();
    }

    const CheckRegistration processIsX86{{L"Smoke", L"ProcessIsX86", L"-", Host::Self, false, ProcessIsX86}};
    const CheckRegistration settingsKeyIsIsolated{{L"Smoke", L"SettingsKeyIsIsolated", L"-", Host::Self, false, SettingsKeyIsIsolated}};

    const wchar_t* Argument(int argc, wchar_t** argv, const wchar_t* name)
    {
        for (int i = 1; i + 1 < argc; ++i)
        {
            if (std::wcscmp(argv[i], name) == 0)
            {
                return argv[i + 1];
            }
        }
        return nullptr;
    }

    bool HasFlag(int argc, wchar_t** argv, const wchar_t* name)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (std::wcscmp(argv[i], name) == 0)
            {
                return true;
            }
        }
        return false;
    }

    int List(const wchar_t* group)
    {
        for (const CheckDefinition& check : Checks())
        {
            if (group != nullptr && *group != L'\0' && _wcsicmp(group, check.group) != 0)
            {
                continue;
            }
            std::fwprintf(stdout, L"%s\t%s\t%s\t%s\t%s\n", check.group, check.name, check.mode,
                          check.host == Host::Pwc ? L"pwc" : L"self", check.perScheme ? L"*" : L"-");
        }
        return Passed;
    }

    int Run(int argc, wchar_t** argv, const wchar_t* name)
    {
        for (const CheckDefinition& check : Checks())
        {
            if (std::wcscmp(check.name, name) != 0)
            {
                continue;
            }
            CheckContext context;
            const wchar_t* value = Argument(argc, argv, L"--scheme");
            context.scheme = value != nullptr ? value : L"";
            value = Argument(argc, argv, L"--dll");
            context.dllPath = value != nullptr ? value : L"";
            value = Argument(argc, argv, L"--bridge");
            context.bridgePath = value != nullptr ? value : L"";
            value = Argument(argc, argv, L"--evidence");
            context.evidenceDir = value != nullptr ? value : L"";
            wchar_t desktop[128]{};
            DWORD needed = 0;
            GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, desktop, sizeof(desktop), &needed);
            std::fwprintf(stdout, L"note desktop %s\n", desktop);
            const int result = check.run(context);
            DeleteSettingsKey();
            return result;
        }
        std::fwprintf(stdout, L"INVALID unknown check '%s'\n", name);
        return Invalid;
    }
}

int wmain(int argc, wchar_t** argv)
{
    (void)_setmode(_fileno(stdout), _O_U8TEXT);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

    if (HasFlag(argc, argv, L"--schemes"))
    {
        for (const ColourScheme& scheme : Schemes())
        {
            std::fwprintf(stdout, L"%s\n", scheme.name);
        }
        return Passed;
    }
    if (HasFlag(argc, argv, L"--list"))
    {
        return List(Argument(argc, argv, L"--filter"));
    }
    if (const wchar_t* name = Argument(argc, argv, L"--run"))
    {
        return Run(argc, argv, name);
    }
    std::fwprintf(stdout, L"usage: --schemes | --list [--filter group] | --run check [--scheme name] [--dll path] [--bridge path] [--evidence dir]\n");
    return Invalid;
}
