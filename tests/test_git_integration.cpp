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

TEST_CASE("git integration: changed files and patches", "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    GitDriver driver;

    const auto log = driver.log(repo.path(), LogOptions{.max_count = 10, .rev = "HEAD"});
    REQUIRE(log.ok());
    const auto& commits = log.value();
    REQUIRE(commits.size() >= 4);

    const auto find_by_subject = [&](const char* subject) -> const Commit* {
        const auto it = std::find_if(commits.begin(), commits.end(), [&](const Commit& c) {
            return c.subject == subject;
        });
        return it == commits.end() ? nullptr : &*it;
    };

    SECTION("an ordinary commit lists what it added") {
        const auto* commit = find_by_subject("Add b");
        REQUIRE(commit != nullptr);
        const auto files = driver.changed_files(repo.path(), commit->hash);
        REQUIRE(files.ok());
        REQUIRE(files.value().size() == 1);
        CHECK(files.value()[0].change == FileChange::Added);
        CHECK(files.value()[0].path == "b.txt");
    }

    SECTION("the root commit is diffed against the empty tree") {
        const auto* root = find_by_subject("Initial commit");
        REQUIRE(root != nullptr);
        const auto files = driver.changed_files(repo.path(), root->hash);
        REQUIRE(files.ok());
        REQUIRE(files.value().size() == 1);
        CHECK(files.value()[0].change == FileChange::Added);
        CHECK(files.value()[0].path == "a.txt");
    }

    SECTION("a merge is diffed against its first parent rather than showing nothing") {
        const auto* merge = find_by_subject("Merge branch 'feature'");
        REQUIRE(merge != nullptr);
        REQUIRE(merge->parents.size() == 2);
        const auto files = driver.changed_files(repo.path(), merge->hash);
        REQUIRE(files.ok());
        CHECK_FALSE(files.value().empty());
    }

    SECTION("patch text parses into hunks with line numbers") {
        const auto* commit = find_by_subject("Add b");
        REQUIRE(commit != nullptr);
        const auto diff = driver.file_diff(repo.path(), commit->hash, "b.txt");
        REQUIRE(diff.ok());
        REQUIRE(diff.value().size() == 1);
        const auto& file = diff.value()[0];
        CHECK(file.new_path == "b.txt");
        REQUIRE(file.hunks.size() == 1);
        REQUIRE(file.hunks[0].lines.size() == 1);
        CHECK(file.hunks[0].lines[0].kind == DiffLineKind::Added);
        CHECK(file.hunks[0].lines[0].text == "bravo");
        CHECK(file.hunks[0].lines[0].new_lineno == 1);
    }

    SECTION("an unknown path yields an empty patch, not an error") {
        const auto* commit = find_by_subject("Add b");
        REQUIRE(commit != nullptr);
        const auto diff = driver.file_diff(repo.path(), commit->hash, "no/such/file");
        REQUIRE(diff.ok());
        CHECK(diff.value().empty());
    }
}

TEST_CASE("git integration: refs parse against real for-each-ref output", "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    GitDriver driver;

    // The parser is fed git's real record framing here — a unit fixture that
    // guessed the framing wrongly still passed while the sidebar came up
    // empty.
    const auto result = driver.refs(repo.path());
    REQUIRE(result.ok());
    const auto& refs = result.value();
    REQUIRE_FALSE(refs.empty());

    const auto find = [&](const std::string& short_name) {
        return std::find_if(refs.begin(), refs.end(), [&](const Ref& r) {
            return r.short_name == short_name && r.kind == RefKind::LocalBranch;
        });
    };
    REQUIRE(find("main") != refs.end());
    REQUIRE(find("feature") != refs.end());
    CHECK(find("main")->is_head);
    CHECK_FALSE(find("feature")->is_head);
    CHECK(find("main")->target.size() == 40);
}
