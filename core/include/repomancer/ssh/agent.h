// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// ssh-agent interaction for the M3 key wizard (implementation-plan.md §7):
// list, add and remove identities via `ssh-add`. Reachability is a first-class
// state — an agent with no keys and no agent at all are ordinary outcomes, not
// errors. Adding a protected key delivers its passphrase on stdin with askpass
// disabled, so the secret never lands in argv or the environment (§13.3).

#pragma once

#include <repomancer/ssh/keys.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace repomancer::ssh {

// One identity currently loaded in the agent (a line of `ssh-add -l`).
struct AgentIdentity {
    KeyType type = KeyType::Ed25519;
    int bits = 0;
    std::string fingerprint_sha256;
    std::string comment;
};

enum class AgentReachability {
    Running,      // reachable and holding at least one identity
    NoIdentities, // reachable but empty
    Unavailable,  // no agent to talk to (SSH_AUTH_SOCK unset/dead)
};

struct AgentListing {
    AgentReachability state = AgentReachability::Unavailable;
    std::vector<AgentIdentity> identities;
};

struct AgentConfig {
    std::filesystem::path ssh_add_binary = "ssh-add";
    std::chrono::milliseconds timeout{std::chrono::milliseconds(15'000)};
    // The agent socket to talk to; empty inherits SSH_AUTH_SOCK from the parent
    // environment. Set explicitly to target a specific agent.
    std::string auth_sock;
};

// `ssh-add -l`. Reachability is reported in the result, not as an error: an
// empty agent (ssh-add exit 1) and an absent agent (exit 2) both return ok()
// with `state` set. Only a failure to launch ssh-add is an SshError.
[[nodiscard]] SshResult<AgentListing> agent_list(const AgentConfig& cfg = {});

// `ssh-add <key>`. A passphrase, when the key needs one, is fed on stdin with
// askpass disabled — never argv/env. Returns ssh-add's own message.
[[nodiscard]] SshResult<std::string> agent_add(
    const std::filesystem::path& private_key, const std::string& passphrase = {},
    const AgentConfig& cfg = {});

// `ssh-add -d <key>` — drop a single identity (by its key file).
[[nodiscard]] SshResult<std::string> agent_remove(
    const std::filesystem::path& private_key, const AgentConfig& cfg = {});

// `ssh-add -D` — drop every identity the agent holds.
[[nodiscard]] SshResult<std::string> agent_remove_all(const AgentConfig& cfg = {});

} // namespace repomancer::ssh
