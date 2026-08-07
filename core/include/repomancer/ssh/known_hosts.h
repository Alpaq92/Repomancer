// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// known_hosts / host-key verification for the M3 key wizard
// (implementation-plan.md §7): scan a host's keys, show SHA256 fingerprints for
// confirmation, and add/remove entries. The published forge fingerprints let
// the wizard recognise GitHub/GitLab automatically — but a non-match only ever
// means "ask the user", never "reject" (see the note in known_hosts.cpp).

#pragma once

#include <repomancer/ssh/keys.h> // KeyType / SshError / SshResult

#include <chrono>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace repomancer::ssh {

// One host key (one known_hosts line) with its fingerprint for display.
struct HostKey {
    std::string host;               // the host field of the line
    KeyType type = KeyType::Ed25519;
    std::string fingerprint_sha256; // "SHA256:…"
    std::string line;               // the raw known_hosts line, for adding
};

struct KnownHostsConfig {
    std::filesystem::path keygen_binary = "ssh-keygen";
    std::filesystem::path keyscan_binary = "ssh-keyscan";
    std::chrono::milliseconds timeout{std::chrono::milliseconds(15'000)};
    int scan_seconds = 5; // ssh-keyscan -T connection timeout
};

// Fingerprint every valid host-key line in `text` (a known_hosts file's
// contents, or ssh-keyscan output), pairing each with its raw line. Blank,
// comment and unparsable lines are skipped. Local — pipes through
// `ssh-keygen -lf -`; intended for the small blocks a scan produces.
[[nodiscard]] SshResult<std::vector<HostKey>> fingerprint_host_keys(
    const std::string& text, const KnownHostsConfig& cfg = {});

// Fetch a host's keys over the network (`ssh-keyscan`), fingerprinted for
// display. An unreachable host yields an empty list (ssh-keyscan exits 0 even
// then), so reachability is judged from emptiness, not an error.
[[nodiscard]] SshResult<std::vector<HostKey>> scan_host(
    const std::string& host, const KnownHostsConfig& cfg = {});

// Is `host` already present in `known_hosts`? (`ssh-keygen -F`.) A missing file
// is simply "no".
[[nodiscard]] SshResult<bool> known_hosts_contains(
    const std::filesystem::path& known_hosts, const std::string& host,
    const KnownHostsConfig& cfg = {});

// Append host-key lines to `known_hosts` (creating it and its parent), each
// newline-terminated. The caller decides whether to add (accept-new).
[[nodiscard]] SshResult<std::monostate> known_hosts_add(
    const std::filesystem::path& known_hosts,
    const std::vector<std::string>& lines);

// Remove every key for `host` (`ssh-keygen -R`); a no-op if the host or the
// file is absent.
[[nodiscard]] SshResult<std::monostate> known_hosts_remove(
    const std::filesystem::path& known_hosts, const std::string& host,
    const KnownHostsConfig& cfg = {});

// Published SHA256 fingerprints for a known forge ("github.com"/"gitlab.com"),
// empty for anything else. A match lets the wizard say "matches the published
// fingerprint" and offer one-click accept; a non-match MUST fall back to manual
// confirmation, never an automatic reject. Returned by value (the set is tiny,
// looked up rarely) — a reference return trips -Wdangling-reference at call
// sites that pass a string literal.
[[nodiscard]] std::vector<std::string> trusted_forge_fingerprints(
    const std::string& host);

// True if `fingerprint_sha256` is one of the published fingerprints for `host`.
[[nodiscard]] bool is_trusted_forge_key(const std::string& host,
                                        const std::string& fingerprint_sha256);

} // namespace repomancer::ssh
