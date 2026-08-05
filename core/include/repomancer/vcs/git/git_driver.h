// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Git backend driving the locally installed `git` binary through plumbing
// interfaces only (implementation-plan.md §3.2). Every invocation goes
// through one argument builder so the §13.1 rules (config neutralization,
// `--end-of-options`) hold everywhere.

#pragma once

#include <repomancer/process/process_runner.h>
#include <repomancer/vcs/blame.h>
#include <repomancer/vcs/diff.h>
#include <repomancer/vcs/provider.h>
#include <repomancer/vcs/refs.h>
#include <repomancer/vcs/stats.h>

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

    // The working tree's combined (staged + unstaged) patch against HEAD,
    // optionally scoped to one path. Untracked files produce no output.
    [[nodiscard]] VcsResult<std::vector<FileDiff>> worktree_diff(
        const std::filesystem::path& repo, const std::string& path = {},
        int context_lines = 3) const;

    // Stages one path (add). The path rides behind --end-of-options alone:
    // add's grammar has no revision, so a following "--" would itself be
    // taken as a pathspec.
    [[nodiscard]] VcsResult<std::string> stage(const std::filesystem::path& repo,
                                               const std::string& path) const;

    // Removes one path from the index (restore --staged), same guard shape.
    [[nodiscard]] VcsResult<std::string> unstage(const std::filesystem::path& repo,
                                                 const std::string& path) const;

    // Commits the staged changes. The message travels on stdin (--file=-),
    // never argv (§13.1).
    [[nodiscard]] VcsResult<std::string> commit(const std::filesystem::path& repo,
                                                const std::string& message) const;

    // Checks out `branch` (switch). Fails cleanly when the working tree is
    // in the way; the caller surfaces git's own message.
    [[nodiscard]] VcsResult<std::string> switch_branch(const std::filesystem::path& repo,
                                                       const std::string& branch) const;

    // Creates `branch` at HEAD; switches to it when `checkout` is set.
    [[nodiscard]] VcsResult<std::string> create_branch(const std::filesystem::path& repo,
                                                       const std::string& branch,
                                                       bool checkout) const;

    // Line attribution of `path` at `rev` (blame --line-porcelain).
    [[nodiscard]] VcsResult<std::vector<BlameLine>> blame(
        const std::filesystem::path& repo, const std::string& path,
        const std::string& rev = "HEAD") const;

    [[nodiscard]] VcsResult<StatusSnapshot>
    status(const std::filesystem::path& repo) const override;

    [[nodiscard]] VcsResult<std::vector<Commit>>
    log(const std::filesystem::path& repo, const LogOptions& options) const override;

    // Every branch, remote branch, tag and the stash, for the sidebar.
    [[nodiscard]] VcsResult<std::vector<Ref>> refs(const std::filesystem::path& repo) const;

    // Commit counts per author across every ref, most prolific first.
    [[nodiscard]] VcsResult<std::vector<Contributor>>
    contributors(const std::filesystem::path& repo) const;

    // Share of tracked bytes per language at the given revision.
    [[nodiscard]] VcsResult<std::vector<LanguageStat>>
    languages(const std::filesystem::path& repo, const std::string& rev = "HEAD") const;

    // Files a commit touched. Merges are diffed against their first parent —
    // git shows nothing for them otherwise — and the root commit against the
    // empty tree, so every commit in the log yields something to inspect.
    [[nodiscard]] VcsResult<std::vector<ChangedFile>>
    changed_files(const std::filesystem::path& repo, const std::string& commit) const;

    // Patch for one path of one commit. An empty path diffs the whole commit.
    [[nodiscard]] VcsResult<std::vector<FileDiff>>
    file_diff(const std::filesystem::path& repo, const std::string& commit,
              const std::string& path, int context_lines = 3) const;

private:
    [[nodiscard]] proc::RunSpec make_spec(const std::filesystem::path* repo,
                                          std::vector<std::string> subcommand_args) const;

    GitConfig config_;
};

} // namespace repomancer::vcs::git
