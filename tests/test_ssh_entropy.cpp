// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// The key-ceremony generator. The decisive tests round-trip through the real
// ssh-keygen: a key we serialize ourselves must load, fingerprint, and derive
// the same public key as the .pub we wrote beside it.

#include <repomancer/process/process_runner.h>
#include <repomancer/ssh/entropy.h>
#include <repomancer/ssh/keys.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <string>

using namespace repomancer::ssh;
namespace fs = std::filesystem;

namespace {

bool ssh_keygen_available() {
#if defined(_WIN32)
    return false; // see test_ssh_keys.cpp — Windows OpenSSH interop unverified
#else
    repomancer::proc::RunSpec spec;
    spec.exe = "ssh-keygen";
    spec.args = {"-l", "-f", "-"};
    spec.stdin_data = "\n";
    return repomancer::proc::ProcessRunner::run(spec).status !=
           repomancer::proc::LaunchStatus::ExeNotFound;
#endif
}

struct TempDir {
    TempDir() {
        std::random_device rd;
        dir = fs::temp_directory_path() / ("repomancer-entropy-" + std::to_string(rd()));
        fs::create_directories(dir);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    fs::path dir;
};

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// `ssh-keygen -y` derives the public key from the private one — the strongest
// check that our serialization is a valid, self-consistent OpenSSH key.
std::string derive_public(const fs::path& priv, const std::string& passphrase = {}) {
    repomancer::proc::RunSpec spec;
    spec.exe = "ssh-keygen";
    spec.args = {"-y", "-P", passphrase, "-f", priv.string()};
    const auto run = repomancer::proc::ProcessRunner::run(spec);
    return run.ok() ? run.out : std::string{};
}

bool passphrase_opens(const fs::path& priv, const std::string& passphrase) {
    repomancer::proc::RunSpec spec;
    spec.exe = "ssh-keygen";
    spec.args = {"-y", "-P", passphrase, "-f", priv.string()};
    return repomancer::proc::ProcessRunner::run(spec).ok();
}

} // namespace

TEST_CASE("EntropyPool: OS entropy alone yields a usable, unique seed") {
    EntropyPool a;
    EntropyPool b;
    CHECK(a.samples() == 0);
    const auto sa = a.finalize_seed();
    const auto sb = b.finalize_seed();
    REQUIRE(sa.size() == 32);
    // Two pools with no user samples must still differ — the OS draw carries it.
    CHECK(sa != sb);
}

TEST_CASE("EntropyPool: absorbing samples counts and changes the seed") {
    EntropyPool pool;
    for (int i = 0; i < 10; ++i) {
        pool.absorb_point(i * 3, i * 7, 1'000'000 + i);
    }
    CHECK(pool.samples() == 10);
    CHECK(pool.finalize_seed().size() == 32);
}

TEST_CASE("EntropyPool: identical sample streams still differ (OS entropy dominates)") {
    const auto run_one = [] {
        EntropyPool pool;
        for (int i = 0; i < 20; ++i) {
            pool.absorb_point(i, i, i); // deliberately identical, low-entropy input
        }
        return pool.finalize_seed();
    };
    // A hostile or degenerate sample stream cannot make two ceremonies agree:
    // OS entropy is mixed in at both ends.
    CHECK(run_one() != run_one());
}

TEST_CASE("generate_from_seed writes a key real ssh-keygen accepts") {
    if (!ssh_keygen_available()) SKIP("ssh-keygen not found on PATH");
    TempDir tmp;
    EntropyPool pool;
    for (int i = 0; i < 32; ++i) {
        pool.absorb_point(i * 11, i * 13, 5'000 + i * 17);
    }

    GenerateRequest req;
    req.type = KeyType::Ed25519;
    req.comment = "ceremony@repomancer";
    req.path = tmp.dir / "id_ed25519";

    const auto r = generate_from_seed(req, pool.finalize_seed());
    REQUIRE(r.ok());
    const KeyInfo& key = r.value();

    // ssh-keygen inspected it: right type, right size, our comment.
    CHECK(key.type == KeyType::Ed25519);
    CHECK(key.bits == 256);
    CHECK(key.comment == "ceremony@repomancer");
    CHECK(key.fingerprint_sha256.rfind("SHA256:", 0) == 0);

    // The private key derives exactly the public key we wrote beside it.
    const std::string derived = derive_public(req.path);
    REQUIRE_FALSE(derived.empty());
    const std::string stored = read_file(fs::path(req.path.string() + ".pub"));
    CHECK(stored.rfind("ssh-ed25519 ", 0) == 0);
    // `-y` prints "ssh-ed25519 <b64>" with no comment; compare that prefix.
    CHECK(stored.rfind(derived.substr(0, derived.find_last_not_of(" \n\r") + 1), 0) == 0);

    // It is a standard PEM-armoured OpenSSH key.
    const std::string pem = read_file(req.path);
    CHECK(pem.rfind("-----BEGIN OPENSSH PRIVATE KEY-----\n", 0) == 0);
    CHECK(pem.find("-----END OPENSSH PRIVATE KEY-----") != std::string::npos);
}

TEST_CASE("generate_from_seed is deterministic for a given seed") {
    if (!ssh_keygen_available()) SKIP("ssh-keygen not found on PATH");
    TempDir tmp;
    const std::vector<std::uint8_t> seed(32, 0x42); // fixed seed

    GenerateRequest a;
    a.path = tmp.dir / "key_a";
    GenerateRequest b;
    b.path = tmp.dir / "key_b";
    const auto ra = generate_from_seed(a, seed);
    const auto rb = generate_from_seed(b, seed);
    REQUIRE(ra.ok());
    REQUIRE(rb.ok());
    // Same seed ⇒ same key material (proves the seed really drives the key).
    CHECK(ra.value().fingerprint_sha256 == rb.value().fingerprint_sha256);
}

TEST_CASE("different seeds produce different keys") {
    if (!ssh_keygen_available()) SKIP("ssh-keygen not found on PATH");
    TempDir tmp;
    std::set<std::string> fingerprints;
    for (int i = 0; i < 3; ++i) {
        EntropyPool pool;
        pool.absorb_point(i, i, i);
        GenerateRequest req;
        req.path = tmp.dir / ("key_" + std::to_string(i));
        const auto r = generate_from_seed(req, pool.finalize_seed());
        REQUIRE(r.ok());
        fingerprints.insert(r.value().fingerprint_sha256);
    }
    CHECK(fingerprints.size() == 3);
}

TEST_CASE("generate_from_seed applies a passphrase when asked") {
    if (!ssh_keygen_available()) SKIP("ssh-keygen not found on PATH");
    TempDir tmp;
    EntropyPool pool;
    pool.absorb_point(1, 2, 3);

    GenerateRequest req;
    req.path = tmp.dir / "id_protected";
    req.passphrase = "ceremony-secret";
    const auto r = generate_from_seed(req, pool.finalize_seed());
    REQUIRE(r.ok());

    CHECK(passphrase_opens(req.path, "ceremony-secret"));
    CHECK_FALSE(passphrase_opens(req.path, ""));
    CHECK(is_encrypted(req.path).value());
}

TEST_CASE("generate_from_seed refuses to clobber and rejects bad input") {
    if (!ssh_keygen_available()) SKIP("ssh-keygen not found on PATH");
    TempDir tmp;
    const std::vector<std::uint8_t> seed(32, 0x11);
    GenerateRequest req;
    req.path = tmp.dir / "id_ed25519";
    REQUIRE(generate_from_seed(req, seed).ok());

    // Second attempt at the same path is refused, key left intact.
    const auto again = generate_from_seed(req, seed);
    REQUIRE_FALSE(again.ok());
    CHECK(again.error().kind == SshError::Kind::InvalidRequest);

    // A short seed and a non-Ed25519 type are rejected up front.
    GenerateRequest other;
    other.path = tmp.dir / "id_other";
    CHECK_FALSE(generate_from_seed(other, std::vector<std::uint8_t>(16, 0)).ok());
    GenerateRequest rsa;
    rsa.path = tmp.dir / "id_rsa";
    rsa.type = KeyType::Rsa;
    CHECK_FALSE(generate_from_seed(rsa, seed).ok());
}
