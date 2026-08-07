// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/ssh/keys.h>

#include "fingerprint.h"
#include "run_error.h"

#include <repomancer/process/process_runner.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <utility>
#include <variant>

namespace repomancer::ssh {
namespace {

std::string read_text_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::string all((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
    return detail::rstrip(std::move(all));
}

// Given the file the caller inspected, locate the pair on disk and read the
// public-key line if the .pub is present.
void fill_paths_and_pubkey(const std::filesystem::path& key, KeyInfo& info) {
    std::error_code ec;
    if (key.extension() == ".pub") {
        info.public_path = key;
        std::filesystem::path priv = key.parent_path() / key.stem();
        if (std::filesystem::exists(priv, ec)) info.private_path = priv;
    } else {
        info.private_path = key;
        std::filesystem::path pub(key.string() + ".pub");
        if (std::filesystem::exists(pub, ec)) info.public_path = pub;
    }
    if (!info.public_path.empty()) {
        info.public_key = read_text_file(info.public_path);
    }
}

} // namespace

const char* to_string(KeyType type) {
    switch (type) {
    case KeyType::Ed25519: return "ed25519";
    case KeyType::Rsa:     return "rsa";
    case KeyType::Ecdsa:   return "ecdsa";
    }
    return "ed25519";
}

std::optional<KeyType> key_type_from_string(std::string_view text) {
    std::string up(text);
    std::transform(up.begin(), up.end(), up.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (up.find("ED25519") != std::string::npos) return KeyType::Ed25519;
    if (up.find("ECDSA") != std::string::npos) return KeyType::Ecdsa;
    if (up.find("RSA") != std::string::npos) return KeyType::Rsa;
    return std::nullopt;
}

SshResult<KeyInfo> inspect(const std::filesystem::path& key, const SshConfig& cfg) {
    proc::RunSpec spec;
    spec.exe = cfg.keygen_binary;
    spec.timeout = cfg.timeout;
    spec.args = {"-l", "-f", key.string()};
    spec.env_extra["LC_ALL"] = "C"; // stable, ASCII fingerprint formatting

    const auto run = proc::ProcessRunner::run(spec);
    if (!run.ok()) {
        return detail::from_run_failure(run, cfg.keygen_binary);
    }
    const auto fp = detail::parse_fingerprint_line(run.out);
    if (!fp) {
        return SshError{SshError::Kind::ParseError,
                        "unrecognized ssh-keygen fingerprint output", 0,
                        detail::excerpt(run.out)};
    }
    KeyInfo info;
    info.type = fp->type;
    info.bits = fp->bits;
    info.comment = fp->comment;
    info.fingerprint_sha256 = fp->fingerprint_sha256;
    fill_paths_and_pubkey(key, info);
    return info;
}

SshResult<KeyInfo> generate(const GenerateRequest& req, const SshConfig& cfg) {
    if (req.path.empty()) {
        return SshError{SshError::Kind::InvalidRequest, "no output path given"};
    }
    std::error_code ec;
    const std::filesystem::path pub(req.path.string() + ".pub");
    if (std::filesystem::exists(req.path, ec) || std::filesystem::exists(pub, ec)) {
        return SshError{SshError::Kind::InvalidRequest,
                        "a key already exists at " + req.path.string()};
    }
    if (req.path.has_parent_path()) {
        // Best effort; if it truly can't be created ssh-keygen fails clearly.
        std::filesystem::create_directories(req.path.parent_path(), ec);
    }

    proc::RunSpec spec;
    spec.exe = cfg.keygen_binary;
    spec.timeout = cfg.timeout;
    std::vector<std::string> args = {"-t", to_string(req.type)};
    if (req.bits > 0 && req.type != KeyType::Ed25519) {
        args.push_back("-b");
        args.push_back(std::to_string(req.bits));
    }
    if (!req.comment.empty()) {
        args.push_back("-C");
        args.push_back(req.comment);
    }
    args.push_back("-f");
    args.push_back(req.path.string());
    args.push_back("-q");
    if (req.passphrase.empty()) {
        args.push_back("-N");
        args.push_back(""); // explicit: an unencrypted key
    } else {
        // The passphrase goes on stdin, entered twice for ssh-keygen's confirm
        // prompt. SSH_ASKPASS_REQUIRE=never (OpenSSH ≥ 8.4) stops ssh-keygen
        // from reaching for a GUI askpass when DISPLAY is set — without it, it
        // would ignore stdin and silently create an *unencrypted* key. The
        // secret never appears in argv or the inherited environment (§13.3).
        spec.stdin_data = req.passphrase + "\n" + req.passphrase + "\n";
        spec.env_extra["SSH_ASKPASS_REQUIRE"] = "never";
    }
    spec.args = std::move(args);

    const auto run = proc::ProcessRunner::run(spec);
    if (!run.ok()) {
        return detail::from_run_failure(run, cfg.keygen_binary);
    }
    // Read the freshly written key back for its canonical identity.
    return inspect(req.path, cfg);
}

SshResult<std::vector<KeyInfo>> list_keys(const std::filesystem::path& dir,
                                          const SshConfig& cfg) {
    std::vector<KeyInfo> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        return out; // missing/!dir is not an error — just nothing to list
    }
    std::vector<std::filesystem::path> pubs;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".pub") {
            pubs.push_back(entry.path());
        }
    }
    std::sort(pubs.begin(), pubs.end());
    for (const auto& p : pubs) {
        auto r = inspect(p, cfg);
        if (r.ok()) {
            out.push_back(std::move(r).value());
            continue;
        }
        // Environmental failures abort; a .pub that simply isn't a key (a
        // nonzero exit or unparseable output) is skipped.
        switch (r.error().kind) {
        case SshError::Kind::ExeNotFound:
        case SshError::Kind::LaunchFailed:
        case SshError::Kind::Timeout:
        case SshError::Kind::Cancelled:
            return std::move(r).error();
        default:
            break;
        }
    }
    return out;
}

