// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/vcs/blame.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using repomancer::vcs::parse_blame_line_porcelain;
using repomancer::vcs::ParseLimits;

namespace {

const std::string kHashA(40, 'a');
const std::string kHashB(40, 'b');

std::string record(const std::string& hash, const std::string& author, long time,
                   const std::string& content) {
    return hash + " 1 1 1\n" + "author " + author + "\n" + "author-mail <x@y>\n" +
           "author-time " + std::to_string(time) + "\n" + "author-tz +0000\n" +
           "summary something\n" + "filename f.txt\n" + "\t" + content + "\n";
}

} // namespace

TEST_CASE("blame: line-porcelain records parse into attributed lines") {
    const std::string data =
        record(kHashA, "Dev One", 1700000000, "first line") +
        record(kHashB, "Dev Two", 1800000000, "second\twith tab");

    const auto result = parse_blame_line_porcelain(data);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 2);
    CHECK(result.value()[0].hash == kHashA);
    CHECK(result.value()[0].author == "Dev One");
    CHECK(result.value()[0].author_time == 1700000000);
    CHECK(result.value()[0].content == "first line");
    // Only the first TAB separates; the content keeps its own.
    CHECK(result.value()[1].content == "second\twith tab");
}

TEST_CASE("blame: truncated and malformed input is an error, not garbage") {
    const std::string headers_only = kHashA + " 1 1 1\nauthor Dev\n";
    CHECK(!parse_blame_line_porcelain(headers_only).ok());

    CHECK(!parse_blame_line_porcelain("\torphan content\n").ok());
}

TEST_CASE("blame: limits cap entries and record size") {
    ParseLimits limits;
    limits.max_entries = 1;
    const std::string two =
        record(kHashA, "A", 1, "x") + record(kHashB, "B", 2, "y");
    CHECK(!parse_blame_line_porcelain(two, limits).ok());

    ParseLimits tiny;
    tiny.max_record_bytes = 16;
    CHECK(!parse_blame_line_porcelain(record(kHashA, "A", 1, "x"), tiny).ok());
}

TEST_CASE("blame: empty input is an empty file") {
    const auto result = parse_blame_line_porcelain("");
    REQUIRE(result.ok());
    CHECK(result.value().empty());
}
