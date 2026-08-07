// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/vcs/remote_url.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace repomancer::vcs {
namespace {

std::string trim(std::string_view v) {
    std::size_t b = 0, e = v.size();
    while (b < e && std::isspace(static_cast<unsigned char>(v[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(v[e - 1]))) --e;
    return std::string(v.substr(b, e - b));
}

// Split "owner/sub/repo(.git)" into the leading segment and the final name.
void split_path(std::string_view path, RemoteUrl& out) {
    while (!path.empty() && path.front() == '/') {
        path.remove_prefix(1);
    }
    while (!path.empty() && path.back() == '/') {
        path.remove_suffix(1);
    }
    if (path.empty()) {
        return;
    }
    const auto last = path.rfind('/');
    if (last == std::string_view::npos) {
        out.repo = std::string(path);
    } else {
        out.owner = std::string(path.substr(0, last));
        out.repo = std::string(path.substr(last + 1));
    }
    // Strip the conventional .git suffix from the repository name only.
    constexpr std::string_view kGit = ".git";
    if (out.repo.size() > kGit.size() &&
        out.repo.compare(out.repo.size() - kGit.size(), kGit.size(), kGit) == 0) {
        out.repo.erase(out.repo.size() - kGit.size());
    }
}

// Pull "user@host:port" apart. Returns false if no host survives.
bool split_authority(std::string_view authority, RemoteUrl& out) {
    if (const auto at = authority.rfind('@'); at != std::string_view::npos) {
        out.user = std::string(authority.substr(0, at));
        authority.remove_prefix(at + 1);
    }
    // An IPv6 literal is bracketed; its colons are not a port separator.
    if (!authority.empty() && authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string_view::npos) {
            return false;
        }
        out.host = std::string(authority.substr(0, close + 1));
        authority.remove_prefix(close + 1);
        if (!authority.empty() && authority.front() == ':') {
            out.port = std::atoi(std::string(authority.substr(1)).c_str());
        }
        return !out.host.empty();
    }
    if (const auto colon = authority.find(':'); colon != std::string_view::npos) {
        out.host = std::string(authority.substr(0, colon));
        out.port = std::atoi(std::string(authority.substr(colon + 1)).c_str());
    } else {
        out.host = std::string(authority);
    }
    return !out.host.empty();
}

} // namespace

std::optional<RemoteUrl> parse_remote_url(std::string_view url) {
    const std::string text = trim(url);
    if (text.empty()) {
        return std::nullopt;
    }
    std::string_view v(text);
    RemoteUrl out;

    // Explicit scheme.
    if (const auto sep = v.find("://"); sep != std::string_view::npos) {
        const std::string_view scheme = v.substr(0, sep);
        v.remove_prefix(sep + 3);
        if (scheme == "file") {
            return std::nullopt; // a local path, not a network remote
        }
        out.ssh = (scheme == "ssh" || scheme == "git+ssh");
        const auto slash = v.find('/');
        const std::string_view authority =
            slash == std::string_view::npos ? v : v.substr(0, slash);
        if (!split_authority(authority, out)) {
            return std::nullopt;
        }
        if (slash != std::string_view::npos) {
            split_path(v.substr(slash + 1), out);
        }
        return out;
    }

    // scp-like "user@host:path" — the colon separates host from path, and the
    // part after it is a path, never a port (that is what distinguishes this
    // form from ssh://host:port/path).
    const auto colon = v.find(':');
    if (colon == std::string_view::npos) {
        return std::nullopt; // a bare local path
    }
    // A Windows drive letter ("C:/repo") is a local path, not a host.
    if (colon == 1 && std::isalpha(static_cast<unsigned char>(v[0]))) {
        return std::nullopt;
    }
    const std::string_view authority = v.substr(0, colon);
    if (authority.find('/') != std::string_view::npos) {
        return std::nullopt; // "./path:with:colons" — still a local path
    }
    out.ssh = true;
    if (!split_authority(authority, out)) {
        return std::nullopt;
    }
    out.port = 0; // scp-like syntax cannot carry a port
    split_path(v.substr(colon + 1), out);
    return out;
}

} // namespace repomancer::vcs
