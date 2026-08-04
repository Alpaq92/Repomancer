// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// `git log` machine format: fields separated by US (0x1F), records NUL
// terminated (-z). The body field (%b) is last and consumes the record
// remainder, so newlines and even stray 0x1F bytes inside the body survive.

#pragma once

#include <repomancer/vcs/model.h>

#include <string>
#include <string_view>
#include <vector>

namespace repomancer::vcs::git {

inline constexpr char kFieldSep = '\x1f';

// "--format=%H<US>%P<US>%at<US>%an<US>%ae<US>%ct<US>%cn<US>%ce<US>%D<US>%s<US>%b"
[[nodiscard]] std::string log_format_argument();

// Full argv tail for `git log` (§13.1: always ends positional args with
// `--end-of-options <rev> --`).
[[nodiscard]] std::vector<std::string> build_log_args(const LogOptions& options);

VcsResult<std::vector<Commit>> parse_log_z(std::string_view data, const ParseLimits& limits = {});

} // namespace repomancer::vcs::git
