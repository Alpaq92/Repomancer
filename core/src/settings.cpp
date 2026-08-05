// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/settings.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace repomancer {

namespace {

// §13.2 spirit: cap all parsed input. Sized for the trust store — hundreds
// of ~200-byte canonical repo paths must fit with room to spare, because an
// over-limit file degrades to defaults and the next save would PERSIST those
// defaults, wiping the user's configuration.
constexpr std::size_t kMaxSettingsBytes = 256 * 1024;
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
    if (const auto it = json.find("integrated_titlebar"); it != json.end() && it->is_boolean()) {
        settings.integrated_titlebar = it->get<bool>();
    }
    if (const auto it = json.find("topbar_buttons"); it != json.end() && it->is_number_integer()) {
        if (const int value = it->get<int>(); value >= 0 && value <= 2) {
            settings.topbar_buttons = value;
        }
    }
    if (const auto it = json.find("topbar_sharp_corners");
        it != json.end() && it->is_boolean()) {
        settings.topbar_sharp_corners = it->get<bool>();
    }
    if (const auto it = json.find("trusted_repos"); it != json.end() && it->is_array()) {
        for (const auto& item : *it) {
            if (item.is_string()) {
                if (const auto& value = item.get_ref<const std::string&>();
                    !value.empty()) {
                    settings.trusted_repos.push_back(value);
                }
            }
        }
        // Entries are appended chronologically; over the cap the OLDEST
        // trust decisions are the ones to forget.
        if (settings.trusted_repos.size() > kMaxTrustedRepos) {
            settings.trusted_repos.erase(
                settings.trusted_repos.begin(),
                settings.trusted_repos.end() - kMaxTrustedRepos);
        }
    }
    if (const auto it = json.find("recent_repos"); it != json.end() && it->is_array()) {
        for (const auto& entry : *it) {
            if (entry.is_string()) {
                if (const auto value = entry.get<std::string>(); !value.empty()) {
                    settings.recent_repos.push_back(value);
                }
            }
            if (settings.recent_repos.size() >= kMaxRecentRepos) {
                break;
            }
        }
    }
    if (const auto it = json.find("git_binary"); it != json.end() && it->is_string()) {
        // Free-form by design — it names an executable on this machine; what
        // matters is that it is only ever exec'd argv-style, never through a
        // shell (§13.1).
        if (const auto value = it->get<std::string>(); !value.empty()) {
            settings.git_binary = value;
        }
    }
    return settings;
}

bool is_repo_trusted(const Settings& settings, const std::string& path) {
    return std::find(settings.trusted_repos.begin(), settings.trusted_repos.end(),
                     path) != settings.trusted_repos.end();
}

void remember_trusted_repo(Settings& settings, const std::string& path) {
    if (path.empty() || is_repo_trusted(settings, path)) {
        return;
    }
    settings.trusted_repos.push_back(path);
    // Same eviction as load: the newest trust always survives.
    if (settings.trusted_repos.size() > kMaxTrustedRepos) {
        settings.trusted_repos.erase(settings.trusted_repos.begin());
    }
}

void remember_recent_repo(Settings& settings, const std::string& path) {
    if (path.empty()) {
        return;
    }
    auto& recent = settings.recent_repos;
    recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
    recent.insert(recent.begin(), path);
    if (recent.size() > kMaxRecentRepos) {
        recent.resize(kMaxRecentRepos);
    }
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
        {"git_binary", settings.git_binary},
        {"integrated_titlebar", settings.integrated_titlebar},
        {"topbar_buttons", settings.topbar_buttons},
        {"topbar_sharp_corners", settings.topbar_sharp_corners},
        {"recent_repos", settings.recent_repos},
        {"trusted_repos", settings.trusted_repos},
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
