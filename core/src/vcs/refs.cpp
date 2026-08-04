// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/vcs/refs.h>

namespace repomancer::vcs {

namespace {

struct KindRule {
    std::string_view prefix;
    RefKind kind;
};

// Order matters: refs/stash must be tested before the generic cases, and
// refs/remotes before refs/heads would be wrong since neither is a prefix of
// the other.
constexpr KindRule kRules[] = {
    {"refs/heads/", RefKind::LocalBranch},
    {"refs/remotes/", RefKind::RemoteBranch},
    {"refs/tags/", RefKind::Tag},
};

} // namespace

std::string_view ref_kind_name(RefKind kind) {
    switch (kind) {
    case RefKind::LocalBranch:
        return "branch";
    case RefKind::RemoteBranch:
        return "remote branch";
    case RefKind::Tag:
        return "tag";
    case RefKind::Stash:
        return "stash";
    case RefKind::Other:
        break;
    }
    return "ref";
}

VcsResult<std::vector<Ref>> parse_for_each_ref_z(std::string_view data,
                                                 const ParseLimits& limits) {
    constexpr std::size_t kFieldsPerRecord = 5;
    std::vector<Ref> refs;

    // Records are newline-terminated and their fields NUL-separated, so the
    // newline has to be peeled off first — splitting on NUL alone would glue
    // each record's last field to the next record's first one.
    std::size_t line_start = 0;
    while (line_start < data.size()) {
        std::size_t eol = data.find('\n', line_start);
        if (eol == std::string_view::npos) {
            eol = data.size();
        }
        std::string_view record = data.substr(line_start, eol - line_start);
        line_start = eol + 1;
        if (!record.empty() && record.back() == '\r') {
            record.remove_suffix(1);
        }
        if (record.empty()) {
            continue;
        }

        std::vector<std::string_view> fields;
        std::size_t start = 0;
        while (true) {
            const std::size_t nul = record.find('\0', start);
            if (nul == std::string_view::npos) {
                fields.push_back(record.substr(start));
                break;
            }
            fields.push_back(record.substr(start, nul - start));
            start = nul + 1;
        }
        if (fields.size() != kFieldsPerRecord) {
            return VcsError{VcsError::Kind::ParseError, "ref record has the wrong field count"};
        }
        if (refs.size() >= limits.max_entries) {
            return VcsError{VcsError::Kind::LimitExceeded, "too many refs"};
        }

        Ref ref;
        ref.full_name = std::string(fields[0]);
        ref.target = std::string(fields[1]);
        // %(HEAD) is "*" for the checked-out branch and a space otherwise.
        ref.is_head = fields[2] == "*";
        ref.upstream = std::string(fields[3]);

        if (ref.full_name.empty()) {
            return VcsError{VcsError::Kind::ParseError, "ref without a name"};
        }
        if (ref.full_name == "refs/stash") {
            ref.kind = RefKind::Stash;
            ref.short_name = "stash";
        } else {
            for (const auto& rule : kRules) {
                if (ref.full_name.rfind(rule.prefix, 0) == 0) {
                    ref.kind = rule.kind;
                    ref.short_name = ref.full_name.substr(rule.prefix.size());
                    break;
                }
            }
            if (ref.short_name.empty()) {
                ref.short_name = ref.full_name;
            }
        }
        if (ref.full_name.size() > limits.max_record_bytes) {
            return VcsError{VcsError::Kind::LimitExceeded, "ref name exceeds size limit"};
        }
        refs.push_back(std::move(ref));
    }
    return refs;
}

} // namespace repomancer::vcs
