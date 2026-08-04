// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "fixture_repo.h"

#include <repomancer/vcs/git/git_driver.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace repomancer::vcs;
using repomancer::test::FixtureRepo;
using repomancer::vcs::git::GitDriver;

namespace {

const StatusEntry* find_entry(const StatusSnapshot& snapshot, const std::string& path) {
    const auto it = std::find_if(snapshot.entries.begin(), snapshot.entries.end(),
                                 [&](const StatusEntry& e) { return e.path == path; });
    return it == snapshot.entries.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE("git integration: version, status and log on the fixture repo", "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    GitDriver driver;

    SECTION("version probe") {
        const auto version = driver.version();
        REQUIRE(version.ok());
        CHECK(version.value().major >= 2);
    }

    SECTION("status snapshot matches the fixture working state") {
        const auto result = driver.status(repo.path());
        REQUIRE(result.ok());
        const auto& snapshot = result.value();

        CHECK(snapshot.branch.head == "main");
        CHECK(snapshot.branch.upstream.empty());
        CHECK(snapshot.entries.size() == 4);

        const auto* modified = find_entry(snapshot, "a.txt");
        REQUIRE(modified != nullptr);
        CHECK(modified->kind == EntryKind::Ordinary);
        CHECK(modified->x == '.');
        CHECK(modified->y == 'M');

        const auto* staged = find_entry(snapshot, "staged.txt");
        REQUIRE(staged != nullptr);
        CHECK(staged->x == 'A');
        CHECK(staged->y == '.');

        const auto* renamed = find_entry(snapshot, "renamed.txt");
        REQUIRE(renamed != nullptr);
        CHECK(renamed->kind == EntryKind::RenamedCopied);
        CHECK(renamed->x == 'R');
        CHECK(renamed->orig_path == "b.txt");

        const auto* untracked = find_entry(snapshot, "untracked.txt");
        REQUIRE(untracked != nullptr);
        CHECK(untracked->kind == EntryKind::Untracked);
    }

    SECTION("log returns the four commits in topological order") {
        const auto result = driver.log(repo.path(), LogOptions{.max_count = 10, .rev = "HEAD"});
        REQUIRE(result.ok());
        const auto& commits = result.value();
        REQUIRE(commits.size() == 4);

        const auto& merge = commits.front();
        CHECK(merge.subject == "Merge branch 'feature'");
        CHECK(merge.parents.size() == 2);
        CHECK(merge.refs.find("HEAD -> main") != std::string::npos);
        CHECK(merge.commit_time == 1767272400); // 2026-01-01T13:00:00Z

        const auto& root = commits.back();
        CHECK(root.subject == "Initial commit");
        CHECK(root.parents.empty());
        CHECK(root.author_time == 1767261600); // 2026-01-01T10:00:00Z

        for (const auto& commit : commits) {
            CHECK(commit.author_name == FixtureRepo::kAuthorName);
            CHECK(commit.author_email == FixtureRepo::kAuthorEmail);
        }

        const auto has_subject = [&](const char* subject) {
            return std::any_of(commits.begin(), commits.end(),
                               [&](const Commit& c) { return c.subject == subject; });
        };
        CHECK(has_subject("Add b"));
        CHECK(has_subject("Feature work"));
    }
}
