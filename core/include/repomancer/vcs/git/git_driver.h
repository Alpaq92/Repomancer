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

// §13.1 repo trust gate. A repository the user has not vouched for runs
// READ-ONLY: every mutating driver method refuses up front, and the
// invocations neutralize exec-capable config (hooks, ssh command,
// credential helpers) as defense in depth.
enum class RepoTrust { Trusted, ReadOnly };

struct GitConfig {
    // Resolved from Preferences → VCS Providers; "git" means PATH lookup.
    std::filesystem::path binary = "git";
    std::chrono::milliseconds timeout{std::chrono::milliseconds(60'000)};
    ParseLimits limits{};
    RepoTrust trust = RepoTrust::Trusted;
    // §13.1: extra "-c key=" argument pairs appended when trust==ReadOnly,
    // computed once per repo from its OWN config — the content/diff filter
    // drivers it declares, whose names are arbitrary and so cannot be a
    // fixed list. Populated via read_only_overrides() before loading.
    std::vector<std::string> extra_neutralize;
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

    // §13.1: the "-c filter.<d>.clean=" (and .smudge/.process) plus
    // "-c diff.<d>.command=/.textconv=" overrides that neutralize the
    // program-naming drivers declared in this repository's OWN (--local)
    // config. Reading config executes nothing; the user's global drivers
    // (e.g. git-lfs) are untouched. Store the result in
    // GitConfig::extra_neutralize before opening an untrusted repo.
    [[nodiscard]] VcsResult<std::vector<std::string>>
    read_only_overrides(const std::filesystem::path& repo) const;

    // The unstaged patch (index vs worktree), optionally scoped to one
    // path. Untracked files produce no output. This is the base hunk
    // staging/discard operate on — see apply_patch.
    [[nodiscard]] VcsResult<std::vector<FileDiff>> worktree_diff(
        const std::filesystem::path& repo, const std::string& path = {},
        int context_lines = 3) const;

    // The staged patch (HEAD vs index), optionally scoped to one path.
    // Hunk UN-staging operates on this base.
    [[nodiscard]] VcsResult<std::vector<FileDiff>> staged_diff(
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

    // Remote synchronization. All three run prompt-free
    // (GIT_TERMINAL_PROMPT=0): missing credentials fail fast with git's own
    // message rather than hanging on a hidden prompt. `pull` is --ff-only —
    // a divergent branch is surfaced, never silently merged.
    [[nodiscard]] VcsResult<std::string> fetch(const std::filesystem::path& repo) const;
    [[nodiscard]] VcsResult<std::string> pull(const std::filesystem::path& repo) const;
    [[nodiscard]] VcsResult<std::string> push(const std::filesystem::path& repo) const;

    // Stashes the tracked changes (stash push). The message is argv-safe:
    // it rides as -m's own argument, which git consumes whole.
    [[nodiscard]] VcsResult<std::string> stash_save(const std::filesystem::path& repo,
                                                    const std::string& message = {}) const;

    // Re-applies and drops the newest stash (stash pop).
    [[nodiscard]] VcsResult<std::string> stash_pop(const std::filesystem::path& repo) const;

    // Creates a lightweight tag at `rev` (HEAD when empty). The name goes
    // behind --end-of-options; the rev is program-controlled.
    [[nodiscard]] VcsResult<std::string> create_tag(const std::filesystem::path& repo,
                                                    const std::string& name,
                                                    const std::string& rev = {}) const;

    // Applies a patch (as produced by single_hunk_patch) to the index
    // (`cached` — staging) or the working tree; `reverse` un-applies it
    // (hunk discard). The patch travels on stdin, never argv.
    [[nodiscard]] VcsResult<std::string> apply_patch(const std::filesystem::path& repo,
                                                     const std::string& patch,
                                                     bool cached, bool reverse) const;

    // Restores one tracked path to HEAD in both the index and the working
    // tree — the whole-file discard. Untracked files are the caller's to
    // delete; restore has no source for them.
    [[nodiscard]] VcsResult<std::string> discard_file(const std::filesystem::path& repo,
                                                      const std::string& path) const;

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
