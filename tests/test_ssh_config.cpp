// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// ssh config editing is pure filesystem work — no external tool — so these run
// unconditionally. They assert the managed block is added/updated/removed while
// unrelated content is preserved, and that edits are backed up.

#include <repomancer/ssh/config.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace repomancer::ssh;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    TempDir() {
        std::random_device rd;
        dir = fs::temp_directory_path() / ("repomancer-sshcfg-" + std::to_string(rd()));
        fs::create_directories(dir);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    fs::path dir;
};

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

std::size_t count_occurrences(const std::string& hay, const std::string& needle) {
    std::size_t n = 0;
    for (auto p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + 1)) {
        ++n;
    }
    return n;
}

} // namespace

TEST_CASE("config_set_identity creates the file and a readable block") {
    TempDir tmp;
    const fs::path cfg = tmp.dir / "config";
    const fs::path key = tmp.dir / "id_ed25519";

    const auto r = config_set_identity(cfg, "github.com", key);
    REQUIRE(r.ok());
    CHECK(r.value().backup_path.empty()); // nothing to back up on first write
    CHECK(fs::exists(cfg));

    const std::string text = read(cfg);
    CHECK(text.find("Host github.com") != std::string::npos);
    CHECK(text.find("IdentityFile " + key.generic_string()) != std::string::npos);
    CHECK(text.find("IdentitiesOnly yes") != std::string::npos);

    const auto got = config_identity(cfg, "github.com");
    REQUIRE(got.ok());
    REQUIRE(got.value().has_value());
    CHECK(got.value().value() == key);
}

TEST_CASE("config_identity is empty for an unmanaged host or missing file") {
    TempDir tmp;
    const fs::path cfg = tmp.dir / "config";
    // Missing file.
    auto none = config_identity(cfg, "github.com");
    REQUIRE(none.ok());
    CHECK_FALSE(none.value().has_value());
    // File exists, but only a user stanza we do not manage.
    { std::ofstream(cfg) << "Host example.com\n    User bob\n"; }
    auto still = config_identity(cfg, "example.com");
    REQUIRE(still.ok());
    CHECK_FALSE(still.value().has_value());
}

TEST_CASE("config_set_identity preserves other content and does not duplicate") {
    TempDir tmp;
    const fs::path cfg = tmp.dir / "config";
    { std::ofstream(cfg) << "Host example.com\n    User bob\n"; }

    REQUIRE(config_set_identity(cfg, "github.com", tmp.dir / "id_a").ok());
    // Re-point the same host: the block is replaced, not appended.
    const auto second = config_set_identity(cfg, "github.com", tmp.dir / "id_b");
    REQUIRE(second.ok());
    CHECK_FALSE(second.value().backup_path.empty()); // the prior file was saved

    const std::string text = read(cfg);
    CHECK(text.find("Host example.com") != std::string::npos); // user stanza kept
    CHECK(count_occurrences(text, "# >>> repomancer:github.com >>>") == 1);
    CHECK(count_occurrences(text, "Host github.com") == 1);

    const auto got = config_identity(cfg, "github.com");
    REQUIRE(got.value().has_value());
    CHECK(got.value().value() == tmp.dir / "id_b");
}

TEST_CASE("two managed hosts coexist independently") {
    TempDir tmp;
    const fs::path cfg = tmp.dir / "config";
    REQUIRE(config_set_identity(cfg, "github.com", tmp.dir / "id_gh").ok());
    REQUIRE(config_set_identity(cfg, "gitlab.com", tmp.dir / "id_gl").ok());

    CHECK(config_identity(cfg, "github.com").value().value() == tmp.dir / "id_gh");
    CHECK(config_identity(cfg, "gitlab.com").value().value() == tmp.dir / "id_gl");
}

TEST_CASE("config_remove drops only the managed block") {
    TempDir tmp;
    const fs::path cfg = tmp.dir / "config";
    { std::ofstream(cfg) << "Host keep.me\n    User carol\n"; }
    REQUIRE(config_set_identity(cfg, "github.com", tmp.dir / "id_a").ok());

    const auto r = config_remove(cfg, "github.com");
    REQUIRE(r.ok());
    const std::string text = read(cfg);
    CHECK(text.find("repomancer:github.com") == std::string::npos);
    CHECK(text.find("Host github.com") == std::string::npos);
    CHECK(text.find("Host keep.me") != std::string::npos); // untouched

    CHECK_FALSE(config_identity(cfg, "github.com").value().has_value());
}

TEST_CASE("a path with spaces is quoted") {
    TempDir tmp;
    const fs::path cfg = tmp.dir / "config";
    const fs::path key = tmp.dir / "my keys" / "id_ed25519";

    REQUIRE(config_set_identity(cfg, "github.com", key).ok());
    const std::string text = read(cfg);
    CHECK(text.find("IdentityFile \"" + key.generic_string() + "\"") != std::string::npos);
    // …and it round-trips unquoted through the reader.
    CHECK(config_identity(cfg, "github.com").value().value() == key);
}
