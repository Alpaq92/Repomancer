// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/ssh/known_hosts.h>

#include "fingerprint.h"
#include "run_error.h"

#include <repomancer/process/process_runner.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace repomancer::ssh {
namespace {

namespace fs = std::filesystem;

SshError io_error(std::string msg) {
    return SshError{SshError::Kind::IoError, std::move(msg)};
}

// Fingerprint one line via `ssh-keygen -l -f -` (reads stdin). std::nullopt for
// a line ssh-keygen rejects; the environmental-failure case is signalled
// through `fatal` so the caller can abort rather than silently skip.
std::optional<detail::Fingerprint> fingerprint_line(const std::string& line,
                                                    const KnownHostsConfig& cfg,
                                                    SshError& fatal, bool& is_fatal) {
    is_fatal = false;
    proc::RunSpec spec;
    spec.exe = cfg.keygen_binary;
    spec.timeout = cfg.timeout;
    spec.args = {"-l", "-f", "-"};
    spec.env_extra["LC_ALL"] = "C";
    spec.stdin_data = line + "\n";

    const auto run = proc::ProcessRunner::run(spec);
    if (run.status != proc::LaunchStatus::Ok) {
        fatal = detail::from_run_failure(run, cfg.keygen_binary);
        is_fatal = true;
        return std::nullopt;
    }
    if (run.exit_code != 0) {
        return std::nullopt; // not a valid key line — skip it
    }
    return detail::parse_fingerprint_line(run.out);
}

} // namespace

SshResult<std::vector<HostKey>> fingerprint_host_keys(const std::string& text,
                                                      const KnownHostsConfig& cfg) {
    std::vector<HostKey> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto nl = text.find('\n', start);
        std::string line = detail::rstrip(text.substr(
            start, nl == std::string::npos ? std::string::npos : nl - start));
        const bool last = (nl == std::string::npos);
        if (!last) {
            start = nl + 1;
        }

        if (!line.empty() && line[0] != '#') {
            SshError fatal;
            bool is_fatal = false;
            if (const auto fp = fingerprint_line(line, cfg, fatal, is_fatal)) {
                // A known_hosts fingerprint line carries the host in the comment
                // position: "256 SHA256:… github.com (ED25519)".
                out.push_back({fp->comment, fp->type, fp->fingerprint_sha256, line});
            } else if (is_fatal) {
                return fatal;
            }
        }
        if (last) {
            break;
        }
    }
    return out;
}

SshResult<std::vector<HostKey>> scan_host(const std::string& host,
                                          const KnownHostsConfig& cfg) {
    proc::RunSpec spec;
    spec.exe = cfg.keyscan_binary;
    spec.timeout = cfg.timeout;
    spec.args = {"-T", std::to_string(cfg.scan_seconds), host};
    spec.env_extra["LC_ALL"] = "C";

    const auto run = proc::ProcessRunner::run(spec);
    // ssh-keyscan exits 0 even for an unreachable host (writing nothing), so a
    // launch failure is the only hard error; emptiness means "no keys / no
    // host". Keys land on stdout, diagnostics on stderr.
    if (run.status != proc::LaunchStatus::Ok) {
        return detail::from_run_failure(run, cfg.keyscan_binary);
    }
    return fingerprint_host_keys(run.out, cfg);
}

SshResult<bool> known_hosts_contains(const std::filesystem::path& known_hosts,
                                     const std::string& host,
                                     const KnownHostsConfig& cfg) {
    std::error_code ec;
    if (!fs::exists(known_hosts, ec)) {
        return false;
    }
    proc::RunSpec spec;
    spec.exe = cfg.keygen_binary;
    spec.timeout = cfg.timeout;
    spec.args = {"-F", host, "-f", known_hosts.string()};
    spec.env_extra["LC_ALL"] = "C";

    const auto run = proc::ProcessRunner::run(spec);
    if (run.status != proc::LaunchStatus::Ok) {
        return detail::from_run_failure(run, cfg.keygen_binary);
    }
    if (run.exit_code == 0) {
        return true; // found
    }
    if (run.exit_code == 1) {
        return false; // not found
    }
    return detail::from_run_failure(run, cfg.keygen_binary); // unexpected
}

