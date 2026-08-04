// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/vcs/refs.h>

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <string>

using namespace repomancer::vcs;

namespace {

// git writes five NUL-separated fields and terminates the record with a
// newline — there is no NUL after the last field. Getting this wrong is what
// made the sidebar come up empty, so the fixtures mirror git exactly.
std::string record(std::initializer_list<std::string> fields) {
    std::string data;
    bool first = true;
    for (const auto& field : fields) {
        if (!first) {
            data.push_back('\0');
        }
        data += field;
        first = false;
    }
    data.push_back('\n');
    return data;
}

} // namespace

TEST_CASE("refs: kinds are derived from the full name") {
    const std::string data =
        record({"refs/heads/main", "aaa", "*", "origin/main", "commit"}) +
        record({"refs/remotes/origin/main", "aaa", " ", "", "commit"}) +
        record({"refs/tags/v1.0", "bbb", " ", "", "tag"}) +
        record({"refs/stash", "ccc", " ", "", "commit"}) +
        record({"refs/notes/commits", "ddd", " ", "", "commit"});

    const auto result = parse_for_each_ref_z(data);
    REQUIRE(result.ok());
    const auto& refs = result.value();
    REQUIRE(refs.size() == 5);

    CHECK(refs[0].kind == RefKind::LocalBranch);
    CHECK(refs[0].short_name == "main");
    CHECK(refs[0].is_head);
    CHECK(refs[0].upstream == "origin/main");

    // A non-HEAD branch carries a space in the HEAD field, not an empty one.
    CHECK(refs[1].kind == RefKind::RemoteBranch);
    CHECK(refs[1].short_name == "origin/main");
    CHECK_FALSE(refs[1].is_head);

    CHECK(refs[2].kind == RefKind::Tag);
    CHECK(refs[2].short_name == "v1.0");

    CHECK(refs[3].kind == RefKind::Stash);
    CHECK(refs[3].short_name == "stash");

    CHECK(refs[4].kind == RefKind::Other);
    CHECK(refs[4].short_name == "refs/notes/commits");
}

TEST_CASE("refs: branch names containing slashes keep them") {
    const auto data = record({"refs/heads/feature/login", "aaa", " ", "", "commit"});
    const auto result = parse_for_each_ref_z(data);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 1);
    CHECK(result.value()[0].short_name == "feature/login");
    CHECK(result.value()[0].kind == RefKind::LocalBranch);
}

TEST_CASE("refs: empty input and a truncated record") {
    const auto empty = parse_for_each_ref_z("");
    REQUIRE(empty.ok());
    CHECK(empty.value().empty());

    std::string truncated = "refs/heads/main";
    truncated.push_back('\0');
    truncated += "aaa";
    truncated.push_back('\n');
    const auto result = parse_for_each_ref_z(truncated);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().kind == VcsError::Kind::ParseError);
}

TEST_CASE("refs: limits are enforced") {
    ParseLimits limits;
    limits.max_entries = 1;
    const std::string data = record({"refs/heads/a", "1", " ", "", "commit"}) +
                             record({"refs/heads/b", "2", " ", "", "commit"});
    const auto result = parse_for_each_ref_z(data, limits);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().kind == VcsError::Kind::LimitExceeded);
}
