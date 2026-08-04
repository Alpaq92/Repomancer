// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// The repository's refs, as the sidebar shows them.

#pragma once

#include <repomancer/vcs/model.h>

#include <string>
#include <string_view>
#include <vector>

namespace repomancer::vcs {

enum class RefKind {
    LocalBranch,
    RemoteBranch,
    Tag,
    Stash,
    Other, // notes, replace, bisect, …
};

struct Ref {
    RefKind kind = RefKind::Other;
    std::string full_name;  // refs/heads/main
    std::string short_name; // main
    std::string target;     // object id
    std::string upstream;   // short upstream name, when tracking one
    bool is_head = false;
};

[[nodiscard]] std::string_view ref_kind_name(RefKind kind);

// `git for-each-ref` with NUL-separated fields:
// refname, objectname, HEAD marker, upstream, objecttype — five per record.
VcsResult<std::vector<Ref>> parse_for_each_ref_z(std::string_view data,
                                                 const ParseLimits& limits = {});

} // namespace repomancer::vcs
