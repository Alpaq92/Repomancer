// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/vcs/blame.h>

#include <cctype>
#include "parse_util.h"

namespace repomancer::vcs {

namespace {

bool is_hex40(std::string_view text) {
    if (text.size() != 40) {
        return false;
    }
    for (const char ch : text) {
        if (std::isxdigit(static_cast<unsigned char>(ch)) == 0) {
            return false;
        }
    }
    return true;
}

std::string_view first_token(std::string_view line) {
    const std::size_t space = line.find(' ');
    return space == std::string_view::npos ? line : line.substr(0, space);
}

} // namespace

VcsResult<std::vector<BlameLine>> parse_blame_line_porcelain(std::string_view data,
                                                            const ParseLimits& limits) {
    std::vector<BlameLine> lines;
    BlameLine current;
    bool in_record = false;
    std::size_t record_bytes = 0;

    std::size_t start = 0;
    while (start < data.size()) {
        std::size_t eol = data.find('\n', start);
        if (eol == std::string_view::npos) {
            eol = data.size();
        }
        const std::string_view line = data.substr(start, eol - start);
        start = eol + 1;

        record_bytes += line.size() + 1;
        if (record_bytes > limits.max_record_bytes) {
            return VcsError{VcsError::Kind::LimitExceeded, "blame record exceeds the size limit"};
        }

        if (!line.empty() && line.front() == '\t') {
            // The content line closes a record.
            if (!in_record) {
                return VcsError{VcsError::Kind::ParseError, "blame content without a header"};
            }
            current.content = std::string(line.substr(1));
            lines.push_back(std::move(current));
            if (lines.size() > limits.max_entries) {
                return VcsError{VcsError::Kind::LimitExceeded, "blame exceeds the entry limit"};
            }
            current = {};
            in_record = false;
            record_bytes = 0;
            continue;
        }

        if (const auto token = first_token(line); is_hex40(token)) {
            current.hash = std::string(token);
            in_record = true;
            continue;
        }
        constexpr std::string_view kAuthor = "author ";
        constexpr std::string_view kAuthorTime = "author-time ";
        if (line.substr(0, kAuthor.size()) == kAuthor) {
            current.author = std::string(line.substr(kAuthor.size()));
        } else if (line.substr(0, kAuthorTime.size()) == kAuthorTime) {
            const std::string_view value = line.substr(kAuthorTime.size());
            std::int64_t parsed = 0;
            if (parse_number(value, parsed)) {
                current.author_time = parsed;
            }
        }
        // Every other header (committer, summary, filename, …) is skipped.
    }

    if (in_record) {
        return VcsError{VcsError::Kind::ParseError, "blame ends inside a record"};
    }
    return lines;
}

} // namespace repomancer::vcs
