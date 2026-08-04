// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/vcs/git/porcelain.h>

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <string>

using namespace repomancer::vcs;
using repomancer::vcs::git::parse_status_porcelain_v2z;

namespace {

std::string join_z(std::initializer_list<std::string> records) {
    std::string data;
    for (const auto& record : records) {
        data += record;
        data.push_back('\0');
    }
    return data;
}

} // namespace

TEST_CASE("porcelain v2: empty input yields empty snapshot") {
    const auto result = parse_status_porcelain_v2z("");
    REQUIRE(result.ok());
    CHECK(result.value().entries.empty());
}

TEST_CASE("porcelain v2: branch headers") {
    const auto data = join_z({
        "# branch.oid 1234abcd",
        "# branch.head main",
        "# branch.upstream origin/main",
        "# branch.ab +2 -1",
    });
    const auto result = parse_status_porcelain_v2z(data);
    REQUIRE(result.ok());
    const auto& branch = result.value().branch;
    CHECK(branch.oid == "1234abcd");
    CHECK(branch.head == "main");
    CHECK(branch.upstream == "origin/main");
    CHECK(branch.ahead == 2);
    CHECK(branch.behind == 1);
}

TEST_CASE("porcelain v2: ordinary, untracked and ignored entries") {
    const auto data = join_z({
        "1 .M N... 100644 100644 100644 aaaa bbbb path with spaces.txt",
        "1 A. N... 000000 100644 100644 0000 cccc staged.txt",
        "? untracked file.txt",
        "! build/",
    });
    const auto result = parse_status_porcelain_v2z(data);
    REQUIRE(result.ok());
    const auto& entries = result.value().entries;
    REQUIRE(entries.size() == 4);

    CHECK(entries[0].kind == EntryKind::Ordinary);
    CHECK(entries[0].x == '.');
    CHECK(entries[0].y == 'M');
    CHECK(entries[0].path == "path with spaces.txt");

    CHECK(entries[1].x == 'A');
    CHECK(entries[1].y == '.');
    CHECK(entries[1].path == "staged.txt");

    CHECK(entries[2].kind == EntryKind::Untracked);
    CHECK(entries[2].path == "untracked file.txt");

    CHECK(entries[3].kind == EntryKind::Ignored);
    CHECK(entries[3].path == "build/");
}

TEST_CASE("porcelain v2: rename consumes the following original-path record") {
    const auto data = join_z({
        "2 R. N... 100644 100644 100644 aaaa bbbb R100 new name.txt",
        "old.txt",
    });
    const auto result = parse_status_porcelain_v2z(data);
    REQUIRE(result.ok());
    REQUIRE(result.value().entries.size() == 1);
    const auto& entry = result.value().entries[0];
    CHECK(entry.kind == EntryKind::RenamedCopied);
    CHECK(entry.x == 'R');
    CHECK(entry.rename_score == 100);
    CHECK(entry.path == "new name.txt");
    CHECK(entry.orig_path == "old.txt");
}

TEST_CASE("porcelain v2: unmerged entry") {
    const auto data = join_z({
        "u UU N... 100644 100644 100644 100644 aaaa bbbb cccc conflict.txt",
    });
    const auto result = parse_status_porcelain_v2z(data);
    REQUIRE(result.ok());
    REQUIRE(result.value().entries.size() == 1);
    CHECK(result.value().entries[0].kind == EntryKind::Unmerged);
    CHECK(result.value().entries[0].path == "conflict.txt");
}

TEST_CASE("porcelain v2: attacker-ish bytes in paths survive verbatim") {
    // Control characters and raw UTF-8 must pass through the parser; display
    // sanitization happens at the UI layer (§13.1), never here.
    const std::string weird = "we\x01ird\xC3\xA9.txt";
    const auto data = join_z({"? " + weird});
    const auto result = parse_status_porcelain_v2z(data);
    REQUIRE(result.ok());
    CHECK(result.value().entries[0].path == weird);
}

TEST_CASE("porcelain v2: malformed records are rejected") {
    CHECK_FALSE(parse_status_porcelain_v2z(join_z({"1 M"})).ok());
    CHECK_FALSE(parse_status_porcelain_v2z(join_z({"z boom"})).ok());
    CHECK_FALSE(parse_status_porcelain_v2z(
                    join_z({"2 R. N... 100644 100644 100644 aaaa bbbb R100 new.txt"}))
                    .ok()); // rename without original path
    CHECK_FALSE(parse_status_porcelain_v2z(join_z({"# branch.ab 2 1"})).ok());
}

TEST_CASE("porcelain v2: limits are enforced") {
    ParseLimits limits;
    limits.max_entries = 1;
    const auto too_many = parse_status_porcelain_v2z(join_z({"? a.txt", "? b.txt"}), limits);
    REQUIRE_FALSE(too_many.ok());
    CHECK(too_many.error().kind == VcsError::Kind::LimitExceeded);

    ParseLimits tiny;
    tiny.max_record_bytes = 4;
    const auto too_big = parse_status_porcelain_v2z(join_z({"? aaaaaaaaaa.txt"}), tiny);
    REQUIRE_FALSE(too_big.ok());
    CHECK(too_big.error().kind == VcsError::Kind::LimitExceeded);
}
