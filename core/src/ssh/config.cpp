// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/ssh/config.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <variant>

namespace repomancer::ssh {
namespace {

namespace fs = std::filesystem;

SshError io_error(std::string msg) {
    return SshError{SshError::Kind::IoError, std::move(msg)};
}

std::string begin_marker(const std::string& host) {
    return "# >>> repomancer:" + host + " >>>";
}
std::string end_marker(const std::string& host) {
    return "# <<< repomancer:" + host + " <<<";
}

std::string rstrip_newlines(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    return s;
}

// The full content of `path`; std::nullopt if it does not exist (an absent
// config is simply empty, not an error). `failed` is set on a real read error.
std::optional<std::string> read_all(const fs::path& path, bool& failed) {
    failed = false;
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        failed = true;
        return std::nullopt;
    }
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    if (in.bad()) {
        failed = true;
        return std::nullopt;
    }
    return content;
}

// The Repomancer-managed block for `host`, or `content` unchanged if absent.
// Removes the marker lines and everything between them.
std::string strip_block(const std::string& content, const std::string& host) {
    const auto b = content.find(begin_marker(host));
    if (b == std::string::npos) {
        return content;
    }
    const auto e = content.find(end_marker(host), b);
    if (e == std::string::npos) {
        return content; // malformed (no end marker) — leave the file be
    }
    const auto line_end = content.find('\n', e);
    const std::size_t erase_end =
        (line_end == std::string::npos) ? content.size() : line_end + 1;
    return content.substr(0, b) + content.substr(erase_end);
}

std::string build_block(const std::string& host, const fs::path& identity_file) {
    std::string id = identity_file.generic_string();
    if (id.find_first_of(" \t") != std::string::npos) {
        id = "\"" + id + "\""; // ssh_config quotes paths containing spaces
    }
    return begin_marker(host) + "\n" +
           "Host " + host + "\n" +
           "    IdentityFile " + id + "\n" +
           "    IdentitiesOnly yes\n" +
           end_marker(host);
}

// Back up `path` to `<path>.repomancer.bak` if it exists. Returns the backup
// path (empty when there was nothing to back up).
SshResult<fs::path> backup(const fs::path& path) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return fs::path{};
    }
    fs::path bak = path;
    bak += ".repomancer.bak";
    fs::copy_file(path, bak, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        return io_error("could not back up " + path.string() + ": " + ec.message());
    }
    return bak;
}

// Write `content` to `path`, creating the parent directory. Best-effort tight
// permissions on POSIX (the dir 0700, the file 0600); ignored elsewhere.
SshResult<std::monostate> write_all(const fs::path& path, const std::string& content) {
    std::error_code ec;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
        fs::permissions(path.parent_path(), fs::perms::owner_all,
                        fs::perm_options::replace, ec);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return io_error("could not open " + path.string() + " for writing");
    }
    out << content;
    out.close();
    if (out.fail()) {
        return io_error("could not write " + path.string());
    }
    fs::permissions(path,
                    fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, ec);
    return std::monostate{};
}

// Shared body of set/remove: `mutate` turns the current content into the new.
template <typename Mutate>
SshResult<ConfigEdit> rewrite(const fs::path& config_path, Mutate&& mutate) {
    bool failed = false;
    const auto existing = read_all(config_path, failed);
    if (failed) {
        return io_error("could not read " + config_path.string());
    }
    const std::string new_content = mutate(existing.value_or(std::string{}));

    auto bak = backup(config_path);
    if (!bak.ok()) {
        return bak.error();
    }
    auto wrote = write_all(config_path, new_content);
    if (!wrote.ok()) {
        return wrote.error();
    }
    return ConfigEdit{new_content, std::move(bak).value()};
}

} // namespace

SshResult<std::optional<std::filesystem::path>> config_identity(
    const std::filesystem::path& config_path, const std::string& host) {
    bool failed = false;
    const auto content = read_all(config_path, failed);
    if (failed) {
        return io_error("could not read " + config_path.string());
    }
    if (!content) {
        return std::optional<std::filesystem::path>{};
    }
    const auto b = content->find(begin_marker(host));
    if (b == std::string::npos) {
        return std::optional<std::filesystem::path>{};
    }
    const auto e = content->find(end_marker(host), b);
    const auto key = content->find("IdentityFile", b);
    if (key == std::string::npos || (e != std::string::npos && key > e)) {
        return std::optional<std::filesystem::path>{};
    }
    const auto line_end = content->find('\n', key);
    std::string line = content->substr(key, line_end == std::string::npos
                                                ? std::string::npos
                                                : line_end - key);
    // Drop the "IdentityFile" keyword and surrounding whitespace/quotes.
    const auto sp = line.find_first_of(" \t");
    std::string value = (sp == std::string::npos) ? std::string{}
                                                  : rstrip_newlines(line.substr(sp));
    std::size_t s = 0, len = value.size();
    while (s < len && (value[s] == ' ' || value[s] == '\t')) ++s;
    value = value.substr(s);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return std::optional<std::filesystem::path>(value);
}

SshResult<ConfigEdit> config_set_identity(const std::filesystem::path& config_path,
                                          const std::string& host,
                                          const std::filesystem::path& identity_file) {
    if (host.empty()) {
        return SshError{SshError::Kind::InvalidRequest, "no host given"};
    }
    return rewrite(config_path, [&](const std::string& current) {
        std::string base = rstrip_newlines(strip_block(current, host));
        std::string out;
        if (!base.empty()) {
            out = base + "\n\n";
        }
        out += build_block(host, identity_file);
        out += "\n";
        return out;
    });
}

SshResult<ConfigEdit> config_remove(const std::filesystem::path& config_path,
                                    const std::string& host) {
    return rewrite(config_path, [&](const std::string& current) {
        std::string out = rstrip_newlines(strip_block(current, host));
        if (!out.empty()) {
            out += "\n";
        }
        return out;
    });
}

} // namespace repomancer::ssh
