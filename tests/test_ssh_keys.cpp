// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// SSH key core tests, driven against the real ssh-keygen (present on every
// target OS; the product's key engine per §7). Skipped where ssh-keygen is
// absent, like the git tests skip without git.

#include <repomancer/process/process_runner.h>
#include <repomancer/ssh/keys.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>

using namespace repomancer::ssh;
namespace fs = std::filesystem;

namespace {

bool ssh_keygen_available() {
    repomancer::proc::RunSpec spec;
    spec.exe = "ssh-keygen";
    spec.args = {"-A", "-f", "/nonexistent-repomancer-probe"};
    // We don't care about the exit code — only that the binary launches.
    return repomancer::proc::ProcessRunner::run(spec).status !=
           repomancer::proc::LaunchStatus::ExeNotFound;
}

struct TempDir {
    TempDir() {
        std::random_device rd;
        dir = fs::temp_directory_path() / ("repomancer-ssh-" + std::to_string(rd()));
        fs::create_directories(dir);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    fs::path dir;
};

// Does `ssh-keygen -y -P <passphrase>` accept this passphrase for the key?
// Empty passphrase succeeding ⇒ the key is unencrypted.
bool passphrase_opens(const fs::path& priv, const std::string& passphrase) {
    repomancer::proc::RunSpec spec;
    spec.exe = "ssh-keygen";
    spec.args = {"-y", "-P", passphrase, "-f", priv.string()};
    return repomancer::proc::ProcessRunner::run(spec).ok();
}

} // namespace

TEST_CASE("ssh key type strings round-trip and parse") {
    CHECK(std::string(to_string(KeyType::Ed25519)) == "ed25519");
    CHECK(std::string(to_string(KeyType::Rsa)) == "rsa");
    CHECK(std::string(to_string(KeyType::Ecdsa)) == "ecdsa");

    CHECK(key_type_from_string("ED25519") == KeyType::Ed25519);
    CHECK(key_type_from_string("ssh-ed25519") == KeyType::Ed25519);
    CHECK(key_type_from_string("RSA") == KeyType::Rsa);
    CHECK(key_type_from_string("ecdsa-sha2-nistp256") == KeyType::Ecdsa);
    CHECK_FALSE(key_type_from_string("dsa").has_value());
}

TEST_CASE("generate ed25519 with no passphrase") {
    if (!ssh_keygen_available()) SKIP("ssh-keygen not found on PATH");
    TempDir tmp;
    GenerateRequest req;
    req.type = KeyType::Ed25519;
    req.comment = "unit@repomancer";
    req.path = tmp.dir / "id_ed25519";

    const auto r = generate(req);
    REQUIRE(r.ok());
    const KeyInfo& k = r.value();

    CHECK(fs::exists(req.path));
    CHECK(fs::exists(fs::path(req.path.string() + ".pub")));
    CHECK(k.type == KeyType::Ed25519);
    CHECK(k.bits == 256);
    CHECK(k.comment == "unit@repomancer");
    CHECK(k.fingerprint_sha256.rfind("SHA256:", 0) == 0);
    CHECK(k.public_key.rfind("ssh-ed25519 ", 0) == 0);
    CHECK(k.public_key.find("unit@repomancer") != std::string::npos);
    CHECK(k.private_path == req.path);
    CHECK(k.public_path == fs::path(req.path.string() + ".pub"));
    // No passphrase ⇒ an empty passphrase opens it.
    CHECK(passphrase_opens(req.path, ""));
}

TEST_CASE("generate rsa honours the bit size") {
    if (!ssh_keygen_available()) SKIP("ssh-keygen not found on PATH");
    TempDir tmp;
    GenerateRequest req;
    req.type = KeyType::Rsa;
    req.bits = 2048;
    req.path = tmp.dir / "id_rsa";

    const auto r = generate(req);
    REQUIRE(r.ok());
    CHECK(r.value().type == KeyType::Rsa);
    CHECK(r.value().bits == 2048);
}

TEST_CASE("generate with a passphrase produces an ENCRYPTED key (never in argv)") {
    if (!ssh_keygen_available()) SKIP("ssh-keygen not found on PATH");
    TempDir tmp;
    GenerateRequest req;
    req.type = KeyType::Ed25519;
    req.path = tmp.dir / "id_secure";
    req.passphrase = "correct horse battery staple";

    const auto r = generate(req);
    REQUIRE(r.ok());
    // The security guarantee: the key really is encrypted — an empty passphrase
    // must NOT open it, the real one must. (Regression guard against the
    // askpass-fallback trap where the passphrase is silently dropped.)
    CHECK_FALSE(passphrase_opens(req.path, ""));
    CHECK_FALSE(passphrase_opens(req.path, "wrong passphrase"));
    CHECK(passphrase_opens(req.path, "correct horse battery staple"));
}

TEST_CASE("generate refuses to clobber an existing key") {
    if (!ssh_keygen_available()) SKIP("ssh-keygen not found on PATH");
    TempDir tmp;
    GenerateRequest req;
    req.path = tmp.dir / "id_ed25519";
    REQUIRE(generate(req).ok());

    const auto again = generate(req);
    REQUIRE_FALSE(again.ok());
    CHECK(again.error().kind == SshError::Kind::InvalidRequest);
    // The original key is untouched (still openable, still present).
    CHECK(fs::exists(req.path));
}

TEST_CASE("generate creates a missing parent directory") {
    if (!ssh_keygen_available()) SKIP("ssh-keygen not found on PATH");
    TempDir tmp;
    GenerateRequest req;
    req.path = tmp.dir / "nested" / "sub" / "id_ed25519";
    const auto r = generate(req);
    REQUIRE(r.ok());
    CHECK(fs::exists(req.path));
}

TEST_CASE("inspect agrees on the public and the private file") {
    if (!ssh_keygen_available()) SKIP("ssh-keygen not found on PATH");
    TempDir tmp;
    GenerateRequest req;
    req.path = tmp.dir / "id_ed25519";
    req.comment = "pairwise@test";
    REQUIRE(generate(req).ok());

    const auto from_priv = inspect(req.path);
    const auto from_pub = inspect(fs::path(req.path.string() + ".pub"));
    REQUIRE(from_priv.ok());
    REQUIRE(from_pub.ok());
    CHECK(from_priv.value().fingerprint_sha256 == from_pub.value().fingerprint_sha256);
    CHECK(from_pub.value().comment == "pairwise@test");
    // Inspecting the .pub still locates the private half beside it.
    CHECK(from_pub.value().private_path == req.path);
}

TEST_CASE("inspect of a nonexistent key is a nonzero-exit error") {
    if (!ssh_keygen_available()) SKIP("ssh-keygen not found on PATH");
    TempDir tmp;
    const auto r = inspect(tmp.dir / "does-not-exist");
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == SshError::Kind::NonZeroExit);
}

