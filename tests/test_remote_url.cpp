// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Remote-URL parsing is pure logic, so these run everywhere with no fixtures.

#include <repomancer/vcs/remote_url.h>

#include <catch2/catch_test_macros.hpp>

using repomancer::vcs::parse_remote_url;

TEST_CASE("remote url: scp-like SSH form") {
    const auto r = parse_remote_url("git@github.com:Alpaq92/Repomancer.git");
    REQUIRE(r.has_value());
    CHECK(r->host == "github.com");
    CHECK(r->user == "git");
    CHECK(r->owner == "Alpaq92");
    CHECK(r->repo == "Repomancer"); // .git stripped
    CHECK(r->ssh);
    CHECK(r->port == 0);
}

TEST_CASE("remote url: ssh:// with an explicit port") {
    const auto r = parse_remote_url("ssh://git@git.example.com:2222/team/tool.git");
    REQUIRE(r.has_value());
    CHECK(r->host == "git.example.com");
    CHECK(r->port == 2222);
    CHECK(r->owner == "team");
    CHECK(r->repo == "tool");
    CHECK(r->ssh);
}

TEST_CASE("remote url: https, with and without a user") {
    const auto plain = parse_remote_url("https://github.com/Alpaq92/Repomancer.git");
    REQUIRE(plain.has_value());
    CHECK(plain->host == "github.com");
    CHECK(plain->owner == "Alpaq92");
    CHECK(plain->repo == "Repomancer");
    CHECK_FALSE(plain->ssh);
    CHECK(plain->user.empty());

    const auto with_user = parse_remote_url("https://bob@gitlab.com/g/p.git");
    REQUIRE(with_user.has_value());
    CHECK(with_user->host == "gitlab.com");
    CHECK(with_user->user == "bob");
}

TEST_CASE("remote url: no .git suffix, and nested owner groups") {
    const auto bare = parse_remote_url("https://github.com/o/r");
    REQUIRE(bare.has_value());
    CHECK(bare->repo == "r");

    // GitLab subgroups: everything before the last segment is the owner.
    const auto nested = parse_remote_url("git@gitlab.com:group/sub/proj.git");
    REQUIRE(nested.has_value());
    CHECK(nested->owner == "group/sub");
    CHECK(nested->repo == "proj");
}

TEST_CASE("remote url: git:// scheme is not SSH") {
    const auto r = parse_remote_url("git://github.com/o/r.git");
    REQUIRE(r.has_value());
    CHECK(r->host == "github.com");
    CHECK_FALSE(r->ssh);
}

TEST_CASE("remote url: local paths have no host") {
    CHECK_FALSE(parse_remote_url("/srv/git/repo.git").has_value());
    CHECK_FALSE(parse_remote_url("../sibling").has_value());
    CHECK_FALSE(parse_remote_url("file:///srv/git/repo.git").has_value());
    CHECK_FALSE(parse_remote_url("C:/work/repo").has_value()); // Windows drive
    CHECK_FALSE(parse_remote_url("").has_value());
    CHECK_FALSE(parse_remote_url("   ").has_value());
}

TEST_CASE("remote url: surrounding whitespace is ignored") {
    const auto r = parse_remote_url("  git@github.com:o/r.git\n");
    REQUIRE(r.has_value());
    CHECK(r->host == "github.com");
    CHECK(r->repo == "r");
}

TEST_CASE("remote url: an IPv6 literal keeps its brackets and port") {
    const auto r = parse_remote_url("ssh://git@[2001:db8::1]:2222/o/r.git");
    REQUIRE(r.has_value());
    CHECK(r->host == "[2001:db8::1]");
    CHECK(r->port == 2222);
    CHECK(r->repo == "r");
}

TEST_CASE("remote url: host without a path still parses") {
    const auto r = parse_remote_url("https://github.com");
    REQUIRE(r.has_value());
    CHECK(r->host == "github.com");
    CHECK(r->owner.empty());
    CHECK(r->repo.empty());
}
