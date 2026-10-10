#pragma once

#include "Shared/ActivePalette.h"

// Installs a WH_CBT hook for uiThreadId only, which must be the calling thread. Windows created on that
// thread are subclassed so WM_CTLCOLOR* stock brushes follow the palette. Subclassing, SetWindowTheme
// and DWM changes are only ever made on windows owned by that thread, from that thread.
bool InstallControlHook(DWORD uiThreadId) noexcept;

// Subclasses supported windows and applies or removes the dark window theme. A null palette, or a light
// one, only resets windows this module themed.
void ApplyControlTheme(HWND window, const PaletteResources* resources) noexcept;

// Everything a theme switch applies to a top-level window and its descendants. UI thread only.
void ApplyWindowTree(HWND top, const PaletteResources* resources) noexcept;

bool OwnedByUiThread(HWND window) noexcept;

// Called on the UI thread when a structured exception interrupts the deferred theming of new windows.
using DeferredFaultHandler = void (*)() noexcept;
void SetDeferredFaultHandler(DeferredFaultHandler handler) noexcept;

namespace ControlSeams
{
    using SetHook = HHOOK(WINAPI*)(int, HOOKPROC, HINSTANCE, DWORD);
    extern SetHook setWindowsHookEx;
    using ThemeAvailable = bool(WINAPI*)(LPCWSTR subApp) noexcept;
    extern ThemeAvailable themeAvailable;
}
