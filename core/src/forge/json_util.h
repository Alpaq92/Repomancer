// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Internal (not shipped in include/): defensive JSON access shared by the forge
// wrappers. Every forge response is attacker-influenced network data, so parse
// without exceptions and read fields type-checked — a wrong shape yields a
// nullopt / default, never a throw.

#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace repomancer::forge::detail {

// A JSON object parsed without exceptions; nullopt for invalid JSON or a
// non-object top level.
inline std::optional<nlohmann::json> parse_object(const std::string& body) {
    auto j = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        return std::nullopt;
    }
    return j;
}

inline std::string json_str(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string{};
}

inline int json_int(const nlohmann::json& j, const char* key, int fallback) {
    const auto it = j.find(key);
    return (it != j.end() && it->is_number_integer()) ? it->get<int>() : fallback;
}

// An integer-or-string id rendered as a string ("" if absent/other).
inline std::string json_id(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return {};
    }
    if (it->is_number_integer()) {
        return std::to_string(it->get<long long>());
    }
    return it->is_string() ? it->get<std::string>() : std::string{};
}

} // namespace repomancer::forge::detail
