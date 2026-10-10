#pragma once

#include "Domain/ColourScheme.h"

// HKCU\Software\Third Digital\ProjectWise Themes, value "Scheme". The TDPWTHEMES_SETTINGS_KEY environment
// variable replaces the subkey path beneath HKCU so tests never touch the real preference.
const ColourScheme* ReadSelection() noexcept;

// Null deletes the value (None). Returns false only when the preference could not be changed.
bool SaveSelection(const ColourScheme* scheme) noexcept;
