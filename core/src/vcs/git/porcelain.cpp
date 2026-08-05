// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/vcs/git/porcelain.h>

#include "../parse_util.h"
#include <vector>

namespace repomancer::vcs::git {

namespace {

VcsError parse_error(std::string message) {
    return VcsError{VcsError::Kind::ParseError, std::move(message)};
}

std::vector<std::string_view> split_records(std::string_view data) {
    std::vector<std::string_view> records;
    std::size_t start = 0;
    while (start < data.size()) {
        const std::size_t nul = data.find('\0', start);
        if (nul == std::string_view::npos) {
            records.push_back(data.substr(start));
            break;
        }
        records.push_back(data.substr(start, nul - start));
        start = nul + 1;
    }
    return records;
}

// Removes `count` space-separated tokens from the front of `rec`.
// Returns false if the record is too short.
bool take_tokens(std::string_view& rec, int count, std::vector<std::string_view>& tokens) {
    tokens.clear();
    for (int i = 0; i < count; ++i) {
        const std::size_t space = rec.find(' ');
        if (space == std::string_view::npos) {
            return false;
        }
        tokens.push_back(rec.substr(0, space));
        rec.remove_prefix(space + 1);
    }
    return true;
}

bool parse_branch_header(std::string_view rec, BranchInfo& branch) {
    constexpr std::string_view kOid = "# branch.oid ";
    constexpr std::string_view kHead = "# branch.head ";
    constexpr std::string_view kUpstream = "# branch.upstream ";
    constexpr std::string_view kAb = "# branch.ab ";

    if (rec.starts_with(kOid)) {
        branch.oid = std::string(rec.substr(kOid.size()));
        return true;
    }
    if (rec.starts_with(kHead)) {
        branch.head = std::string(rec.substr(kHead.size()));
        return true;
    }
    if (rec.starts_with(kUpstream)) {
        branch.upstream = std::string(rec.substr(kUpstream.size()));
        return true;
    }
    if (rec.starts_with(kAb)) {
        // "+<ahead> -<behind>"
        std::string_view rest = rec.substr(kAb.size());
        const std::size_t space = rest.find(' ');
        if (space == std::string_view::npos) {
            return false;
        }
        std::string_view ahead = rest.substr(0, space);
        std::string_view behind = rest.substr(space + 1);
        if (ahead.empty() || behind.empty() || ahead.front() != '+' || behind.front() != '-') {
            return false;
        }
        return parse_number(ahead.substr(1), branch.ahead) &&
               parse_number(behind.substr(1), branch.behind);
    }
    // Unknown headers are tolerated (forward compatibility).
    return true;
}

} // namespace

VcsResult<StatusSnapshot> parse_status_porcelain_v2z(std::string_view data,
                                                     const ParseLimits& limits) {
    StatusSnapshot snapshot;
    const auto records = split_records(data);
    std::vector<std::string_view> tokens;

    for (std::size_t i = 0; i < records.size(); ++i) {
        std::string_view rec = records[i];
        if (rec.empty()) {
            continue;
        }
        if (rec.size() > limits.max_record_bytes) {
            return VcsError{VcsError::Kind::LimitExceeded, "status record exceeds size limit"};
        }
        if (snapshot.entries.size() >= limits.max_entries) {
            return VcsError{VcsError::Kind::LimitExceeded, "too many status entries"};
        }

        const char type = rec.front();
        StatusEntry entry;
        switch (type) {
        case '#':
            if (!parse_branch_header(rec, snapshot.branch)) {
                return parse_error("malformed branch header");
            }
            continue;
        case '1': {
            // 1 <XY> <sub> <mH> <mI> <mW> <hH> <hI> <path>
            if (!take_tokens(rec, 8, tokens) || tokens[1].size() != 2 || rec.empty()) {
                return parse_error("malformed '1' record");
            }
            entry.kind = EntryKind::Ordinary;
            entry.x = tokens[1][0];
            entry.y = tokens[1][1];
            entry.submodule = std::string(tokens[2]);
            entry.path = std::string(rec);
            break;
        }
        case '2': {
            // 2 <XY> <sub> <mH> <mI> <mW> <hH> <hI> <Xscore> <path> NUL <origPath>
            if (!take_tokens(rec, 9, tokens) || tokens[1].size() != 2 || rec.empty()) {
                return parse_error("malformed '2' record");
            }
            entry.kind = EntryKind::RenamedCopied;
            entry.x = tokens[1][0];
            entry.y = tokens[1][1];
            entry.submodule = std::string(tokens[2]);
            std::string_view score = tokens[8];
            if (score.size() < 2 || !parse_number(score.substr(1), entry.rename_score)) {
                return parse_error("malformed rename score");
            }
            entry.path = std::string(rec);
            if (i + 1 >= records.size()) {
                return parse_error("rename record missing original path");
            }
            entry.orig_path = std::string(records[++i]);
            break;
        }
        case 'u': {
            // u <XY> <sub> <m1> <m2> <m3> <mW> <h1> <h2> <h3> <path>
            if (!take_tokens(rec, 10, tokens) || tokens[1].size() != 2 || rec.empty()) {
                return parse_error("malformed 'u' record");
            }
            entry.kind = EntryKind::Unmerged;
            entry.x = tokens[1][0];
            entry.y = tokens[1][1];
            entry.submodule = std::string(tokens[2]);
            entry.path = std::string(rec);
            break;
        }
        case '?':
        case '!': {
            if (rec.size() < 3 || rec[1] != ' ') {
                return parse_error("malformed untracked/ignored record");
            }
            entry.kind = (type == '?') ? EntryKind::Untracked : EntryKind::Ignored;
            entry.x = type;
            entry.y = type;
            entry.path = std::string(rec.substr(2));
            break;
        }
        default:
            return parse_error("unknown status record type");
        }
        snapshot.entries.push_back(std::move(entry));
    }
    return snapshot;
}

} // namespace repomancer::vcs::git
