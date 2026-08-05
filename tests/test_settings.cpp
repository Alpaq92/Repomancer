// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/settings.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>

using repomancer::load_settings;
using repomancer::save_settings;
using repomancer::Settings;

namespace {

struct TempDir {
    TempDir() {
        std::random_device rd;
        dir = std::filesystem::temp_directory_path() /
              ("repomancer-settings-" + std::to_string(rd()));
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    std::filesystem::path dir;
};

} // namespace

TEST_CASE("settings: missing file yields defaults") {
    TempDir tmp;
    const auto settings = load_settings(tmp.dir);
    CHECK(settings.theme == "system");
}

TEST_CASE("settings: round-trip") {
    TempDir tmp;
    Settings settings;
    settings.theme = "dark";
    settings.git_binary = "/opt/git/bin/git";
    settings.integrated_titlebar = false;
    REQUIRE(save_settings(settings, tmp.dir));
    const auto loaded = load_settings(tmp.dir);
    CHECK(loaded.theme == "dark");
    CHECK(loaded.git_binary == "/opt/git/bin/git");
    CHECK(loaded.integrated_titlebar == false);

    settings.topbar_buttons = 2;
    settings.topbar_sharp_corners = true;
    REQUIRE(save_settings(settings, tmp.dir));
    CHECK(load_settings(tmp.dir).topbar_buttons == 2);
    CHECK(load_settings(tmp.dir).topbar_sharp_corners == true);
}

TEST_CASE("settings: empty git binary degrades to the default") {
    TempDir tmp;
    std::filesystem::create_directories(tmp.dir);
    std::ofstream(tmp.dir / "settings.json") << R"({"git_binary": ""})";
    CHECK(load_settings(tmp.dir).git_binary == "git");
}

TEST_CASE("settings: corrupt or invalid content degrades to defaults") {
    TempDir tmp;
    std::filesystem::create_directories(tmp.dir);

    SECTION("malformed JSON") {
        std::ofstream(tmp.dir / "settings.json") << "{ this is not json";
        CHECK(load_settings(tmp.dir).theme == "system");
    }
    SECTION("unknown theme value") {
        std::ofstream(tmp.dir / "settings.json") << R"({"theme":"purple"})";
        CHECK(load_settings(tmp.dir).theme == "system");
    }
    SECTION("non-object JSON") {
        std::ofstream(tmp.dir / "settings.json") << "[1,2,3]";
        CHECK(load_settings(tmp.dir).theme == "system");
    }
    SECTION("oversized file is refused") {
        std::ofstream out(tmp.dir / "settings.json");
        out << R"({"theme":"dark","pad":")" << std::string(70 * 1024, 'x') << R"("})";
        out.close();
        CHECK(load_settings(tmp.dir).theme == "system");
    }
}

TEST_CASE("settings: recent repositories dedupe, cap and round-trip") {
    TempDir tmp;
    Settings settings;
    for (int i = 0; i < 12; ++i) {
        repomancer::remember_recent_repo(settings, "/repo/" + std::to_string(i));
    }
    repomancer::remember_recent_repo(settings, "/repo/5"); // moves to front
    CHECK(settings.recent_repos.size() == 8);
    CHECK(settings.recent_repos.front() == "/repo/5");

    REQUIRE(save_settings(settings, tmp.dir));
    CHECK(load_settings(tmp.dir).recent_repos == settings.recent_repos);
}
