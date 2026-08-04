// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Application settings: a JSON file in the per-user config directory
// (implementation-plan.md §8 — JSON over wxConfig: portable, diffable).
// Strict parsing with a size cap; unknown/invalid content degrades to
// defaults, never to an error dialog.

#pragma once

#include <filesystem>
#include <string>

namespace repomancer {

struct Settings {
    // "system" | "light" | "dark"
    std::string theme = "system";
};

// <config-base>/repomancer — %APPDATA% on Windows,
// ~/Library/Application Support on macOS, $XDG_CONFIG_HOME or ~/.config
// elsewhere.
[[nodiscard]] std::filesystem::path default_config_dir();

[[nodiscard]] Settings load_settings(const std::filesystem::path& config_dir = default_config_dir());

// Best-effort atomic (write temp file, then rename). Returns false on I/O error.
bool save_settings(const Settings& settings,
                   const std::filesystem::path& config_dir = default_config_dir());

} // namespace repomancer
