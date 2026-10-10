#pragma once

#include "Domain/ColourScheme.h"

// All functions run on the UI thread that called StartTheming.

// Records the calling thread as the UI thread and activates the saved selection, if any. The refresh of
// windows that already exist is deferred to a zero-delay thread timer so half-constructed windows are
// never re-entered from inside initialisation.
bool StartTheming() noexcept;

bool Activate(const ColourScheme& scheme) noexcept;

// Saves the preference first, then applies it live; null is None. A request made while a transition is
// running is deferred until it returns.
bool Switch(const ColourScheme* scheme) noexcept;

void ApplyToExistingWindows(bool themeClassChanged) noexcept;

// Rollback for failures: unpublishes the palette and restores every window this module changed. The
// saved preference is left as it is.
void RestoreStock() noexcept;
