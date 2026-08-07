// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// known_hosts core. The file ops and fingerprinting run against real
// ssh-keygen with locally-built host-key lines (no network). scan_host is
// exercised best-effort against an unreachable host.

#include <repomancer/process/process_runner.h>
#include <repomancer/ssh/keys.h>
#include <repomancer/ssh/known_hosts.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

using namespace repomancer::ssh;
namespace fs = std::filesystem;

namespace {

bool ssh_keygen_available() {
    repomancer::proc::RunSpec spec;
    spec.exe = "ssh-keygen";
    spec.args = {"-l", "-f", "-"};
    spec.stdin_data = "\n";
    return repomancer::proc::ProcessRunner::run(spec).status !=
           repomancer::proc::LaunchStatus::ExeNotFound;
}

bool ssh_keyscan_available() {
    repomancer::proc::RunSpec spec;
    spec.exe = "ssh-keyscan";
    spec.args = {"-h"};
    return repomancer::proc::ProcessRunner::run(spec).status !=
           repomancer::proc::LaunchStatus::ExeNotFound;
}

struct TempDir {
    TempDir() {
        std::random_device rd;
        dir = fs::temp_directory_path() / ("repomancer-kh-" + std::to_string(rd()));
        fs::create_directories(dir);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    fs::path dir;
};

// A known_hosts line "<host> <type> <base64>" built from a real generated key.
std::string host_line(const std::string& host, const std::string& public_key) {
    std::istringstream is(public_key);
    std::string type, b64;
    is >> type >> b64;
    return host + " " + type + " " + b64;
}

// Generate a throwaway ed25519 key and return its public_key line + fingerprint.
KeyInfo throwaway_key(const fs::path& dir, const std::string& name) {
    GenerateRequest req;
    req.type = KeyType::Ed25519;
    req.path = dir / name;
    auto r = generate(req);
    REQUIRE(r.ok());
    return std::move(r).value();
}

} // namespace

TEST_CASE("fingerprint_host_keys parses a host-key line and matches the key") {
    if (!ssh_keygen_available()) SKIP("ssh-keygen not found on PATH");
    TempDir tmp;
    const KeyInfo key = throwaway_key(tmp.dir, "hk");
    const std::string line = host_line("github.com", key.public_key);

    const auto r = fingerprint_host_keys(line);
    REQUIRE(r.ok());
    REQUIRE(r.value().size() == 1);
    CHECK(r.value()[0].host == "github.com");
    CHECK(r.value()[0].type == KeyType::Ed25519);
    CHECK(r.value()[0].fingerprint_sha256 == key.fingerprint_sha256);
    CHECK(r.value()[0].line == line);
}

TEST_CASE("fingerprint_host_keys skips blanks, comments and junk") {
    if (!ssh_keygen_available()) SKIP("ssh-keygen not found on PATH");
    TempDir tmp;
    const KeyInfo key = throwaway_key(tmp.dir, "hk");
    const std::string text = "# a comment\n\n" + host_line("gitlab.com", key.public_key) +
                             "\nnot-a-key blah blah\n";
    const auto r = fingerprint_host_keys(text);
    REQUIRE(r.ok());
    REQUIRE(r.value().size() == 1);
    CHECK(r.value()[0].host == "gitlab.com");
}

TEST_CASE("known_hosts add / contains / remove round-trip") {
    if (!ssh_keygen_available()) SKIP("ssh-keygen not found on PATH");
    TempDir tmp;
    const fs::path kh = tmp.dir / "known_hosts";
    const KeyInfo key = throwaway_key(tmp.dir, "hk");

    CHECK_FALSE(known_hosts_contains(kh, "example.com").value()); // missing file

    REQUIRE(known_hosts_add(kh, {host_line("example.com", key.public_key)}).ok());
    CHECK(known_hosts_contains(kh, "example.com").value());

    REQUIRE(known_hosts_remove(kh, "example.com").ok());
    CHECK_FALSE(known_hosts_contains(kh, "example.com").value());
}

TEST_CASE("known_hosts_remove on an absent host or file is a no-op success") {
    if (!ssh_keygen_available()) SKIP("ssh-keygen not found on PATH");
    TempDir tmp;
    const fs::path kh = tmp.dir / "known_hosts";
    CHECK(known_hosts_remove(kh, "nope.example").ok()); // file absent
    { std::ofstream(kh) << "other.example ssh-ed25519 AAAA\n"; }
    CHECK(known_hosts_remove(kh, "nope.example").ok()); // host absent
}

TEST_CASE("trusted forge fingerprints: lookup mechanism") {
    const auto& gh = trusted_forge_fingerprints("github.com");
    const auto& gl = trusted_forge_fingerprints("gitlab.com");
    REQUIRE_FALSE(gh.empty());
    REQUIRE_FALSE(gl.empty());

    // A present value matches; a bogus one does not; unknown hosts have none.
    CHECK(is_trusted_forge_key("github.com", gh.front()));
    CHECK_FALSE(is_trusted_forge_key("github.com", "SHA256:not-a-real-fingerprint"));
    CHECK(trusted_forge_fingerprints("example.com").empty());
    CHECK_FALSE(is_trusted_forge_key("example.com", gh.front()));
    // Every seeded value is in SHA256 form.
    for (const auto& fp : gh) {
        CHECK(fp.rfind("SHA256:", 0) == 0);
    }
}

TEST_CASE("scan_host of an unreachable host is empty, not an error") {
    if (!ssh_keyscan_available()) SKIP("ssh-keyscan not found on PATH");
    KnownHostsConfig cfg;
    cfg.scan_seconds = 2;
    const auto r = scan_host("nonexistent.invalid", cfg);
    REQUIRE(r.ok()); // unreachable is not an error, just no keys
    CHECK(r.value().empty());
}

TEST_CASE("a missing ssh-keygen binary is reported by known_hosts_contains") {
    TempDir tmp;
    const fs::path kh = tmp.dir / "known_hosts";
    { std::ofstream(kh) << "x ssh-ed25519 AAAA\n"; }
    KnownHostsConfig cfg;
    cfg.keygen_binary = "repomancer-no-such-ssh-keygen-4b1a";
    const auto r = known_hosts_contains(kh, "x", cfg);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == SshError::Kind::ExeNotFound);
}