SshResult<std::monostate> known_hosts_add(const std::filesystem::path& known_hosts,
                                          const std::vector<std::string>& lines) {
    std::error_code ec;
    if (known_hosts.has_parent_path()) {
        fs::create_directories(known_hosts.parent_path(), ec);
        fs::permissions(known_hosts.parent_path(), fs::perms::owner_all,
                        fs::perm_options::replace, ec);
    }
    std::ofstream out(known_hosts, std::ios::binary | std::ios::app);
    if (!out) {
        return io_error("could not open " + known_hosts.string() + " for writing");
    }
    for (const auto& line : lines) {
        const std::string l = detail::rstrip(line);
        if (!l.empty()) {
            out << l << '\n';
        }
    }
    out.close();
    if (out.fail()) {
        return io_error("could not write " + known_hosts.string());
    }
    return std::monostate{};
}

SshResult<std::monostate> known_hosts_remove(const std::filesystem::path& known_hosts,
                                             const std::string& host,
                                             const KnownHostsConfig& cfg) {
    std::error_code ec;
    if (!fs::exists(known_hosts, ec)) {
        return std::monostate{}; // nothing to remove
    }
    proc::RunSpec spec;
    spec.exe = cfg.keygen_binary;
    spec.timeout = cfg.timeout;
    spec.args = {"-R", host, "-f", known_hosts.string()};
    spec.env_extra["LC_ALL"] = "C";

    const auto run = proc::ProcessRunner::run(spec);
    // `ssh-keygen -R` exits 0 whether or not the host was present.
    if (run.status != proc::LaunchStatus::Ok || run.exit_code != 0) {
        return detail::from_run_failure(run, cfg.keygen_binary);
    }
    return std::monostate{};
}

// ---------------------------------------------------------------------------
// Published forge host-key fingerprints.
//
// Cross-checked 2026-08-07 against what the live servers present
// (`ssh-keyscan <host> | ssh-keygen -lf -`): all six matched. Re-verify against
// the published pages when host keys rotate (GitHub rotated its RSA host key on
// 2023-03-24 after a private-key exposure). A STALE value here is harmless:
// is_trusted_forge_key returning false only makes the wizard fall back to manual
// fingerprint confirmation; it never rejects or auto-accepts.
//
//   GitHub:  https://docs.github.com/authentication/keeping-your-account-and-data-secure/githubs-ssh-key-fingerprints
//   GitLab:  https://docs.gitlab.com/ee/user/gitlab_com/#ssh-host-keys-fingerprints
// ---------------------------------------------------------------------------
std::vector<std::string> trusted_forge_fingerprints(const std::string& host) {
    static const std::map<std::string, std::vector<std::string>> table = {
        {"github.com",
         {
             "SHA256:uNiVztksCsDhcc0u9e8BujQXVUpKZIDTMczCvj3tD2s", // ssh-rsa
             "SHA256:p2QAMXNIC1TJYWeIOttrVc98/R1BUFWu3/LiyKgUfQM", // ecdsa
             "SHA256:+DiY3wvvV6TuJJhbpZisF/zLDA0zPMSvHdkr4UvCOqU", // ssh-ed25519
         }},
        {"gitlab.com",
         {
             "SHA256:ROQFvPThGrW4RuWLoL9tq9I9zJ42fK4XywyRtbOz/EQ", // ssh-rsa
             "SHA256:HbW3g8zUjNSksFbqTiUWPWg2Bq1x8xdGUrliXFzSnUw", // ecdsa
             "SHA256:eUXGGm1YGsMAS7vkcx6JOJdOGHPem5gQp4taiCfCLB8", // ssh-ed25519
         }},
    };
    const auto it = table.find(host);
    return it == table.end() ? std::vector<std::string>{} : it->second;
}

bool is_trusted_forge_key(const std::string& host,
                          const std::string& fingerprint_sha256) {
    const auto known = trusted_forge_fingerprints(host);
    return std::find(known.begin(), known.end(), fingerprint_sha256) != known.end();
}

} // namespace repomancer::ssh
