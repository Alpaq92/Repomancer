// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// The wizard's final step (implementation-plan.md §7): probe SSH auth to a
// forge with `ssh -T` and report whether the key authenticated. The greeting
// parser is a pure function (thoroughly testable); the network probe wraps it.

#pragma once

#include <repomancer/ssh/keys.h> // SshError / SshResult

#include <chrono>
#include <filesystem>
#include <string>

namespace repomancer::ssh {

struct ConnectionResult {
    bool authenticated = false;
    std::string username; // parsed from the forge greeting when present
    std::string message;  // the server's greeting / error, for display
};

struct ConnectionConfig {
    std::filesystem::path ssh_binary = "ssh";
    std::chrono::milliseconds timeout{std::chrono::milliseconds(20'000)};
    int connect_seconds = 10;            // -o ConnectTimeout
    std::filesystem::path identity_file; // -i (empty ⇒ agent / ssh config default)
    std::filesystem::path known_hosts;   // -o UserKnownHostsFile (empty ⇒ default)
    bool strict_host_key_checking = true; // false ⇒ accept-new
};

// Parse an `ssh -T` greeting into a verdict. Recognises GitHub ("Hi <user>!
// You've successfully authenticated…") and GitLab ("Welcome to GitLab,
// @<user>!"), plus a generic "successfully authenticated" signal, and pulls out
// the username where the forge reports it. Pure — no process is spawned.
[[nodiscard]] ConnectionResult parse_connection_response(const std::string& text);

// Probe SSH authentication to `destination` (e.g. "git@github.com") with
// `ssh -T` in BatchMode (it never prompts). The exit code is not trusted —
// GitHub returns 1 even on success — so the verdict comes from the greeting.
// A launch failure is an SshError; an unreachable host or a rejected key is an
// ordinary result with authenticated == false. Network.
[[nodiscard]] SshResult<ConnectionResult> test_connection(
    const std::string& destination, const ConnectionConfig& cfg = {});

} // namespace repomancer::ssh
