// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/vcs/diff.h>

#include "parse_util.h"

namespace repomancer::vcs {

namespace {

VcsError parse_error(std::string message) {
    return VcsError{VcsError::Kind::ParseError, std::move(message)};
}

FileChange change_from_status(char status) {
    switch (status) {
    case 'A':
        return FileChange::Added;
    case 'M':
        return FileChange::Modified;
    case 'D':
        return FileChange::Deleted;
    case 'R':
        return FileChange::Renamed;
    case 'C':
        return FileChange::Copied;
    case 'T':
        return FileChange::TypeChanged;
    case 'U':
        return FileChange::Unmerged;
    default:
        break;
    }
    return FileChange::Unknown;
}

// Splits on NUL without producing a trailing empty record.
std::vector<std::string_view> split_z(std::string_view data) {
    std::vector<std::string_view> records;
    std::size_t start = 0;
    while (start < data.size()) {
        std::size_t nul = data.find('\0', start);
        if (nul == std::string_view::npos) {
            nul = data.size();
        }
        records.push_back(data.substr(start, nul - start));
        start = nul + 1;
    }
    return records;
}

// One line of the patch, without its terminator.
std::string_view next_line(std::string_view& data) {
    std::size_t eol = data.find('\n');
    if (eol == std::string_view::npos) {
        const std::string_view line = data;
        data = {};
        return line;
    }
    std::string_view line = data.substr(0, eol);
    data.remove_prefix(eol + 1);
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    return line;
}

// "a/path/to/file" → "path/to/file"; git uses /dev/null for creations and
// deletions, and quotes paths containing unusual bytes.
std::string strip_diff_prefix(std::string_view path) {
    if (path == "/dev/null") {
        return {};
    }
    if (path.size() > 2 && (path[0] == 'a' || path[0] == 'b') && path[1] == '/') {
        path.remove_prefix(2);
    }
    return std::string(path);
}

// "@@ -3,4 +3,5 @@ heading"
bool parse_hunk_header(std::string_view line, DiffHunk& hunk) {
    constexpr std::string_view kOpen = "@@ -";
    if (line.substr(0, kOpen.size()) != kOpen) {
        return false;
    }
    line.remove_prefix(kOpen.size());

    const auto take_range = [&line](int& start, int& count, char lead) {
        if (lead != '\0') {
            if (line.empty() || line.front() != lead) {
                return false;
            }
            line.remove_prefix(1);
        }
        std::size_t end = line.find_first_of(" ,");
        if (end == std::string_view::npos) {
            return false;
        }
        if (!parse_number(line.substr(0, end), start)) {
            return false;
        }
        count = 1; // a range without a count covers exactly one line
        if (line[end] == ',') {
            line.remove_prefix(end + 1);
            end = line.find(' ');
            if (end == std::string_view::npos || !parse_number(line.substr(0, end), count)) {
                return false;
            }
        }
        line.remove_prefix(end + 1);
        return true;
    };

    if (!take_range(hunk.old_start, hunk.old_count, '\0')) {
        return false;
    }
    if (!take_range(hunk.new_start, hunk.new_count, '+')) {
        return false;
    }
    constexpr std::string_view kClose = "@@";
    if (line.substr(0, kClose.size()) != kClose) {
        return false;
    }
    line.remove_prefix(kClose.size());
    if (!line.empty() && line.front() == ' ') {
        line.remove_prefix(1);
    }
    hunk.heading = std::string(line);
    return true;
}

} // namespace

std::string_view file_change_name(FileChange change) {
    switch (change) {
    case FileChange::Added:
        return "added";
    case FileChange::Modified:
        return "modified";
    case FileChange::Deleted:
        return "deleted";
    case FileChange::Renamed:
        return "renamed";
    case FileChange::Copied:
        return "copied";
    case FileChange::TypeChanged:
        return "type changed";
    case FileChange::Unmerged:
        return "unmerged";
    case FileChange::Unknown:
        break;
    }
    return "unknown";
}

VcsResult<std::vector<ChangedFile>> parse_name_status_z(std::string_view data,
                                                        const ParseLimits& limits) {
    std::vector<ChangedFile> files;
    const auto records = split_z(data);

    for (std::size_t i = 0; i < records.size(); ++i) {
        const std::string_view status = records[i];
        if (status.empty()) {
            continue;
        }
        if (files.size() >= limits.max_entries) {
            return VcsError{VcsError::Kind::LimitExceeded, "too many changed files"};
        }

        ChangedFile file;
        file.change = change_from_status(status.front());
        if (file.change == FileChange::Unknown) {
            return parse_error("unknown file status");
        }
        // R and C carry a similarity score and name both paths.
        const bool has_two_paths =
            file.change == FileChange::Renamed || file.change == FileChange::Copied;
        if (status.size() > 1 && !parse_number(status.substr(1), file.score)) {
            return parse_error("bad rename/copy score");
        }
        if (i + 1 >= records.size()) {
            return parse_error("status without a path");
        }
        if (has_two_paths) {
            if (i + 2 >= records.size()) {
                return parse_error("rename without both paths");
            }
            file.old_path = std::string(records[++i]);
            file.path = std::string(records[++i]);
        } else {
            file.path = std::string(records[++i]);
        }
        if (file.path.size() > limits.max_record_bytes) {
            return VcsError{VcsError::Kind::LimitExceeded, "path exceeds size limit"};
        }
        files.push_back(std::move(file));
    }
    return files;
}

VcsResult<std::vector<FileDiff>> parse_unified_diff(std::string_view data,
                                                    const ParseLimits& limits) {
    std::vector<FileDiff> files;
    FileDiff current;
    bool in_file = false;
    int old_lineno = 0;
    int new_lineno = 0;

    const auto flush = [&]() {
        if (in_file) {
            files.push_back(std::move(current));
            current = FileDiff{};
            in_file = false;
        }
    };

    while (!data.empty()) {
        const std::string_view line = next_line(data);
        if (line.size() > limits.max_record_bytes) {
            return VcsError{VcsError::Kind::LimitExceeded, "diff line exceeds size limit"};
        }

        if (line.substr(0, 11) == "diff --git ") {
            flush();
            in_file = true;
            continue;
        }
        if (!in_file) {
            continue; // preamble git may emit before the first file
        }
        if (files.size() >= limits.max_entries) {
            return VcsError{VcsError::Kind::LimitExceeded, "too many files in diff"};
        }

        if (line.substr(0, 4) == "--- ") {
            current.old_path = strip_diff_prefix(line.substr(4));
            continue;
        }
        if (line.substr(0, 4) == "+++ ") {
            current.new_path = strip_diff_prefix(line.substr(4));
            continue;
        }
        if (line.substr(0, 7) == "Binary " || line.substr(0, 21) == "GIT binary patch") {
            current.is_binary = true;
            continue;
        }
        if (line.substr(0, 2) == "@@") {
            DiffHunk hunk;
            if (!parse_hunk_header(line, hunk)) {
                return parse_error("malformed hunk header");
            }
            old_lineno = hunk.old_start;
            new_lineno = hunk.new_start;
            current.hunks.push_back(std::move(hunk));
            continue;
        }
        if (current.hunks.empty()) {
            continue; // extended headers: index, mode, rename from/to, …
        }

        DiffHunk& hunk = current.hunks.back();
        if (hunk.lines.size() >= limits.max_entries) {
            return VcsError{VcsError::Kind::LimitExceeded, "too many lines in a hunk"};
        }
        DiffLine entry;
        const char marker = line.empty() ? ' ' : line.front();
        const std::string_view text = line.empty() ? line : line.substr(1);
        switch (marker) {
        case '+':
            entry.kind = DiffLineKind::Added;
            entry.new_lineno = new_lineno++;
            break;
        case '-':
            entry.kind = DiffLineKind::Removed;
            entry.old_lineno = old_lineno++;
            break;
        case '\\':
            entry.kind = DiffLineKind::NoNewline;
            break;
        case ' ':
            entry.kind = DiffLineKind::Context;
            entry.old_lineno = old_lineno++;
            entry.new_lineno = new_lineno++;
            break;
        default:
            // Anything else ends the patch body for this file.
            continue;
        }
        entry.text = std::string(text);
        hunk.lines.push_back(std::move(entry));
    }
    flush();
    return files;
}

} // namespace repomancer::vcs
