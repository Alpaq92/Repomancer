// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/ssh/connection.h>

#include "fingerprint.h" // detail::trim / detail::rstrip
#include "run_error.h"   // detail::from_run_failure

#include <repomancer/process/process_runner.h>

#include <string>
#include <string_view>
#include <vector>

namespace repomancer::ssh {
namespace {

// The text between `after` and the next `until`, trimmed; empty if either is
// missing. Used to lift a username out of a fixed greeting.
std::string between(const std::string& text, const std::string& after, char until) {
    const auto p = text.find(after);
    if (p == std::string::npos) {
        return {};
    }
    const auto start = p + after.size();
    const auto end = text.find(until, start);
    if (end == std::string::npos) {
        return {};
    }
    return detail::trim(std::string_view(text).substr(start, end - start));
}

} // namespace

ConnectionResult parse_connection_response(const std::string& text) {
    ConnectionResult result;
    result.message = detail::rstrip(text);

    // GitHub: "Hi <user>! You've successfully authenticated, but GitHub does
    // not provide shell access."
    if (text.find("successfully authenticated") != std::string::npos) {
        result.authenticated = true;
        result.username = between(text, "Hi ", '!');
        return result;
    }
    // GitLab: "Welcome to GitLab, @<user>!"
    if (const auto gl = text.find("Welcome to GitLab"); gl != std::string::npos) {
        result.authenticated = true;
        result.username = between(text.substr(gl), "@", '!');
        return result;
    }
    // Anything else (Permission denied, unreachable, host-key failure) is a
    // failed probe; the message carries the reason for display.
    return result;
}

SshResult<ConnectionResult> test_connection(const std::string& destination,
                                            const ConnectionConfig& cfg) {
    proc::RunSpec spec;
    spec.exe = cfg.ssh_binary;
    spec.timeout = cfg.timeout;

    std::vector<std::string> args = {
        "-T",
        "-o", "BatchMode=yes", // never block on a prompt — fail instead
        "-o", "ConnectTimeout=" + std::to_string(cfg.connect_seconds),
        "-o", std::string("StrictHostKeyChecking=") +
                  (cfg.strict_host_key_checking ? "yes" : "accept-new"),
    };
    if (!cfg.identity_file.empty()) {
        args.push_back("-o");
        args.push_back("IdentitiesOnly=yes");
        args.push_back("-i");
        args.push_back(cfg.identity_file.string());
    }
    if (!cfg.known_hosts.empty()) {
        args.push_back("-o");
        args.push_back("UserKnownHostsFile=" + cfg.known_hosts.string());
    }
    args.push_back(destination);
    spec.args = std::move(args);
    // Belt-and-suspenders: never let ssh reach for a GUI askpass.
    spec.env_extra["SSH_ASKPASS_REQUIRE"] = "never";

    const auto run = proc::ProcessRunner::run(spec);
    if (run.status != proc::LaunchStatus::Ok) {
        return detail::from_run_failure(run, cfg.ssh_binary);
    }
    // Exit code is unreliable (GitHub exits 1 on success); the greeting decides.
    // ssh writes its greeting to stderr.
    return parse_connection_response(run.err + "\n" + run.out);
}

} // namespace repomancer::ssh
