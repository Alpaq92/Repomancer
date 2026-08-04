// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Unified-diff model and parser. The patch text comes from the VCS itself
// (git show -p, hg diff, svn diff all emit unified diffs), so one parser
// serves every backend; the in-app diff engine (implementation-plan.md §4.1)
// is only needed where there is no patch to parse — unsaved buffers, hunk
// staging arithmetic and three-way merge.

#pragma once

#include <repomancer/vcs/model.h>

#include <string>
#include <string_view>
#include <vector>

namespace repomancer::vcs {

enum class FileChange {
    Added,
    Modified,
    Deleted,
    Renamed,
    Copied,
    TypeChanged,
    Unmerged,
    Unknown,
};

[[nodiscard]] std::string_view file_change_name(FileChange change);

struct ChangedFile {
    FileChange change = FileChange::Unknown;
    std::string path;
    std::string old_path; // renames and copies only
    int score = 0;        // similarity index for renames/copies
};

enum class DiffLineKind {
    Context,
    Added,
    Removed,
    NoNewline, // "\ No newline at end of file"
};

struct DiffLine {
    DiffLineKind kind = DiffLineKind::Context;
    int old_lineno = 0; // 0 when the line does not exist on that side
    int new_lineno = 0;
    std::string text;   // without the leading marker
};

struct DiffHunk {
    int old_start = 0;
    int old_count = 0;
    int new_start = 0;
    int new_count = 0;
    std::string heading; // the function context git puts after the second @@
    std::vector<DiffLine> lines;
};

struct FileDiff {
    std::string old_path;
    std::string new_path;
    bool is_binary = false;
    std::vector<DiffHunk> hunks;
};

// Parses `git diff-tree -z --name-status` output: a status field and a path
// per record, with renames and copies spanning three records.
VcsResult<std::vector<ChangedFile>> parse_name_status_z(std::string_view data,
                                                        const ParseLimits& limits = {});

// Parses unified diff text, which may cover several files.
VcsResult<std::vector<FileDiff>> parse_unified_diff(std::string_view data,
                                                    const ParseLimits& limits = {});

} // namespace repomancer::vcs
