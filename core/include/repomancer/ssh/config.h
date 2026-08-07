// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// ~/.ssh/config editing for the M3 key wizard (implementation-plan.md §7):
// route a host to a specific key by maintaining a clearly-delimited,
// Repomancer-owned block. We never parse or rewrite the user's own stanzas —
// only our own block is touched — and every edit backs the prior file up and
// hands back the full new content for a preview.

#pragma once

#include <repomancer/ssh/keys.h> // SshError / SshResult

#include <filesystem>
#include <optional>
#include <string>

namespace repomancer::ssh {

// The outcome of an edit: the full new file content (for a preview/diff) and
// where the previous content was saved (empty if the file was newly created).
struct ConfigEdit {
    std::string content;
    std::filesystem::path backup_path;
};

// Read the IdentityFile that Repomancer's managed block sets for `host`.
// std::nullopt when the file has no managed block for that host. Only our own
// block is consulted — user-authored stanzas are deliberately not parsed.
[[nodiscard]] SshResult<std::optional<std::filesystem::path>> config_identity(
    const std::filesystem::path& config_path, const std::string& host);

// Route `host` to `identity_file`: write (or replace) a managed block
//     # >>> repomancer:<host> >>>
//     Host <host>
//         IdentityFile <identity_file>
//         IdentitiesOnly yes
//     # <<< repomancer:<host> <<<
// leaving every other line untouched. The prior file is backed up first and
// the parent directory created. Returns the new content + backup path.
[[nodiscard]] SshResult<ConfigEdit> config_set_identity(
    const std::filesystem::path& config_path, const std::string& host,
    const std::filesystem::path& identity_file);

// Remove Repomancer's managed block for `host` (a no-op, still backed up, if
// absent). Returns the new content + backup path.
[[nodiscard]] SshResult<ConfigEdit> config_remove(
    const std::filesystem::path& config_path, const std::string& host);

} // namespace repomancer::ssh