TEST_CASE("list_keys finds every pair and skips non-keys") {
    if (!ssh_keygen_available()) SKIP("ssh-keygen not found on PATH");
    TempDir tmp;
    for (const char* name : {"id_ed25519", "id_two"}) {
        GenerateRequest req;
        req.path = tmp.dir / name;
        REQUIRE(generate(req).ok());
    }
    // A .pub that is not actually a key must be ignored, not fail the listing.
    { std::ofstream(tmp.dir / "authorized_keys.pub") << "not a real key\n"; }

    const auto r = list_keys(tmp.dir);
    REQUIRE(r.ok());
    CHECK(r.value().size() == 2);
}

TEST_CASE("list_keys on a missing directory is empty, not an error") {
    if (!ssh_keygen_available()) SKIP("ssh-keygen not found on PATH");
    TempDir tmp;
    const auto r = list_keys(tmp.dir / "no-such-subdir");
    REQUIRE(r.ok());
    CHECK(r.value().empty());
}

TEST_CASE("a missing ssh-keygen binary is reported, not crashed") {
    TempDir tmp;
    SshConfig cfg;
    cfg.keygen_binary = "repomancer-no-such-ssh-keygen-7f3a";
    GenerateRequest req;
    req.path = tmp.dir / "id_ed25519";
    const auto r = generate(req, cfg);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == SshError::Kind::ExeNotFound);
}

TEST_CASE("is_encrypted distinguishes protected from plain keys") {
    if (!ssh_keygen_available()) SKIP("ssh-keygen not found on PATH");
    TempDir tmp;
    GenerateRequest plain;
    plain.path = tmp.dir / "plain";
    REQUIRE(generate(plain).ok());
    GenerateRequest prot;
    prot.path = tmp.dir / "prot";
    prot.passphrase = "guardme";
    REQUIRE(generate(prot).ok());

    REQUIRE(is_encrypted(plain.path).ok());
    CHECK_FALSE(is_encrypted(plain.path).value());
    REQUIRE(is_encrypted(prot.path).ok());
    CHECK(is_encrypted(prot.path).value());
    // A missing key is an error, not a silent "false".
    CHECK_FALSE(is_encrypted(tmp.dir / "ghost").ok());
}

TEST_CASE("change_passphrase re-keys, removes, and adds protection") {
    if (!ssh_keygen_available()) SKIP("ssh-keygen not found on PATH");
    TempDir tmp;
    GenerateRequest req;
    req.path = tmp.dir / "id_ed25519";
    req.passphrase = "first-pass";
    REQUIRE(generate(req).ok());

    // Change the passphrase.
    REQUIRE(change_passphrase(req.path, "first-pass", "second-pass").ok());
    CHECK(passphrase_opens(req.path, "second-pass"));
    CHECK_FALSE(passphrase_opens(req.path, "first-pass"));

    // Remove it (empty new passphrase) → the key is now unencrypted.
    REQUIRE(change_passphrase(req.path, "second-pass", "").ok());
    CHECK(passphrase_opens(req.path, ""));
    CHECK_FALSE(is_encrypted(req.path).value());

    // Add one back onto the now-plain key (old passphrase ignored).
    REQUIRE(change_passphrase(req.path, "", "third-pass").ok());
    CHECK(passphrase_opens(req.path, "third-pass"));
    CHECK_FALSE(passphrase_opens(req.path, ""));
}

TEST_CASE("change_passphrase with the wrong old passphrase fails") {
    if (!ssh_keygen_available()) SKIP("ssh-keygen not found on PATH");
    TempDir tmp;
    GenerateRequest req;
    req.path = tmp.dir / "id_ed25519";
    req.passphrase = "the-real-one";
    REQUIRE(generate(req).ok());

    CHECK_FALSE(change_passphrase(req.path, "the-wrong-one", "new").ok());
    // The key is untouched — the real passphrase still opens it.
    CHECK(passphrase_opens(req.path, "the-real-one"));
}
