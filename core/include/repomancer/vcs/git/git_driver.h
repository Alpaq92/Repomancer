// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Git backend driving the locally installed `git` binary through plumbing
// interfaces only (implementation-plan.md §3.2). Every invocation goes
// through one argument builder so the §13.1 rules (config neutralization,
// `--end-of-options`) hold everywhere.

#pragma once

#include <repomancer/process/process_runner.h>
#include <repomancer/vcs/provider.h>

#include <chrono>
#include <filesystem>

namespace repomancer::vcs::git {

struct GitConfig {
    // Resolved from Preferences → VCS Providers; "git" means PATH lookup.
    std::filesystem::path binary = "git";
    std::chrono::milliseconds timeout{std::chrono::milliseconds(60'000)};
    ParseLimits limits{};
};

struct GitVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::string raw;
};

class GitDriver final : public IVcsProvider {
public:
    explicit GitDriver(GitConfig config = {});

    [[nodiscard]] std::string id() const override { return "git"; }
    [[nodiscard]] VcsCapabilities capabilities() const override;

    [[nodiscard]] VcsResult<GitVersion> version() const;

    [[nodiscard]] VcsResult<StatusSnapshot>
    status(const std::filesystem::path& repo) const override;

    [[nodiscard]] VcsResult<std::vector<Commit>>
    log(const std::filesystem::path& repo, const LogOptions& options) const override;

private:
    [[nodiscard]] proc::RunSpec make_spec(const std::filesystem::path* repo,
                                          std::vector<std::string> subcommand_args) const;

    GitConfig config_;
};

} // namespace repomancer::vcs::git
