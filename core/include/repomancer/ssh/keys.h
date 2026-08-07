// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// SSH key management, the wx-free core of the M3 key wizard
// (implementation-plan.md §7). ssh-keygen is the engine — the same OpenSSH
// tool present on Windows 10+, macOS and Linux — driven through ProcessRunner
// so every rule in §13.3 holds: argv exec only, no shell, and a passphrase
// travels on the child's stdin, never in argv or the environment.

#pragma once

#include <repomancer/outcome.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace repomancer::ssh {

enum class KeyType { Ed25519, Rsa, Ecdsa };

// The `-t` value ssh-keygen expects ("ed25519" / "rsa" / "ecdsa").
[[nodiscard]] const char* to_string(KeyType type);

// Parse a key type from an ssh-keygen algorithm token — the "(ED25519)" tail of
// a fingerprint line, or a "ssh-ed25519"/"ecdsa-sha2-*" public-key prefix.
// Case-insensitive; std::nullopt for anything unrecognized.
[[nodiscard]] std::optional<KeyType> key_type_from_string(std::string_view text);

// A key pair's identity, as read back from ssh-keygen and the .pub file. Paths
// are filled in where known: `inspect` sets whichever of the pair it can locate
// on disk; `generate` sets both.
struct KeyInfo {
    std::filesystem::path private_path;
    std::filesystem::path public_path;
    KeyType type = KeyType::Ed25519;
    int bits = 0;
    std::string comment;
    std::string fingerprint_sha256; // e.g. "SHA256:cjmSpY0w…"
    std::string public_key;         // full "ssh-ed25519 AAAA… comment" line
};

struct GenerateRequest {
    KeyType type = KeyType::Ed25519;
    // 0 = ssh-keygen's default for the type; ignored for Ed25519 (fixed size).
    int bits = 0;
    std::string comment;          // -C; omitted when empty (ssh-keygen default)
    std::filesystem::path path;   // -f: the private key; .pub is written beside it
    // Empty = an unencrypted key. A non-empty passphrase is delivered on stdin
    // with askpass disabled, so it never reaches argv or the environment.
    std::string passphrase;
};

// Which ssh-keygen to run and how long to wait — mirrors GitConfig so the
// binary is resolved from Preferences and tests can point at a stub.
struct SshConfig {
    std::filesystem::path keygen_binary = "ssh-keygen";
    std::chrono::milliseconds timeout{std::chrono::milliseconds(30'000)};
};

struct SshError {
    enum class Kind {
        ExeNotFound,    // configured ssh-keygen missing
        LaunchFailed,   // could not spawn
        NonZeroExit,    // ssh-keygen ran and reported failure
        Timeout,
        Cancelled,
        ParseError,     // output did not match the expected format
        InvalidRequest, // caught before spawning (e.g. target already exists)
        IoError,        // a filesystem read/write failed (config, known_hosts)
    };

    SshError() = default;
    SshError(Kind k, std::string msg, int exit = 0, std::string stderr_ex = {})
        : kind(k), message(std::move(msg)), exit_code(exit),
          stderr_excerpt(std::move(stderr_ex)) {}

    Kind kind{};
    std::string message;
    int exit_code = 0;
    std::string stderr_excerpt; // truncated; for diagnostics, never parsed
};

template <typename T>
using SshResult = Outcome<T, SshError>;

// Generate a new key pair. Refuses (InvalidRequest) if the private key or its
// .pub already exists — it never clobbers a key. A missing parent directory is
// created. On success the returned KeyInfo is the freshly generated key.
[[nodiscard]] SshResult<KeyInfo> generate(const GenerateRequest& req,
                                          const SshConfig& cfg = {});

// Read a key's type/bits/comment/fingerprint via `ssh-keygen -l -f`. Accepts
// either the private or the public file; works on passphrase-protected keys
// (no passphrase is needed to fingerprint). The full public-key line is filled
// in when the .pub is on disk.
[[nodiscard]] SshResult<KeyInfo> inspect(const std::filesystem::path& key,
                                         const SshConfig& cfg = {});

// Every key pair discoverable in `dir` — one per *.pub — inspected and sorted
// by path. A missing directory is not an error (returns empty). A .pub that is
// not a key is skipped; an environmental failure (missing binary, timeout) is
// reported.
[[nodiscard]] SshResult<std::vector<KeyInfo>> list_keys(
    const std::filesystem::path& dir, const SshConfig& cfg = {});

// ~/.ssh (or %USERPROFILE%\.ssh). Empty path if the home directory is unset.
[[nodiscard]] std::filesystem::path default_key_dir();

// True if the private key is passphrase-protected. An absent/unreadable key is
// an error, not "unencrypted".
[[nodiscard]] SshResult<bool> is_encrypted(const std::filesystem::path& private_key,
                                           const SshConfig& cfg = {});

// Change a key's passphrase in place (ssh-keygen -p). For an encrypted key
// `old_passphrase` must decrypt it; for an unencrypted key it is ignored. An
// empty `new_passphrase` removes protection. Both passphrases travel on stdin,
// never in argv or the environment (§13.3).
[[nodiscard]] SshResult<std::monostate> change_passphrase(
    const std::filesystem::path& private_key, const std::string& old_passphrase,
    const std::string& new_passphrase, const SshConfig& cfg = {});

} // namespace repomancer::ssh
