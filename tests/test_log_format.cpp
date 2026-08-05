// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/vcs/git/log_format.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

using namespace repomancer::vcs;
using namespace repomancer::vcs::git;

namespace {

std::string record(std::initializer_list<std::string> fields) {
    std::string rec;
    bool first = true;
    for (const auto& field : fields) {
        if (!first) {
            rec.push_back(kFieldSep);
        }
        rec += field;
        first = false;
    }
    rec.push_back('\0');
    return rec;
}

} // namespace

TEST_CASE("log args: end-of-options discipline") {
    LogOptions options;
    options.max_count = 42;
    options.rev = "HEAD";
    options.all_refs = false;
    const auto args = build_log_args(options);

    const auto eoo = std::find(args.begin(), args.end(), "--end-of-options");
    REQUIRE(eoo != args.end());
    // rev comes after --end-of-options, and the args end with "--".
    REQUIRE(std::next(eoo) != args.end());
    CHECK(*std::next(eoo) == "HEAD");
    CHECK(args.back() == "--");
    CHECK(std::find(args.begin(), args.end(), "--max-count=42") != args.end());
    CHECK(std::find(args.begin(), args.end(), "--all") == args.end());
}

TEST_CASE("log args: all_refs replaces the revision with --all") {
    LogOptions options;
    options.all_refs = true;
    const auto args = build_log_args(options);

    CHECK(std::find(args.begin(), args.end(), "--all") != args.end());
    // No revision may follow --end-of-options, or git would union it with --all.
    const auto eoo = std::find(args.begin(), args.end(), "--end-of-options");
    REQUIRE(eoo != args.end());
    REQUIRE(std::next(eoo) != args.end());
    CHECK(*std::next(eoo) == "--");
    CHECK(args.back() == "--");
}

TEST_CASE("log format string shape") {
    const auto fmt = log_format_argument();
    CHECK(fmt.starts_with("--format=%H"));
    CHECK(fmt.find("%b") == fmt.size() - 2); // body is last
}

TEST_CASE("log parse: full record") {
    const auto data = record({
        "deadbeef",
        "p1 p2",
        "1767261600",
        "Alice",
        "alice@x.test",
        "1767265200",
        "Bob",
        "bob@x.test",
        "HEAD -> main, tag: v1",
        "Subject here",
        "Body line1\nline2\x1fwith stray separator",
    });
    const auto result = parse_log_z(data);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 1);
    const auto& commit = result.value()[0];
    CHECK(commit.hash == "deadbeef");
    REQUIRE(commit.parents.size() == 2);
    CHECK(commit.parents[0] == "p1");
    CHECK(commit.parents[1] == "p2");
    CHECK(commit.author_time == 1767261600);
    CHECK(commit.commit_time == 1767265200);
    CHECK(commit.author_name == "Alice");
    CHECK(commit.committer_email == "bob@x.test");
    CHECK(commit.refs == "HEAD -> main, tag: v1");
    CHECK(commit.subject == "Subject here");
    CHECK(commit.body == "Body line1\nline2\x1fwith stray separator");
}

TEST_CASE("log parse: root commit with empty parents and empty body") {
    const auto data = record({
        "cafebabe", "", "1767261600", "Alice", "a@x", "1767261600", "Alice", "a@x", "", "Root",
        "",
    });
    const auto result = parse_log_z(data);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 1);
    CHECK(result.value()[0].parents.empty());
    CHECK(result.value()[0].body.empty());
    CHECK(result.value()[0].refs.empty());
}

TEST_CASE("log parse: malformed records rejected") {
    CHECK_FALSE(parse_log_z(record({"only", "five", "fields", "in", "record"})).ok());

    const auto bad_time = record({
        "h", "", "12x", "A", "a@x", "1", "A", "a@x", "", "S", "",
    });
    const auto result = parse_log_z(bad_time);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().kind == VcsError::Kind::ParseError);
}

TEST_CASE("log parse: limits enforced") {
    ParseLimits limits;
    limits.max_entries = 1;
    const auto two = record({"h1", "", "1", "A", "a@x", "1", "A", "a@x", "", "S", ""}) +
                     record({"h2", "", "1", "A", "a@x", "1", "A", "a@x", "", "S", ""});
    const auto result = parse_log_z(two, limits);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().kind == VcsError::Kind::LimitExceeded);
}

TEST_CASE("log args: a path scopes the log and follows renames") {
    repomancer::vcs::LogOptions options;
    options.all_refs = false;
    options.path = "src/--evil.txt";
    const auto args = repomancer::vcs::git::build_log_args(options);

    const auto follow = std::find(args.begin(), args.end(), "--follow");
    const auto separator = std::find(args.begin(), args.end(), "--");
    const auto end_of_options = std::find(args.begin(), args.end(), "--end-of-options");
    REQUIRE(follow != args.end());
    REQUIRE(separator != args.end());
    REQUIRE(end_of_options != args.end());
    // The option before the guard, the path after the separator — a path
    // that looks like an option must never be parsed as one.
    CHECK(follow < end_of_options);
    CHECK(args.back() == "src/--evil.txt");
    CHECK(separator < args.end() - 1);
}
