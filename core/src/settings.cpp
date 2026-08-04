// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/settings.h>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace repomancer {

namespace {

constexpr std::size_t kMaxSettingsBytes = 64 * 1024; // §13.2 spirit: cap all parsed input
constexpr const char* kFileName = "settings.json";

std::filesystem::path home_relative(const char* suffix) {
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / suffix;
    }
    return std::filesystem::current_path();
}

} // namespace

std::filesystem::path default_config_dir() {
#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"); appdata != nullptr && *appdata != '\0') {
        return std::filesystem::path(appdata) / "Repomancer";
    }
    return home_relative("Repomancer");
#elif defined(__APPLE__)
    return home_relative("Library/Application Support") / "Repomancer";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
        return std::filesystem::path(xdg) / "repomancer";
    }
    return home_relative(".config") / "repomancer";
#endif
}

Settings load_settings(const std::filesystem::path& config_dir) {
    Settings settings;

    std::error_code ec;
    const auto file = config_dir / kFileName;
    if (!std::filesystem::exists(file, ec) || ec) {
        return settings;
    }
    if (std::filesystem::file_size(file, ec) > kMaxSettingsBytes || ec) {
        return settings;
    }

    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return settings;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();

    const auto json = nlohmann::json::parse(buffer.str(), nullptr, /*allow_exceptions=*/false);
    if (json.is_discarded() || !json.is_object()) {
        return settings;
    }

    if (const auto it = json.find("theme"); it != json.end() && it->is_string()) {
        const auto value = it->get<std::string>();
        if (value == "system" || value == "light" || value == "dark") {
            settings.theme = value;
        }
    }
    if (const auto it = json.find("graph_style"); it != json.end() && it->is_string()) {
        const auto value = it->get<std::string>();
        if (value == "rounded" || value == "angular") {
            settings.graph_style = value;
        }
    }
    return settings;
}

bool save_settings(const Settings& settings, const std::filesystem::path& config_dir) {
    std::error_code ec;
    std::filesystem::create_directories(config_dir, ec);
    if (ec) {
        return false;
    }

    const nlohmann::json json = {
        {"theme", settings.theme},
        {"graph_style", settings.graph_style},
    };

    const auto file = config_dir / kFileName;
    const auto temp = config_dir / (std::string(kFileName) + ".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << json.dump(2) << '\n';
        if (!out.flush()) {
            return false;
        }
    }
    std::filesystem::rename(temp, file, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

} // namespace repomancer
