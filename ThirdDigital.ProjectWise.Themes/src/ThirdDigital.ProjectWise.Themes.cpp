#include <windows.h>

extern "C" LONG WINAPI CustomInitialize(ULONG applicationMask, LPVOID reserved)
{
    UNREFERENCED_PARAMETER(applicationMask);
    UNREFERENCED_PARAMETER(reserved);
    return IDOK;
}
