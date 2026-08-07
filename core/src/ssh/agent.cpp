// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/ssh/agent.h>

#include "fingerprint.h"
#include "run_error.h"

#include <repomancer/process/process_runner.h>

#include <utility>

namespace repomancer::ssh {
namespace {

// A RunSpec pointed at the configured ssh-add and, when set, a specific agent
// socket. LC_ALL=C keeps `-l` output in the stable ASCII form we parse.
proc::RunSpec base_spec(const AgentConfig& cfg) {
    proc::RunSpec spec;
    spec.exe = cfg.ssh_add_binary;
    spec.timeout = cfg.timeout;
    spec.env_extra["LC_ALL"] = "C";
    if (!cfg.auth_sock.empty()) {
        spec.env_extra["SSH_AUTH_SOCK"] = cfg.auth_sock;
    }
    return spec;
}

// ssh-add reports "Identity added/removed…" and the like on stderr; fall back
// to stdout. Trimmed for display.
std::string message_of(const proc::RunResult& run) {
    return detail::rstrip(run.err.empty() ? run.out : run.err);
}

SshResult<std::string> run_for_message(proc::RunSpec spec, const AgentConfig& cfg) {
    const auto run = proc::ProcessRunner::run(spec);
    if (!run.ok()) {
        return detail::from_run_failure(run, cfg.ssh_add_binary);
    }
    return message_of(run);
}

} // namespace

SshResult<AgentListing> agent_list(const AgentConfig& cfg) {
    proc::RunSpec spec = base_spec(cfg);
    spec.args = {"-l"};

    const auto run = proc::ProcessRunner::run(spec);
    // A launch failure is a real error; a nonzero *exit* is a reachability
    // state (ssh-add -l: 0 = has keys, 1 = empty, 2 = no agent).
    if (run.status != proc::LaunchStatus::Ok) {
        return detail::from_run_failure(run, cfg.ssh_add_binary);
    }
    AgentListing listing;
    if (run.exit_code == 1) {
        listing.state = AgentReachability::NoIdentities;
        return listing;
    }
    if (run.exit_code != 0) {
        listing.state = AgentReachability::Unavailable;
        return listing;
    }
    listing.state = AgentReachability::Running;
    // Parse identity lines (same format as `ssh-keygen -l`). ssh-add prints the
    // list on stdout; scan stderr too for robustness.
    const std::string text = run.out + "\n" + run.err;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto nl = text.find('\n', start);
        const std::string line =
            text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (const auto fp = detail::parse_fingerprint_line(line)) {
            listing.identities.push_back(
                {fp->type, fp->bits, fp->fingerprint_sha256, fp->comment});
        }
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return listing;
}

SshResult<std::string> agent_add(const std::filesystem::path& private_key,
                                 const std::string& passphrase,
                                 const AgentConfig& cfg) {
    proc::RunSpec spec = base_spec(cfg);
    spec.args = {private_key.string()};
    // Disable askpass unconditionally so a protected key can never wander off to
    // a GUI prompt (or hang) — ssh-add reads the passphrase from stdin instead.
    // For an unencrypted key ssh-add never prompts and the stdin line is unused.
    // The passphrase never appears in argv or the environment (§13.3).
    spec.env_extra["SSH_ASKPASS_REQUIRE"] = "never";
    spec.stdin_data = passphrase + "\n";
    return run_for_message(std::move(spec), cfg);
}

SshResult<std::string> agent_remove(const std::filesystem::path& private_key,
                                    const AgentConfig& cfg) {
    proc::RunSpec spec = base_spec(cfg);
    spec.args = {"-d", private_key.string()};
    return run_for_message(std::move(spec), cfg);
}

SshResult<std::string> agent_remove_all(const AgentConfig& cfg) {
    proc::RunSpec spec = base_spec(cfg);
    spec.args = {"-D"};
    return run_for_message(std::move(spec), cfg);
}

} // namespace repomancer::ssh
