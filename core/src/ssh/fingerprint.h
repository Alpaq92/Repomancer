// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Internal (not shipped in include/): parse one fingerprint line as emitted by
// both `ssh-keygen -l -f` and `ssh-add -l` — they share the exact format
//   <bits> SHA256:<hash> <comment…> (<ALGO>)
// so key inspection and agent listing parse it through the same code.

#pragma once

#include <repomancer/ssh/keys.h>

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace repomancer::ssh::detail {

inline std::string rstrip(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    return s;
}

inline std::string trim(std::string_view v) {
    std::size_t b = 0, e = v.size();
    while (b < e && std::isspace(static_cast<unsigned char>(v[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(v[e - 1]))) --e;
    return std::string(v.substr(b, e - b));
}

struct Fingerprint {
    int bits = 0;
    std::string fingerprint_sha256;
    std::string comment;
    KeyType type = KeyType::Ed25519;
};

// Robust to a comment containing spaces: the algorithm is the final
// parenthesized token; everything between the fingerprint and it is the
// comment. std::nullopt if the line does not match the expected shape.
inline std::optional<Fingerprint> parse_fingerprint_line(const std::string& raw) {
    const std::string line = rstrip(raw);
    const auto lp = line.rfind('(');
    const auto rp = line.rfind(')');
    if (lp == std::string::npos || rp == std::string::npos || rp < lp) {
        return std::nullopt;
    }
    const auto type = key_type_from_string(line.substr(lp + 1, rp - lp - 1));
    if (!type) {
        return std::nullopt;
    }
    const std::string head = trim(std::string_view(line).substr(0, lp));
    const auto sp1 = head.find(' ');
    if (sp1 == std::string::npos) {
        return std::nullopt;
    }
    const auto sp2 = head.find(' ', sp1 + 1);

    Fingerprint fp;
    try {
        fp.bits = std::stoi(head.substr(0, sp1));
    } catch (const std::exception&) {
        return std::nullopt;
    }
    if (sp2 == std::string::npos) {
        fp.fingerprint_sha256 = head.substr(sp1 + 1);
    } else {
        fp.fingerprint_sha256 = head.substr(sp1 + 1, sp2 - sp1 - 1);
        fp.comment = trim(std::string_view(head).substr(sp2 + 1));
    }
    fp.type = *type;
    return fp;
}

} // namespace repomancer::ssh::detail
