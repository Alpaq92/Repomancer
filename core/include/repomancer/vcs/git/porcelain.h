// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Parser for `git status --porcelain=v2 -z --branch` output.
// NUL-terminated records; rename records ('2') are followed by one extra
// NUL-terminated record holding the original path.

#pragma once

#include <repomancer/vcs/model.h>

#include <string_view>

namespace repomancer::vcs::git {

VcsResult<StatusSnapshot> parse_status_porcelain_v2z(std::string_view data,
                                                     const ParseLimits& limits = {});

} // namespace repomancer::vcs::git
