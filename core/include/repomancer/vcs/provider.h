// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// The backend abstraction (implementation-plan.md §3.1): the GUI talks to
// IVcsProvider + capability flags only — no `if (isGit)` outside drivers.

#pragma once

#include <repomancer/vcs/model.h>

#include <filesystem>
#include <string>
#include <vector>

namespace repomancer::vcs {

struct VcsCapabilities {
    bool staging = false; // index/staging area exists (git)
    bool stash = false;
    bool locking = false; // svn-style lock/unlock
};

class IVcsProvider {
public:
    virtual ~IVcsProvider() = default;

    [[nodiscard]] virtual std::string id() const = 0; // "git", "svn", "hg"
    [[nodiscard]] virtual VcsCapabilities capabilities() const = 0;

    [[nodiscard]] virtual VcsResult<StatusSnapshot>
    status(const std::filesystem::path& repo) const = 0;

    [[nodiscard]] virtual VcsResult<std::vector<Commit>>
    log(const std::filesystem::path& repo, const LogOptions& options) const = 0;
};

} // namespace repomancer::vcs
