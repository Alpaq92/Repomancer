// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/vcs/git/log_format.h>

#include <array>
#include "../parse_util.h"

namespace repomancer::vcs::git {

namespace {

constexpr std::array<std::string_view, 11> kFields = {
    "%H", "%P", "%at", "%an", "%ae", "%ct", "%cn", "%ce", "%D", "%s", "%b",
};
constexpr std::size_t kFixedFieldCount = kFields.size() - 1; // %b is the remainder

std::vector<std::string> split_ws(std::string_view text) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t space = text.find(' ', start);
        if (space == std::string_view::npos) {
            parts.emplace_back(text.substr(start));
            break;
        }
        if (space > start) {
            parts.emplace_back(text.substr(start, space - start));
        }
        start = space + 1;
    }
    return parts;
}

} // namespace

std::string log_format_argument() {
    std::string format = "--format=";
    for (std::size_t i = 0; i < kFields.size(); ++i) {
        if (i > 0) {
            format.push_back(kFieldSep);
        }
        format.append(kFields[i]);
    }
    return format;
}

std::vector<std::string> build_log_args(const LogOptions& options) {
    std::vector<std::string> args = {
        "log",
        "--topo-order",
        "-z",
        "--max-count=" + std::to_string(options.max_count),
        log_format_argument(),
    };
    if (options.all_refs) {
        args.emplace_back("--all");
    }
    if (!options.path.empty()) {
        // One path, renames followed — the shape `--follow` requires.
        args.emplace_back("--follow");
    }
    // §13.1: nothing after this point is ever parsed as an option.
    args.emplace_back("--end-of-options");
    if (!options.all_refs) {
        args.push_back(options.rev);
    }
    args.emplace_back("--");
    if (!options.path.empty()) {
        args.push_back(options.path);
    }
    return args;
}

VcsResult<std::vector<Commit>> parse_log_z(std::string_view data, const ParseLimits& limits) {
    std::vector<Commit> commits;

    std::size_t start = 0;
    while (start < data.size()) {
        std::size_t nul = data.find('\0', start);
        if (nul == std::string_view::npos) {
            nul = data.size();
        }
        const std::string_view record = data.substr(start, nul - start);
        start = nul + 1;

        if (record.empty()) {
            continue;
        }
        if (record.size() > limits.max_record_bytes) {
            return VcsError{VcsError::Kind::LimitExceeded, "log record exceeds size limit"};
        }
        if (commits.size() >= limits.max_entries) {
            return VcsError{VcsError::Kind::LimitExceeded, "too many log records"};
        }

        // Split off the fixed fields; the remainder after the last separator
        // is the body and may legally contain anything (including 0x1F).
        std::array<std::string_view, kFixedFieldCount> fields{};
        std::string_view rest = record;
        bool malformed = false;
        for (std::size_t i = 0; i < kFixedFieldCount; ++i) {
            const std::size_t sep = rest.find(kFieldSep);
            if (sep == std::string_view::npos) {
                malformed = true;
                break;
            }
            fields[i] = rest.substr(0, sep);
            rest.remove_prefix(sep + 1);
        }
        if (malformed) {
            return VcsError{VcsError::Kind::ParseError, "log record has too few fields"};
        }

        Commit commit;
        commit.hash = std::string(fields[0]);
        commit.parents = split_ws(fields[1]);
        if (!parse_number(fields[2], commit.author_time)) {
            return VcsError{VcsError::Kind::ParseError, "bad author timestamp"};
        }
        commit.author_name = std::string(fields[3]);
        commit.author_email = std::string(fields[4]);
        if (!parse_number(fields[5], commit.commit_time)) {
            return VcsError{VcsError::Kind::ParseError, "bad committer timestamp"};
        }
        commit.committer_name = std::string(fields[6]);
        commit.committer_email = std::string(fields[7]);
        commit.refs = std::string(fields[8]);
        commit.subject = std::string(fields[9]);
        commit.body = std::string(rest);
        commits.push_back(std::move(commit));
    }
    return commits;
}

} // namespace repomancer::vcs::git
