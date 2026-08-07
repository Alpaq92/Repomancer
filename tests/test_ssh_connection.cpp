// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// The greeting parser is pure and tested exhaustively; the network probe is
// exercised best-effort (unreachable host, missing binary).

#include <repomancer/process/process_runner.h>
#include <repomancer/ssh/connection.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace repomancer::ssh;

namespace {
bool ssh_available() {
    repomancer::proc::RunSpec spec;
    spec.exe = "ssh";
    spec.args = {"-V"};
    return repomancer::proc::ProcessRunner::run(spec).status !=
           repomancer::proc::LaunchStatus::ExeNotFound;
}
} // namespace

TEST_CASE("parse_connection_response: GitHub success") {
    const auto r = parse_connection_response(
        "Hi octocat! You've successfully authenticated, but GitHub does not "
        "provide shell access.");
    CHECK(r.authenticated);
    CHECK(r.username == "octocat");
}

TEST_CASE("parse_connection_response: GitLab success") {
    const auto r = parse_connection_response("Welcome to GitLab, @sam.doe!");
    CHECK(r.authenticated);
    CHECK(r.username == "sam.doe");
}

TEST_CASE("parse_connection_response: permission denied is not authenticated") {
    const auto r = parse_connection_response(
        "git@github.com: Permission denied (publickey).");
    CHECK_FALSE(r.authenticated);
    CHECK(r.username.empty());
    CHECK(r.message.find("Permission denied") != std::string::npos);
}

TEST_CASE("parse_connection_response: unreachable / empty are not authenticated") {
    CHECK_FALSE(parse_connection_response(
                    "ssh: Could not resolve hostname x: Name or service not known")
                    .authenticated);
    CHECK_FALSE(parse_connection_response("").authenticated);
}

TEST_CASE("parse_connection_response: generic auth without a name still counts") {
    const auto r = parse_connection_response("You have successfully authenticated.");
    CHECK(r.authenticated);
    CHECK(r.username.empty());
}

TEST_CASE("test_connection to an unreachable host is a clean negative") {
    if (!ssh_available()) SKIP("ssh not found on PATH");
    ConnectionConfig cfg;
    cfg.connect_seconds = 2;
    const auto r = test_connection("git@nonexistent.invalid", cfg);
    REQUIRE(r.ok()); // the probe ran; auth simply failed
    CHECK_FALSE(r.value().authenticated);
}

TEST_CASE("test_connection reports a missing ssh binary") {
    ConnectionConfig cfg;
    cfg.ssh_binary = "repomancer-no-such-ssh-8d2e";
    const auto r = test_connection("git@github.com", cfg);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == SshError::Kind::ExeNotFound);
}
