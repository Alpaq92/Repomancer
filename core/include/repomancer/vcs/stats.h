// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Repository-level summaries for the sidebar: who contributed, and what the
// tracked content is written in.

#pragma once

#include <repomancer/vcs/model.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace repomancer::vcs {

struct Contributor {
    std::string name;
    std::string email;
    int commits = 0;
};

struct LanguageStat {
    std::string name;
    std::uint64_t bytes = 0;
    double percent = 0.0;
};

// `git shortlog -sne`: a count, a tab, then "Name <email>", most commits
// first. Ordering is git's; it is not re-sorted here.
VcsResult<std::vector<Contributor>> parse_shortlog(std::string_view data,
                                                   const ParseLimits& limits = {});

// The language a path counts towards, by extension, or an empty view when the
// path is one we deliberately ignore (vendored trees, lock files) or do not
// recognise. This is an approximation of what a linguist would do: extensions
// only, no content sniffing.
[[nodiscard]] std::string_view language_for_path(std::string_view path);

// `git ls-tree -r -l`: "<mode> <type> <oid> <size>\t<path>" per line. Sizes are
// summed per language and returned largest first, with percentages of the
// classified total.
VcsResult<std::vector<LanguageStat>> parse_ls_tree_sizes(std::string_view data,
                                                         const ParseLimits& limits = {});

} // namespace repomancer::vcs
