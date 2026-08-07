// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// core/secret: secure memory + the Argon2id/XChaCha20-Poly1305 fallback store.
// Uses the Interactive KDF strength so the crypto stays fast in CI.

#include <repomancer/secret/encrypted_store.h>
#include <repomancer/secret/secure_buffer.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace repomancer::secret;

TEST_CASE("SecureBuffer zero-fills, moves, and empties the source") {
    SecureBuffer buf(32);
    REQUIRE(buf.size() == 32);
    for (std::size_t i = 0; i < buf.size(); ++i) {
        CHECK(buf.data()[i] == 0); // sodium_malloc'd region, zero-filled by us
    }
    buf.data()[0] = 0xAB;

    SecureBuffer moved = std::move(buf);
    CHECK(moved.size() == 32);
    CHECK(moved.data()[0] == 0xAB);
    CHECK(buf.size() == 0);            // NOLINT(bugprone-use-after-move) intentional
    CHECK(buf.data() == nullptr);      // NOLINT(bugprone-use-after-move) intentional

    moved.reset();
    CHECK(moved.empty());
}

TEST_CASE("SecretString reveals and exposes its bytes intact") {
    SecretString s("hunter2");
    CHECK(s.size() == 7);
    CHECK(s.reveal() == "hunter2");
    CHECK(std::memcmp(s.bytes(), "hunter2", 7) == 0);
    CHECK_FALSE(s.empty());
    CHECK(SecretString().empty());
}

TEST_CASE("encrypt / decrypt round-trips the secret") {
    const SecretString secret("ghp_super_secret_token_value");
    const SecretString password("correct horse battery staple");

    auto blob = encrypt(secret, password, KdfStrength::Interactive);
    REQUIRE(blob.ok());
    CHECK(blob.value().size() > secret.size()); // header + AEAD tag overhead

    auto out = decrypt(blob.value(), password);
    REQUIRE(out.ok());
    CHECK(out.value().reveal() == "ghp_super_secret_token_value");
}

TEST_CASE("two encryptions of the same secret differ (fresh salt + nonce)") {
    const SecretString secret("token");
    const SecretString password("pw");
    auto a = encrypt(secret, password, KdfStrength::Interactive);
    auto b = encrypt(secret, password, KdfStrength::Interactive);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(a.value() != b.value());
}

TEST_CASE("decrypt with the wrong password is WrongPassword") {
    auto blob =
        encrypt(SecretString("data"), SecretString("right"), KdfStrength::Interactive);
    REQUIRE(blob.ok());
    auto out = decrypt(blob.value(), SecretString("wrong"));
    REQUIRE_FALSE(out.ok());
    CHECK(out.error().kind == CryptoError::Kind::WrongPassword);
}

TEST_CASE("a tampered blob fails the AEAD tag") {
    auto blob =
        encrypt(SecretString("data"), SecretString("pw"), KdfStrength::Interactive);
    REQUIRE(blob.ok());
    auto tampered = blob.value();
    tampered.back() ^= 0x01; // flip a bit of the ciphertext/tag
    auto out = decrypt(tampered, SecretString("pw"));
    REQUIRE_FALSE(out.ok());
    CHECK(out.error().kind == CryptoError::Kind::WrongPassword);
}

TEST_CASE("a structurally invalid blob is BadFormat") {
    const std::vector<std::uint8_t> junk(10, 0x00);
    auto out = decrypt(junk, SecretString("pw"));
    REQUIRE_FALSE(out.ok());
    CHECK(out.error().kind == CryptoError::Kind::BadFormat);
}
