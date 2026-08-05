// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Application settings: a JSON file in the per-user config directory
// (implementation-plan.md §8 — JSON over wxConfig: portable, diffable).
// Strict parsing with a size cap; unknown/invalid content degrades to
// defaults, never to an error dialog.

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace repomancer {

// Most-recently-opened repositories kept in settings (and shown in the menu).
inline constexpr std::size_t kMaxRecentRepos = 8;

struct Settings {
    // "system" | "light" | "dark"
    std::string theme = "system";
    // "angular" | "rounded"
    std::string graph_style = "angular";
    // Preferences → VCS Providers: the git executable to run. "git" means
    // PATH lookup; anything else is used as given.
    std::string git_binary = "git";
    // Preferences → Interface: the wxbf integrated title bar (Windows/Linux).
    // Applied at startup; toggling restarts the frame.
    bool integrated_titlebar = true;
    // Window-button style for the integrated bar: 0 = system-native (GTK
    // header bar), 1 = flat (default), 2 = rounded pills. Restart to apply.
    int topbar_buttons = 1;
    // Native header bar only: keep the window corners square instead of
    // matching the platform's rounding.
    bool topbar_sharp_corners = false;
    // Most recently opened repositories, newest first, capped at eight.
    std::vector<std::string> recent_repos; // newest first, kMaxRecentRepos long
};

// Puts `path` at the front of `settings.recent_repos`, deduplicated, capped.
void remember_recent_repo(Settings& settings, const std::string& path);

// <config-base>/repomancer — %APPDATA% on Windows,
// ~/Library/Application Support on macOS, $XDG_CONFIG_HOME or ~/.config
// elsewhere.
[[nodiscard]] std::filesystem::path default_config_dir();

[[nodiscard]] Settings load_settings(const std::filesystem::path& config_dir = default_config_dir());

// Best-effort atomic (write temp file, then rename). Returns false on I/O error.
bool save_settings(const Settings& settings,
                   const std::filesystem::path& config_dir = default_config_dir());

} // namespace repomancer
