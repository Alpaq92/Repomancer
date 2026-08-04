// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/vcs/stats.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace repomancer::vcs;

TEST_CASE("shortlog: counts, names and addresses") {
    const std::string data = "     9\tDemo <a@b.c>\n"
                             "     3\tAda Lovelace <ada@example.org>\n"
                             "     1\tNo Address\n";
    const auto result = parse_shortlog(data);
    REQUIRE(result.ok());
    const auto& people = result.value();
    REQUIRE(people.size() == 3);
    CHECK(people[0].commits == 9);
    CHECK(people[0].name == "Demo");
    CHECK(people[0].email == "a@b.c");
    CHECK(people[1].name == "Ada Lovelace");
    CHECK(people[1].email == "ada@example.org");
    // -s without -e omits the address entirely.
    CHECK(people[2].name == "No Address");
    CHECK(people[2].email.empty());
}

TEST_CASE("shortlog: malformed input is rejected") {
    CHECK_FALSE(parse_shortlog("no tab here\n").ok());
    CHECK_FALSE(parse_shortlog("many\tDemo <a@b.c>\n").ok());
    const auto empty = parse_shortlog("");
    REQUIRE(empty.ok());
    CHECK(empty.value().empty());
}

TEST_CASE("languages: extensions map to languages") {
    CHECK(language_for_path("src/main.cpp") == "C++");
    CHECK(language_for_path("core/x.h") == "C/C++ header");
    CHECK(language_for_path("a/b/c.py") == "Python");
    CHECK(language_for_path("CMakeLists.txt") == "CMake");
    CHECK(language_for_path("deep/path/CMakeLists.txt") == "CMake");
    CHECK(language_for_path("Makefile") == "Makefile");
    // Case is not significant.
    CHECK(language_for_path("A.CPP") == "C++");
    // Unknown or extensionless files count towards nothing.
    CHECK(language_for_path("LICENSE").empty());
    CHECK(language_for_path("data.bin").empty());
}

TEST_CASE("languages: vendored trees are excluded") {
    CHECK(language_for_path("third_party/foo/x.cpp").empty());
    CHECK(language_for_path("nested/vendor/y.py").empty());
    CHECK(language_for_path("node_modules/pkg/index.js").empty());
    // A path merely containing the word is still counted.
    CHECK(language_for_path("src/vendoring.cpp") == "C++");
}

TEST_CASE("languages: sizes are summed per language and ranked") {
    const std::string data =
        "100644 blob aaa      300\tsrc/a.cpp\n"
        "100644 blob bbb      100\tsrc/b.cpp\n"
        "100644 blob ccc      100\tscripts/run.py\n"
        "160000 commit ddd       -\tsubmodule\n"   // no size of its own
        "100644 blob eee       50\tREADME\n";      // unclassified
    const auto result = parse_ls_tree_sizes(data);
    REQUIRE(result.ok());
    const auto& stats = result.value();
    REQUIRE(stats.size() == 2);
    CHECK(stats[0].name == "C++");
    CHECK(stats[0].bytes == 400);
    CHECK(stats[1].name == "Python");
    CHECK(stats[1].bytes == 100);
    // Percentages are of the classified total, so they add up to 100.
    CHECK(stats[0].percent == Catch::Approx(80.0));
    CHECK(stats[1].percent == Catch::Approx(20.0));
}

TEST_CASE("languages: malformed tree entries are rejected") {
    CHECK_FALSE(parse_ls_tree_sizes("100644 blob aaa 300 no-tab-here\n").ok());
    const auto empty = parse_ls_tree_sizes("");
    REQUIRE(empty.ok());
    CHECK(empty.value().empty());
}
