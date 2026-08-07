// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Password-based encryption for the keyring-less fallback token store
// (implementation-plan.md §6): Argon2id derives a key from a master password,
// XChaCha20-Poly1305 seals the data. Only used where no OS keychain exists; on
// Windows/macOS/keyring-Linux the OS does at-rest crypto instead.

#pragma once

#include <repomancer/outcome.h>
#include <repomancer/secret/secure_buffer.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace repomancer::secret {

struct CryptoError {
    enum class Kind {
        BadFormat,     // the blob is structurally invalid
        WrongPassword, // KDF/AEAD rejected it (wrong password or tampering)
        TooLarge,      // input exceeds a sane bound
        Internal,      // libsodium reported an unexpected failure (e.g. OOM)
    };

    CryptoError() = default;
    CryptoError(Kind k, std::string msg) : kind(k), message(std::move(msg)) {}

    Kind kind{};
    std::string message;
};

template <typename T>
using CryptoResult = Outcome<T, CryptoError>;

// Argon2id work factor. The chosen level is recorded in the blob, so decrypt()
// always matches and the default can be retuned later without breaking old
// files. Interactive ≈ login-latency; Moderate is heavier, for at-rest data.
enum class KdfStrength { Interactive, Moderate };

// Seal `plaintext` under `password`. The returned blob is self-describing — a
// versioned header carrying the KDF params, salt and nonce — so decrypt() needs
// nothing but the blob and the password.
[[nodiscard]] CryptoResult<std::vector<std::uint8_t>> encrypt(
    const SecretString& plaintext, const SecretString& password,
    KdfStrength strength = KdfStrength::Moderate);

// Reverse of encrypt(). A wrong password or any tampering fails the AEAD tag →
// WrongPassword; a structurally invalid blob → BadFormat.
[[nodiscard]] CryptoResult<SecretString> decrypt(
    const std::vector<std::uint8_t>& blob, const SecretString& password);

} // namespace repomancer::secret
