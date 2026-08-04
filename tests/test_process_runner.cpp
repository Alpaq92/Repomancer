// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// ProcessRunner tests use `git` as the only guaranteed external binary in the
// dev environment (it is a product prerequisite anyway).

#include "fixture_repo.h"

#include <repomancer/process/process_runner.h>

#include <catch2/catch_test_macros.hpp>

using namespace repomancer::proc;
using repomancer::test::FixtureRepo;

TEST_CASE("process runner: captures stdout of a successful run") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    RunSpec spec;
    spec.exe = "git";
    spec.args = {"--version"};
    const auto result = ProcessRunner::run(spec);
    REQUIRE(result.status == LaunchStatus::Ok);
    CHECK(result.exit_code == 0);
    CHECK(result.out.find("git version") != std::string::npos);
}

TEST_CASE("process runner: nonzero exit with stderr is not a launch failure") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    RunSpec spec;
    spec.exe = "git";
    spec.args = {"definitely-not-a-git-subcommand"};
    const auto result = ProcessRunner::run(spec);
    REQUIRE(result.status == LaunchStatus::Ok);
    CHECK(result.exit_code != 0);
    CHECK_FALSE(result.err.empty());
}

TEST_CASE("process runner: missing executable reported as ExeNotFound") {
    RunSpec spec;
    spec.exe = "repomancer-no-such-binary-a3f1c9";
    spec.args = {"--whatever"};
    const auto result = ProcessRunner::run(spec);
    CHECK(result.status == LaunchStatus::ExeNotFound);
}

TEST_CASE("process runner: stdin reaches the child") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    RunSpec spec;
    spec.exe = "git";
    spec.args = {"hash-object", "--stdin"};
    spec.stdin_data = "hello\n";
    const auto result = ProcessRunner::run(spec);
    REQUIRE(result.status == LaunchStatus::Ok);
    REQUIRE(result.exit_code == 0);
    // Well-known object id of the blob "hello\n".
    CHECK(result.out.rfind("ce013625030ba8dba906f756967f9e9ca394464a", 0) == 0);
}

TEST_CASE("process runner: deadline enforced on a stuck child") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    RunSpec spec;
    spec.exe = "git";
    spec.args = {"hash-object", "--stdin"};
    spec.close_stdin = false; // child waits for EOF that never comes
    spec.timeout = std::chrono::milliseconds(500);
    const auto result = ProcessRunner::run(spec);
    CHECK(result.status == LaunchStatus::TimedOut);
}
