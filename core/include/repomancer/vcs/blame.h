// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Line attribution for one file, as `git blame --line-porcelain` reports it.

#pragma once

#include <repomancer/vcs/model.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace repomancer::vcs {

struct BlameLine {
    std::string hash;        // commit that introduced the line
    std::string author;
    std::int64_t author_time = 0; // unix epoch seconds
    std::string content;     // the line itself, without the newline
};

// `--line-porcelain` repeats the full header block before every line, which
// keeps the parse stateless: a header line names the commit, `author` /
// `author-time` fill the attribution, and the TAB-prefixed line closes the
// record. Limits cap both the record size and the total count (§13.2).
[[nodiscard]] VcsResult<std::vector<BlameLine>> parse_blame_line_porcelain(
    std::string_view data, const ParseLimits& limits = {});

} // namespace repomancer::vcs
