// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/vcs/git/git_driver.h>

#include <repomancer/vcs/git/log_format.h>
#include <repomancer/vcs/git/porcelain.h>

#include <charconv>

namespace repomancer::vcs::git {

namespace {

constexpr std::size_t kStderrExcerptBytes = 2048;

std::string excerpt(const std::string& text) {
    return text.substr(0, kStderrExcerptBytes);
}

VcsError from_run_failure(const proc::RunResult& run) {
    using Kind = VcsError::Kind;
    switch (run.status) {
    case proc::LaunchStatus::ExeNotFound:
        return {Kind::ExeNotFound, "git binary not found", -1, excerpt(run.err)};
    case proc::LaunchStatus::TimedOut:
        return {Kind::Timeout, "git timed out", -1, excerpt(run.err)};
    case proc::LaunchStatus::Cancelled:
        return {Kind::Cancelled, "operation cancelled", -1, {}};
    case proc::LaunchStatus::LaunchFailed:
    case proc::LaunchStatus::IoError:
        return {Kind::LaunchFailed, "failed to run git", -1, excerpt(run.err)};
    case proc::LaunchStatus::Ok:
        break;
    }
    return {Kind::NonZeroExit, "git exited with an error", run.exit_code, excerpt(run.err)};
}

VcsResult<std::string> run_to_string(const proc::RunResult& run) {
    if (!run.ok()) {
        return from_run_failure(run);
    }
    return run.out;
}

} // namespace

GitDriver::GitDriver(GitConfig config) : config_(std::move(config)) {}

VcsCapabilities GitDriver::capabilities() const {
    VcsCapabilities caps;
    caps.staging = true;
    caps.stash = true;
    caps.locking = false;
    return caps;
}

proc::RunSpec GitDriver::make_spec(const std::filesystem::path* repo,
                                   std::vector<std::string> subcommand_args) const {
    proc::RunSpec spec;
    spec.exe = config_.binary;
    spec.timeout = config_.timeout;
    if (repo != nullptr) {
        spec.cwd = *repo;
    }
    // Machine-readable, prompt-free, contention-free invocations (§3.2).
    // core.fsmonitor is neutralized from day one: `status` in a hostile repo
    // would otherwise execute an attacker-supplied hook command (§13.1). The
    // full exec-capable-config neutralization set lands with the M2 trust
    // gate.
    spec.args = {
        "--no-optional-locks",
        "-c", "color.ui=false",
        "-c", "core.fsmonitor=false",
    };
    spec.args.insert(spec.args.end(), std::make_move_iterator(subcommand_args.begin()),
                     std::make_move_iterator(subcommand_args.end()));
    spec.env_extra = {
        {"GIT_TERMINAL_PROMPT", "0"},
        {"LC_ALL", "C"},
    };
    return spec;
}

VcsResult<GitVersion> GitDriver::version() const {
    const auto run = proc::ProcessRunner::run(make_spec(nullptr, {"--version"}));
    if (!run.ok()) {
        return from_run_failure(run);
    }
    // "git version 2.47.3" (possibly with a platform suffix)
    GitVersion version;
    version.raw = run.out;
    constexpr std::string_view kPrefix = "git version ";
    std::string_view text = run.out;
    const std::size_t at = text.find(kPrefix);
    if (at == std::string_view::npos) {
        return VcsError{VcsError::Kind::ParseError, "unrecognized `git --version` output"};
    }
    text.remove_prefix(at + kPrefix.size());
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    auto r1 = std::from_chars(begin, end, version.major);
    if (r1.ec != std::errc{} || r1.ptr == end || *r1.ptr != '.') {
        return VcsError{VcsError::Kind::ParseError, "unrecognized git version number"};
    }
    auto r2 = std::from_chars(r1.ptr + 1, end, version.minor);
    if (r2.ec != std::errc{}) {
        return VcsError{VcsError::Kind::ParseError, "unrecognized git version number"};
    }
    if (r2.ptr != end && *r2.ptr == '.') {
        std::from_chars(r2.ptr + 1, end, version.patch);
    }
    return version;
}

VcsResult<StatusSnapshot> GitDriver::status(const std::filesystem::path& repo) const {
    const auto run = proc::ProcessRunner::run(make_spec(
        &repo, {"status", "--porcelain=v2", "-z", "--branch", "--untracked-files=all"}));
    if (!run.ok()) {
        return from_run_failure(run);
    }
    return parse_status_porcelain_v2z(run.out, config_.limits);
}

VcsResult<std::vector<Ref>> GitDriver::refs(const std::filesystem::path& repo) const {
    const auto run = proc::ProcessRunner::run(make_spec(
        &repo, {"for-each-ref", "--sort=refname",
                "--format=%(refname)%00%(objectname)%00%(HEAD)%00%(upstream:short)%00%(objecttype)"}));
    if (!run.ok()) {
        return from_run_failure(run);
    }
    return parse_for_each_ref_z(run.out, config_.limits);
}

VcsResult<std::vector<Contributor>>
GitDriver::contributors(const std::filesystem::path& repo) const {
    const auto run = proc::ProcessRunner::run(
        make_spec(&repo, {"shortlog", "--summary", "--numbered", "--email", "--all"}));
    if (!run.ok()) {
        return from_run_failure(run);
    }
    return parse_shortlog(run.out, config_.limits);
}

VcsResult<std::vector<LanguageStat>> GitDriver::languages(const std::filesystem::path& repo,
                                                          const std::string& rev) const {
    const auto run = proc::ProcessRunner::run(
        make_spec(&repo, {"ls-tree", "-r", "-l", "--full-tree", "--end-of-options", rev}));
    if (!run.ok()) {
        return from_run_failure(run);
    }
    return parse_ls_tree_sizes(run.out, config_.limits);
}

VcsResult<std::vector<ChangedFile>>
GitDriver::changed_files(const std::filesystem::path& repo, const std::string& commit) const {
    const auto run = proc::ProcessRunner::run(
        make_spec(&repo, {"diff-tree", "-r", "-z", "--no-commit-id", "--name-status",
                          "--find-renames", "-m", "--first-parent", "--root",
                          "--end-of-options", commit}));
    if (!run.ok()) {
        return from_run_failure(run);
    }
    return parse_name_status_z(run.out, config_.limits);
}

VcsResult<std::vector<FileDiff>> GitDriver::file_diff(const std::filesystem::path& repo,
                                                      const std::string& commit,
                                                      const std::string& path,
                                                      int context_lines) const {
    std::vector<std::string> args = {"show",
                                     "--format=", // header suppressed; we want only the patch
                                     "--patch",
                                     "--find-renames",
                                     "--no-color",
                                     "--unified=" + std::to_string(context_lines),
                                     "--first-parent",
                                     "--end-of-options",
                                     commit,
                                     "--"};
    if (!path.empty()) {
        args.push_back(path);
    }
    const auto run = proc::ProcessRunner::run(make_spec(&repo, std::move(args)));
    if (!run.ok()) {
        return from_run_failure(run);
    }
    return parse_unified_diff(run.out, config_.limits);
}

VcsResult<std::string> GitDriver::stage(const std::filesystem::path& repo,
                                        const std::string& path) const {
    return run_to_string(
        proc::ProcessRunner::run(make_spec(&repo, {"add", "--end-of-options", path})));
}

VcsResult<std::string> GitDriver::unstage(const std::filesystem::path& repo,
                                          const std::string& path) const {
    return run_to_string(proc::ProcessRunner::run(
        make_spec(&repo, {"restore", "--staged", "--end-of-options", path})));
}

VcsResult<std::string> GitDriver::commit(const std::filesystem::path& repo,
                                         const std::string& message) const {
    auto spec = make_spec(&repo, {"commit", "--file=-"});
    spec.stdin_data = message;
    return run_to_string(proc::ProcessRunner::run(spec));
}

VcsResult<std::string> GitDriver::switch_branch(const std::filesystem::path& repo,
                                                const std::string& branch) const {
    return run_to_string(proc::ProcessRunner::run(
        make_spec(&repo, {"switch", "--end-of-options", branch})));
}

VcsResult<std::string> GitDriver::create_branch(const std::filesystem::path& repo,
                                                const std::string& branch,
                                                bool checkout) const {
    if (checkout) {
        return run_to_string(proc::ProcessRunner::run(
            make_spec(&repo, {"switch", "-c", "--end-of-options", branch})));
    }
    return run_to_string(proc::ProcessRunner::run(
        make_spec(&repo, {"branch", "--end-of-options", branch})));
}

VcsResult<std::vector<FileDiff>> GitDriver::worktree_diff(const std::filesystem::path& repo,
                                                          const std::string& path,
                                                          int context_lines) const {
    std::vector<std::string> args = {"diff",
                                     "--no-color",
                                     "--unified=" + std::to_string(context_lines),
                                     "--find-renames",
                                     "--end-of-options",
                                     "HEAD",
                                     "--"};
    if (!path.empty()) {
        args.push_back(path);
    }
    const auto run = proc::ProcessRunner::run(make_spec(&repo, std::move(args)));
    if (!run.ok()) {
        return from_run_failure(run);
    }
    return parse_unified_diff(run.out, config_.limits);
}

VcsResult<std::vector<BlameLine>> GitDriver::blame(const std::filesystem::path& repo,
                                                   const std::string& path,
                                                   const std::string& rev) const {
    // No --end-of-options here: blame's rev/pathspec grammar rejects it
    // ("bad revision" for the path). The rev is always program-controlled,
    // and the "--" keeps the attacker-influenced path out of option space.
    const auto run = proc::ProcessRunner::run(
        make_spec(&repo, {"blame", "--line-porcelain", rev, "--", path}));
    if (!run.ok()) {
        return from_run_failure(run);
    }
    return parse_blame_line_porcelain(run.out, config_.limits);
}

VcsResult<std::vector<Commit>> GitDriver::log(const std::filesystem::path& repo,
                                              const LogOptions& options) const {
    const auto run = proc::ProcessRunner::run(make_spec(&repo, build_log_args(options)));
    if (!run.ok()) {
        return from_run_failure(run);
    }
    return parse_log_z(run.out, config_.limits);
}

} // namespace repomancer::vcs::git