std::filesystem::path default_key_dir() {
    for (const char* var : {"HOME", "USERPROFILE"}) {
        if (const char* home = std::getenv(var); home && *home) {
            return std::filesystem::path(home) / ".ssh";
        }
    }
    return {};
}

SshResult<bool> is_encrypted(const std::filesystem::path& private_key,
                             const SshConfig& cfg) {
    std::error_code ec;
    if (!std::filesystem::exists(private_key, ec)) {
        return SshError{SshError::Kind::InvalidRequest,
                        "no key at " + private_key.string()};
    }
    proc::RunSpec spec;
    spec.exe = cfg.keygen_binary;
    spec.timeout = cfg.timeout;
    // `-y -P ""` tries to load the key with an EMPTY passphrase; success means
    // it is not encrypted. -P supplies the passphrase so there is no prompt,
    // but disable askpass anyway for safety.
    spec.args = {"-y", "-P", "", "-f", private_key.string()};
    spec.env_extra["SSH_ASKPASS_REQUIRE"] = "never";

    const auto run = proc::ProcessRunner::run(spec);
    if (run.status != proc::LaunchStatus::Ok) {
        return detail::from_run_failure(run, cfg.keygen_binary);
    }
    return run.exit_code != 0; // could not load with an empty passphrase
}

SshResult<std::monostate> change_passphrase(const std::filesystem::path& private_key,
                                            const std::string& old_passphrase,
                                            const std::string& new_passphrase,
                                            const SshConfig& cfg) {
    const auto encrypted = is_encrypted(private_key, cfg);
    if (!encrypted.ok()) {
        return encrypted.error();
    }
    proc::RunSpec spec;
    spec.exe = cfg.keygen_binary;
    spec.timeout = cfg.timeout;
    spec.args = {"-p", "-f", private_key.string()};
    spec.env_extra["SSH_ASKPASS_REQUIRE"] = "never";
    // ssh-keygen -p prompts for the old passphrase only when the key is
    // currently encrypted, then the new passphrase twice — all on stdin, so the
    // secrets never reach argv or the environment.
    if (encrypted.value()) {
        spec.stdin_data =
            old_passphrase + "\n" + new_passphrase + "\n" + new_passphrase + "\n";
    } else {
        spec.stdin_data = new_passphrase + "\n" + new_passphrase + "\n";
    }

    const auto run = proc::ProcessRunner::run(spec);
    if (!run.ok()) {
        return detail::from_run_failure(run, cfg.keygen_binary);
    }
    return std::monostate{};
}

} // namespace repomancer::ssh
