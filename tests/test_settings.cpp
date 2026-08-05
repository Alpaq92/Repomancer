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
        out << R"({"theme":"dark","pad":")" << std::string(300 * 1024, 'x') << R"("})";
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

TEST_CASE("settings: trust store is idempotent and round-trips") {
    TempDir tmp;
    Settings settings;
    CHECK(!repomancer::is_repo_trusted(settings, "/home/u/repo"));
    repomancer::remember_trusted_repo(settings, "/home/u/repo");
    repomancer::remember_trusted_repo(settings, "/home/u/repo"); // no dupe
    repomancer::remember_trusted_repo(settings, "");              // ignored
    CHECK(settings.trusted_repos.size() == 1);
    CHECK(repomancer::is_repo_trusted(settings, "/home/u/repo"));
    CHECK(!repomancer::is_repo_trusted(settings, "/home/u/repo2"));

    REQUIRE(save_settings(settings, tmp.dir));
    const auto loaded = load_settings(tmp.dir);
    CHECK(loaded.trusted_repos == settings.trusted_repos);
    CHECK(repomancer::is_repo_trusted(loaded, "/home/u/repo"));
}

TEST_CASE("settings: trust store evicts the oldest, newest always survives") {
    TempDir tmp;
    Settings settings;
    for (std::size_t i = 0; i < repomancer::kMaxTrustedRepos + 10; ++i) {
        repomancer::remember_trusted_repo(settings, "/r/" + std::to_string(i));
    }
    CHECK(settings.trusted_repos.size() == repomancer::kMaxTrustedRepos);
    CHECK(!repomancer::is_repo_trusted(settings, "/r/0")); // oldest gone
    const std::string newest =
        "/r/" + std::to_string(repomancer::kMaxTrustedRepos + 9);
    CHECK(repomancer::is_repo_trusted(settings, newest));

    // And the newest survives a save/load round-trip, even at the cap.
    REQUIRE(save_settings(settings, tmp.dir));
    CHECK(repomancer::is_repo_trusted(load_settings(tmp.dir), newest));
}
