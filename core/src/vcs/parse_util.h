// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Private to core/src: the one strict number parse behind every porcelain
// grammar (none of which allow trailing junk after a number).

#pragma once

#include <charconv>
#include <string_view>
#include <system_error>

namespace repomancer::vcs {

template <typename T>
[[nodiscard]] bool parse_number(std::string_view text, T& out) {
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto [ptr, err] = std::from_chars(begin, end, out);
    return err == std::errc{} && ptr == end;
}

} // namespace repomancer::vcs
