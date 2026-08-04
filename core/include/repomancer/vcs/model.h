// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// VCS-agnostic data model shared by drivers, GUI, and statusd.

#pragma once

#include <repomancer/outcome.h>

#include <cstdint>
#include <string>
#include <vector>

namespace repomancer::vcs {

struct VcsError {
    enum class Kind {
        ExeNotFound,   // configured binary missing
        LaunchFailed,  // could not spawn
        NonZeroExit,   // tool ran and reported failure
        Timeout,
        Cancelled,
        ParseError,    // tool output did not match the machine format
        LimitExceeded, // §13.2 parser limits tripped
    };

    VcsError() = default;
    VcsError(Kind k, std::string msg, int exit = 0, std::string stderr_ex = {})
        : kind(k), message(std::move(msg)), exit_code(exit),
          stderr_excerpt(std::move(stderr_ex)) {}

    Kind kind{};
    std::string message;
    int exit_code = 0;
    std::string stderr_excerpt; // truncated; for diagnostics, never parsed
};

template <typename T>
using VcsResult = Outcome<T, VcsError>;

// Hard caps applied while parsing tool output (implementation-plan.md §13.2).
struct ParseLimits {
    std::size_t max_entries = 200'000;
    std::size_t max_record_bytes = 65'536;
};

struct Commit {
    std::string hash;
    std::vector<std::string> parents;
    std::string author_name;
    std::string author_email;
    std::int64_t author_time = 0; // unix epoch seconds
    std::string committer_name;
    std::string committer_email;
    std::int64_t commit_time = 0;
    std::string refs; // raw %D decoration string, e.g. "HEAD -> main, tag: v1"
    std::string subject;
    std::string body;
};

struct LogOptions {
    std::size_t max_count = 1000;
    std::string rev = "HEAD";
};

enum class EntryKind {
    Ordinary,      // '1' porcelain records
    RenamedCopied, // '2'
    Unmerged,      // 'u'
    Untracked,     // '?'
    Ignored,       // '!'
};

struct StatusEntry {
    EntryKind kind{};
    char x = '.'; // staged (index) state, git porcelain XY notation
    char y = '.'; // worktree state
    std::string submodule; // 4-char porcelain submodule field, "N..." when not one
    int rename_score = 0;  // RenamedCopied only
    std::string path;
    std::string orig_path; // RenamedCopied only
};

struct BranchInfo {
    std::string oid;      // "(initial)" before first commit
    std::string head;     // branch name or "(detached)"
    std::string upstream; // empty when none
    int ahead = 0;
    int behind = 0;
};

struct StatusSnapshot {
    BranchInfo branch;
    std::vector<StatusEntry> entries;
};

} // namespace repomancer::vcs
