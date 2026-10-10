#pragma once

// Adds Tools > Colour Theme to ProjectWise Explorer. Call only when dmawin.dll is loaded; a failure,
// including a delay-load failure, leaves the menu unchanged and affects nothing else.
void RegisterThemeMenu() noexcept;
