// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Light/Dark/System theme selection (implementation-plan.md §4).
// wx ≥ 3.3: wxApp::SetAppearance() — one consistent API on MSW/GTK/macOS
// (runtime switching on MSW needs 3.3.3+; otherwise restart).
// wx 3.2 fallback (system-wx dev builds): live GTK
// `gtk-application-prefer-dark-theme` toggle; unsupported elsewhere.

#pragma once

#include <string_view>

namespace repomancer::gui {

enum class ThemeMode { System, Light, Dark };

[[nodiscard]] ThemeMode theme_mode_from_string(std::string_view value);
[[nodiscard]] const char* theme_mode_to_string(ThemeMode mode);

// Call once, early in wxApp::OnInit, before the first apply_theme():
// captures the platform's initial state so System can be restored.
void init_theme_support();

// Returns true if the theme took effect live; false means it needs an app
// restart (or is unsupported on this build).
bool apply_theme(ThemeMode mode);

} // namespace repomancer::gui
