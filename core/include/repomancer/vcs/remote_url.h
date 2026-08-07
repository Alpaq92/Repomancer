// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Parsing of git remote URLs. The SSH wizard uses the host to default its
// target, and the forge integration (implementation-plan.md §6) needs the
// owner/repo pair; both read the same parse rather than each inventing one.

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace repomancer::vcs {

struct RemoteUrl {
    std::string host;  // "github.com"
    std::string user;  // SSH login, usually "git"; empty for https
    std::string owner; // "Alpaq92" — the first path segment, empty if absent
    std::string repo;  // "Repomancer", with any .git suffix removed
    int port = 0;      // explicit port, 0 when the scheme's default applies
    bool ssh = false;  // reached over SSH (scp-like or ssh://)
};

// Parse the forms git actually emits:
//   git@github.com:owner/repo.git            (scp-like)
//   ssh://git@github.com:2222/owner/repo.git
//   https://github.com/owner/repo.git        (with optional user@)
//   git://github.com/owner/repo.git
// Local paths and file:// URLs have no host and yield std::nullopt, as do
// strings that carry no host at all.
[[nodiscard]] std::optional<RemoteUrl> parse_remote_url(std::string_view url);

} // namespace repomancer::vcs
