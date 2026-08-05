// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/vcs/git/git_driver.h>

#include <repomancer/vcs/git/log_format.h>
#include <repomancer/vcs/git/porcelain.h>

#include <charconv>
#include <optional>
#include <string_view>

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

// §13.1: mutations are refused outright for read-only repositories; the
// error carries no git output because git was never run.
std::optional<VcsError> refuse_if_read_only(RepoTrust trust) {
    if (trust == RepoTrust::ReadOnly) {
        return VcsError{VcsError::Kind::UntrustedRepo,
                        "repository is open read-only (untrusted)"};
    }
    return std::nullopt;
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
        // Raw UTF-8 paths in diff headers: the quoted/escaped form would
        // break path round-tripping (hunk staging) for non-ASCII names.
        "-c", "core.quotepath=off",
    };
    if (config_.trust == RepoTrust::ReadOnly) {
        // §13.1: an untrusted repository's own config must not name
        // programs to execute. Mutations are refused before reaching here
        // (guard_writable); these overrides are defense in depth for the
        // read paths. hooksPath points at an empty directory that exists
        // everywhere — an empty value would mean "the default .git/hooks".
        const char* neutralized[] = {
            "-c", "core.hooksPath=/var/empty",
            "-c", "core.sshCommand=false",
            "-c", "credential.helper=",
            "-c", "diff.external=",
            "-c", "core.pager=cat",
        };
        spec.args.insert(spec.args.end(), std::begin(neutralized),
                         std::end(neutralized));
        spec.args.insert(spec.args.end(), config_.extra_neutralize.begin(),
                         config_.extra_neutralize.end());
    }
    spec.args.insert(spec.args.end(), std::make_move_iterator(subcommand_args.begin()),
                     std::make_move_iterator(subcommand_args.end()));
    spec.env_extra = {
        {"GIT_TERMINAL_PROMPT", "0"},
        {"LC_ALL", "C"},
        // Every pathspec this driver passes is a literal path lifted from
        // git's own porcelain. Without this, a hostile file named e.g.
        // ":(glob)*" acts as pathspec magic — `restore` on it would reset
        // the entire working tree (§13.1).
        {"GIT_LITERAL_PATHSPECS", "1"},
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

VcsResult<std::vector<std::string>>
GitDriver::read_only_overrides(const std::filesystem::path& repo) const {
    // --local: only THIS repo's own declarations, so the user's trusted
    // global drivers (git-lfs, etc.) keep working. -z: NUL between records,
    // newline between key and value — a value may itself contain newlines.
    const auto run = proc::ProcessRunner::run(
        make_spec(&repo, {"config", "--list", "--local", "-z"}));
    if (!run.ok()) {
        // No local config is not an error worth failing the open over; the
        // fixed neutralization set still applies.
        return std::vector<std::string>{};
    }
    std::vector<std::string> overrides;
    std::string_view data = run.out;
    while (!data.empty()) {
        const std::size_t nul = data.find('\0');
        std::string_view record = data.substr(0, nul);
        data = nul == std::string_view::npos ? std::string_view{}
                                             : data.substr(nul + 1);
        const std::size_t nl = record.find('\n');
        const std::string_view key = record.substr(0, nl);
        // filter.<d>.clean/.smudge/.process and diff.<d>.command/.textconv
        // are the program-naming keys. (--no-ext-diff/--no-textconv already
        // cover the diff.* pair on our patch invocations; overriding them
        // here too closes any read path that forgets the flag.)
        const auto ends_with = [key](std::string_view suffix) {
            return key.size() > suffix.size() &&
                   key.substr(key.size() - suffix.size()) == suffix;
        };
        const bool is_filter =
            key.substr(0, 7) == "filter." &&
            (ends_with(".clean") || ends_with(".smudge") || ends_with(".process"));
        const bool is_diff =
            key.substr(0, 5) == "diff." &&
            (ends_with(".command") || ends_with(".textconv"));
        if (is_filter || is_diff) {
            overrides.push_back("-c");
            overrides.push_back(std::string(key) + "=");
        }
    }
    return overrides;
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
                                     "--no-ext-diff",
                                     "--no-textconv",
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
    if (auto refused = refuse_if_read_only(config_.trust)) {
        return *refused;
    }
    return run_to_string(
        proc::ProcessRunner::run(make_spec(&repo, {"add", "--end-of-options", path})));
}

VcsResult<std::string> GitDriver::unstage(const std::filesystem::path& repo,
                                          const std::string& path) const {
    if (auto refused = refuse_if_read_only(config_.trust)) {
        return *refused;
    }
    return run_to_string(proc::ProcessRunner::run(
        make_spec(&repo, {"restore", "--staged", "--end-of-options", path})));
}

VcsResult<std::string> GitDriver::commit(const std::filesystem::path& repo,
                                         const std::string& message) const {
    if (auto refused = refuse_if_read_only(config_.trust)) {
        return *refused;
    }
    auto spec = make_spec(&repo, {"commit", "--file=-"});
    spec.stdin_data = message;
    return run_to_string(proc::ProcessRunner::run(spec));
}

VcsResult<std::string> GitDriver::switch_branch(const std::filesystem::path& repo,
                                                const std::string& branch) const {
    if (auto refused = refuse_if_read_only(config_.trust)) {
        return *refused;
    }
    return run_to_string(proc::ProcessRunner::run(
        make_spec(&repo, {"switch", "--end-of-options", branch})));
}

VcsResult<std::string> GitDriver::create_branch(const std::filesystem::path& repo,
                                                const std::string& branch,
                                                bool checkout) const {
    if (auto refused = refuse_if_read_only(config_.trust)) {
        return *refused;
    }
    if (checkout) {
        return run_to_string(proc::ProcessRunner::run(
            make_spec(&repo, {"switch", "-c", "--end-of-options", branch})));
    }
    return run_to_string(proc::ProcessRunner::run(
        make_spec(&repo, {"branch", "--end-of-options", branch})));
}

VcsResult<std::string> GitDriver::fetch(const std::filesystem::path& repo) const {
    if (auto refused = refuse_if_read_only(config_.trust)) {
        return *refused;
    }
    return run_to_string(
        proc::ProcessRunner::run(make_spec(&repo, {"fetch", "--all", "--prune"})));
}

VcsResult<std::string> GitDriver::pull(const std::filesystem::path& repo) const {
    if (auto refused = refuse_if_read_only(config_.trust)) {
        return *refused;
    }
    return run_to_string(
        proc::ProcessRunner::run(make_spec(&repo, {"pull", "--ff-only"})));
}

VcsResult<std::string> GitDriver::push(const std::filesystem::path& repo) const {
    if (auto refused = refuse_if_read_only(config_.trust)) {
        return *refused;
    }
    return run_to_string(proc::ProcessRunner::run(make_spec(&repo, {"push"})));
}

VcsResult<std::string> GitDriver::stash_save(const std::filesystem::path& repo,
                                             const std::string& message) const {
    if (auto refused = refuse_if_read_only(config_.trust)) {
        return *refused;
    }
    std::vector<std::string> args = {"stash", "push"};
    if (!message.empty()) {
        args.push_back("-m");
        args.push_back(message);
    }
    return run_to_string(proc::ProcessRunner::run(make_spec(&repo, std::move(args))));
}

VcsResult<std::string> GitDriver::stash_pop(const std::filesystem::path& repo) const {
    if (auto refused = refuse_if_read_only(config_.trust)) {
        return *refused;
    }
    return run_to_string(proc::ProcessRunner::run(make_spec(&repo, {"stash", "pop"})));
}

VcsResult<std::string> GitDriver::create_tag(const std::filesystem::path& repo,
                                             const std::string& name,
                                             const std::string& rev) const {
    if (auto refused = refuse_if_read_only(config_.trust)) {
        return *refused;
    }
    std::vector<std::string> args = {"tag", "--end-of-options", name};
    if (!rev.empty()) {
        args.push_back(rev);
    }
    return run_to_string(proc::ProcessRunner::run(make_spec(&repo, std::move(args))));
}

VcsResult<std::vector<FileDiff>> GitDriver::worktree_diff(const std::filesystem::path& repo,
                                                          const std::string& path,
                                                          int context_lines) const {
    // Unstaged changes: index vs worktree. This exact base is what makes
    // hunk operations always applicable — `apply --cached` matches the
    // preimage (the index) and `apply --reverse` matches the postimage (the
    // worktree), regardless of what is already staged.
    std::vector<std::string> args = {"diff",
                                     "--no-color",
                                     "--no-ext-diff",
                                     "--no-textconv",
                                     "--unified=" + std::to_string(context_lines),
                                     "--find-renames",
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

VcsResult<std::vector<FileDiff>> GitDriver::staged_diff(const std::filesystem::path& repo,
                                                        const std::string& path,
                                                        int context_lines) const {
    // Staged changes: HEAD vs index. `apply --cached --reverse` un-stages a
    // hunk of this diff — its postimage is the index, so it always matches.
    std::vector<std::string> args = {"diff",
                                     "--cached",
                                     "--no-color",
                                     "--no-ext-diff",
                                     "--no-textconv",
                                     "--unified=" + std::to_string(context_lines),
                                     "--find-renames",
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

VcsResult<std::string> GitDriver::apply_patch(const std::filesystem::path& repo,
                                              const std::string& patch, bool cached,
                                              bool reverse) const {
    if (auto refused = refuse_if_read_only(config_.trust)) {
        return *refused;
    }
    std::vector<std::string> args = {"apply"};
    if (cached) {
        args.push_back("--cached");
    }
    if (reverse) {
        args.push_back("--reverse");
    }
    args.push_back("-"); // the patch body arrives on stdin
    auto spec = make_spec(&repo, std::move(args));
    spec.stdin_data = patch;
    return run_to_string(proc::ProcessRunner::run(spec));
}

VcsResult<std::string> GitDriver::discard_file(const std::filesystem::path& repo,
                                               const std::string& path) const {
    if (auto refused = refuse_if_read_only(config_.trust)) {
        return *refused;
    }
    // The path rides behind --end-of-options alone, as in stage()/unstage():
    // restore has no revision position there, so a following "--" would
    // itself be taken as a pathspec.
    return run_to_string(proc::ProcessRunner::run(
        make_spec(&repo, {"restore", "--source=HEAD", "--staged", "--worktree",
                          "--end-of-options", path})));
}

VcsResult<std::vector<BlameLine>> GitDriver::blame(const std::filesystem::path& repo,
                                                   const std::string& path,
                                                   const std::string& rev) const {
    // No --end-of-options here: blame's rev/pathspec grammar rejects it
    // ("bad revision" for the path). The rev is always program-controlled,
    // and the "--" keeps the attacker-influenced path out of option space.
    // --no-textconv is load-bearing: git blame otherwise honours a
    // diff.<driver>.textconv program from the (untrusted) repo's config.
    const auto run = proc::ProcessRunner::run(make_spec(
        &repo, {"blame", "--no-textconv", "--line-porcelain", rev, "--", path}));
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
