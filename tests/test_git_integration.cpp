// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "fixture_repo.h"

#include <repomancer/vcs/git/git_driver.h>

#include <repomancer/process/process_runner.h>
#include <repomancer/vcs/patch.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>

using namespace repomancer::vcs;
using repomancer::test::FixtureRepo;
using repomancer::vcs::git::GitDriver;

namespace {

const StatusEntry* find_entry(const StatusSnapshot& snapshot, const std::string& path) {
    const auto it = std::find_if(snapshot.entries.begin(), snapshot.entries.end(),
                                 [&](const StatusEntry& e) { return e.path == path; });
    return it == snapshot.entries.end() ? nullptr : &*it;
}

// Raw git for the remote plumbing the driver has no business exposing
// (init --bare, remote add). Fails the test on error.
void raw_git(const std::filesystem::path& cwd, std::vector<std::string> args) {
    repomancer::proc::RunSpec spec;
    spec.exe = "git";
    spec.cwd = cwd;
    spec.args = std::move(args);
    const auto run = repomancer::proc::ProcessRunner::run(spec);
    INFO("git stderr: " << run.err);
    REQUIRE(run.ok());
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
        const auto result = driver.log(repo.path(), LogOptions{.max_count = 10, .rev = "HEAD", .path = {}});
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

    const auto log = driver.log(repo.path(), LogOptions{.max_count = 10, .rev = "HEAD", .path = {}});
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

TEST_CASE("git integration: a path scopes the log to the file's commits",
          "[integration]") {
    repomancer::test::FixtureRepo repo;
    repomancer::vcs::git::GitDriver driver;

    repomancer::vcs::LogOptions everything;
    const auto full = driver.log(repo.path(), everything);
    REQUIRE(full.ok());

    repomancer::vcs::LogOptions scoped;
    scoped.all_refs = false;
    scoped.path = "a.txt";
    const auto history = driver.log(repo.path(), scoped);
    REQUIRE(history.ok());

    REQUIRE(!history.value().empty());
    CHECK(history.value().size() < full.value().size());
    for (const auto& commit : history.value()) {
        const bool known =
            std::any_of(full.value().begin(), full.value().end(),
                        [&](const auto& c) { return c.hash == commit.hash; });
        CHECK(known);
    }
}

TEST_CASE("git integration: blame attributes every line to a known commit",
          "[integration]") {
    repomancer::test::FixtureRepo repo;
    repomancer::vcs::git::GitDriver driver;

    const auto full = driver.log(repo.path(), {});
    REQUIRE(full.ok());

    const auto blame = driver.blame(repo.path(), "a.txt");
    REQUIRE(blame.ok());
    REQUIRE(!blame.value().empty());
    for (const auto& line : blame.value()) {
        CHECK(line.hash.size() == 40);
        CHECK(!line.author.empty());
        const bool known =
            std::any_of(full.value().begin(), full.value().end(),
                        [&](const auto& c) { return c.hash == line.hash; });
        CHECK(known);
    }
}

TEST_CASE("git integration: worktree and staged diffs split by index side",
          "[integration]") {
    repomancer::test::FixtureRepo repo;
    repomancer::vcs::git::GitDriver driver;

    // The fixture leaves a.txt modified UNSTAGED and staged.txt added STAGED.
    const auto unstaged = driver.worktree_diff(repo.path());
    REQUIRE(unstaged.ok());
    const auto in = [](const auto& files, const char* path) {
        return std::any_of(files.begin(), files.end(),
                           [&](const auto& f) { return f.new_path == path; });
    };
    CHECK(in(unstaged.value(), "a.txt"));
    CHECK(!in(unstaged.value(), "staged.txt")); // index side only

    const auto staged = driver.staged_diff(repo.path());
    REQUIRE(staged.ok());
    CHECK(in(staged.value(), "staged.txt"));
    CHECK(!in(staged.value(), "a.txt"));
}

TEST_CASE("git integration: stage, unstage and commit round-trip",
          "[integration]") {
    repomancer::test::FixtureRepo repo;
    repomancer::vcs::git::GitDriver driver;

    const auto before = driver.log(repo.path(), {});
    REQUIRE(before.ok());

    // The fixture leaves a.txt modified but unstaged.
    REQUIRE(driver.stage(repo.path(), "a.txt").ok());
    auto status = driver.status(repo.path());
    REQUIRE(status.ok());
    const auto staged_state = [&](const char* path) {
        for (const auto& entry : status.value().entries) {
            if (entry.path == path) {
                return entry.x;
            }
        }
        return '?';
    };
    CHECK(staged_state("a.txt") == 'M');

    REQUIRE(driver.unstage(repo.path(), "a.txt").ok());
    status = driver.status(repo.path());
    REQUIRE(status.ok());
    CHECK(staged_state("a.txt") == '.');

    // Commit what the fixture already staged (staged.txt and the rename),
    // message via stdin — including a subject that looks like an option.
    REQUIRE(driver.stage(repo.path(), "a.txt").ok());
    const auto commit = driver.commit(repo.path(), "--not-an-option subject\n\nbody\n");
    if (!commit.ok()) {
        UNSCOPED_INFO("commit error: " << commit.error().message
                                       << " stderr: " << commit.error().stderr_excerpt);
    }
    REQUIRE(commit.ok());

    const auto after = driver.log(repo.path(), {});
    REQUIRE(after.ok());
    CHECK(after.value().size() == before.value().size() + 1);
    CHECK(after.value().front().subject == "--not-an-option subject");
}

TEST_CASE("git integration: branch switch and create", "[integration]") {
    repomancer::test::FixtureRepo repo;
    repomancer::vcs::git::GitDriver driver;

    // The fixture leaves the tree dirty; switching between main and feature
    // still works because the touched files do not conflict — and if git
    // refuses, the error must be clean, not a crash.
    const auto to_feature = driver.switch_branch(repo.path(), "feature");
    if (to_feature.ok()) {
        auto status = driver.status(repo.path());
        REQUIRE(status.ok());
        CHECK(status.value().branch.head == "feature");
        REQUIRE(driver.switch_branch(repo.path(), "main").ok());
    } else {
        CHECK(!to_feature.error().message.empty());
    }

    REQUIRE(driver.create_branch(repo.path(), "topic/new", false).ok());
    const auto refs = driver.refs(repo.path());
    REQUIRE(refs.ok());
    const bool created =
        std::any_of(refs.value().begin(), refs.value().end(),
                    [](const auto& r) { return r.short_name == "topic/new"; });
    CHECK(created);

    CHECK(!driver.switch_branch(repo.path(), "no-such-branch").ok());
}

TEST_CASE("git integration: stash save and pop round-trip", "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    GitDriver driver;

    // The fixture's tree is dirty (a.txt modified, staged.txt staged);
    // stashing sets the tracked changes aside…
    const auto saved = driver.stash_save(repo.path(), "wip: fixture state");
    if (!saved.ok()) {
        UNSCOPED_INFO("stash error: " << saved.error().stderr_excerpt);
    }
    REQUIRE(saved.ok());
    auto status = driver.status(repo.path());
    REQUIRE(status.ok());
    CHECK(find_entry(status.value(), "a.txt") == nullptr);

    const auto refs = driver.refs(repo.path());
    REQUIRE(refs.ok());
    const bool stash_listed =
        std::any_of(refs.value().begin(), refs.value().end(),
                    [](const auto& r) { return r.kind == RefKind::Stash; });
    CHECK(stash_listed);

    // …and popping brings them back.
    REQUIRE(driver.stash_pop(repo.path()).ok());
    status = driver.status(repo.path());
    REQUIRE(status.ok());
    CHECK(find_entry(status.value(), "a.txt") != nullptr);

    // No stash left: pop again fails with git's own words, not a crash.
    const auto empty_pop = driver.stash_pop(repo.path());
    REQUIRE(!empty_pop.ok());
    CHECK(!empty_pop.error().stderr_excerpt.empty());
}

TEST_CASE("git integration: tag creation lands on the requested commit",
          "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    GitDriver driver;

    const auto log = driver.log(repo.path(), {});
    REQUIRE(log.ok());
    REQUIRE(log.value().size() >= 2);
    const std::string target = log.value()[1].hash; // not the tip

    REQUIRE(driver.create_tag(repo.path(), "v-test", target).ok());
    const auto refs = driver.refs(repo.path());
    REQUIRE(refs.ok());
    const auto tag = std::find_if(refs.value().begin(), refs.value().end(),
                                  [](const auto& r) { return r.short_name == "v-test"; });
    REQUIRE(tag != refs.value().end());
    CHECK(tag->target == target);

    // A duplicate name is git's error to report, cleanly.
    CHECK(!driver.create_tag(repo.path(), "v-test", target).ok());
}

TEST_CASE("git integration: push, fetch and pull against a local bare remote",
          "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    GitDriver driver;

    // A bare sibling becomes origin; main gets an upstream there.
    const auto bare = repo.path().parent_path() /
                      (repo.path().filename().string() + "-remote.git");
    raw_git(repo.path().parent_path(), {"init", "--bare", "--quiet",
                                        bare.filename().string()});
    raw_git(repo.path(), {"remote", "add", "origin", bare.string()});

    const auto pushed = driver.push(repo.path());
    // A fresh clone has no upstream; the driver must surface git's advice…
    REQUIRE(!pushed.ok());
    CHECK(!pushed.error().stderr_excerpt.empty());

    // …and once the upstream exists, the three ops run clean.
    raw_git(repo.path(), {"push", "--quiet", "--set-upstream", "origin", "main"});
    REQUIRE(driver.push(repo.path()).ok());
    REQUIRE(driver.fetch(repo.path()).ok());
    const auto pulled = driver.pull(repo.path());
    if (!pulled.ok()) {
        UNSCOPED_INFO("pull error: " << pulled.error().stderr_excerpt);
    }
    REQUIRE(pulled.ok());

    std::filesystem::remove_all(bare);
}

TEST_CASE("git integration: staging one hunk of two leaves the file partially staged",
          "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    GitDriver driver;

    // A fresh many-line file, committed, then edited at both ends so the
    // combined diff has two well-separated hunks.
    {
        std::string body;
        for (int i = 1; i <= 30; ++i) {
            body += "line " + std::to_string(i) + "\n";
        }
        raw_git(repo.path(), {"stash", "push", "--include-untracked", "-m", "park"});
        std::ofstream(repo.path() / "wide.txt") << body;
        raw_git(repo.path(), {"add", "wide.txt"});
        raw_git(repo.path(), {"commit", "--quiet", "-m", "wide baseline"});
        body.replace(body.find("line 2\n"), 7, "LINE 2\n");
        body.replace(body.find("line 29\n"), 8, "LINE 29\n");
        std::ofstream(repo.path() / "wide.txt") << body;
    }

    const auto diff = driver.worktree_diff(repo.path(), "wide.txt");
    REQUIRE(diff.ok());
    REQUIRE(diff.value().size() == 1);
    const auto& file = diff.value()[0];
    REQUIRE(file.hunks.size() == 2);
    REQUIRE(supports_hunk_ops(file));

    // Stage only the first hunk…
    const std::string patch = single_hunk_patch(file, 0);
    REQUIRE(!patch.empty());
    const auto applied = driver.apply_patch(repo.path(), patch, /*cached=*/true,
                                            /*reverse=*/false);
    if (!applied.ok()) {
        UNSCOPED_INFO("apply error: " << applied.error().stderr_excerpt);
    }
    REQUIRE(applied.ok());

    // …and the file is now both staged and modified (partial staging).
    auto status = driver.status(repo.path());
    REQUIRE(status.ok());
    const auto* entry = find_entry(status.value(), "wide.txt");
    REQUIRE(entry != nullptr);
    CHECK(entry->x == 'M');
    CHECK(entry->y == 'M');

    // The unstaged diff re-derives against the NEW index — that is the
    // property the GUI relies on: after each stage the remaining hunks stay
    // stageable. Stage the survivor too.
    const auto rediff = driver.worktree_diff(repo.path(), "wide.txt");
    REQUIRE(rediff.ok());
    REQUIRE(rediff.value().size() == 1);
    REQUIRE(rediff.value()[0].hunks.size() == 1);
    REQUIRE(driver
                .apply_patch(repo.path(), single_hunk_patch(rediff.value()[0], 0),
                             /*cached=*/true, /*reverse=*/false)
                .ok());
    status = driver.status(repo.path());
    REQUIRE(status.ok());
    entry = find_entry(status.value(), "wide.txt");
    REQUIRE(entry != nullptr);
    CHECK(entry->x == 'M');
    CHECK(entry->y == '.');

    // Unstage one hunk from the staged side (HEAD-vs-index base).
    const auto staged = driver.staged_diff(repo.path(), "wide.txt");
    REQUIRE(staged.ok());
    REQUIRE(staged.value().size() == 1);
    REQUIRE(staged.value()[0].hunks.size() == 2);
    REQUIRE(driver
                .apply_patch(repo.path(), single_hunk_patch(staged.value()[0], 0),
                             /*cached=*/true, /*reverse=*/true)
                .ok());
    status = driver.status(repo.path());
    REQUIRE(status.ok());
    entry = find_entry(status.value(), "wide.txt");
    REQUIRE(entry != nullptr);
    CHECK(entry->x == 'M');
    CHECK(entry->y == 'M');

    // Discard the now-unstaged hunk from the working tree.
    const auto unstaged = driver.worktree_diff(repo.path(), "wide.txt");
    REQUIRE(unstaged.ok());
    REQUIRE(unstaged.value().size() == 1);
    REQUIRE(unstaged.value()[0].hunks.size() == 1);
    REQUIRE(driver
                .apply_patch(repo.path(), single_hunk_patch(unstaged.value()[0], 0),
                             /*cached=*/false, /*reverse=*/true)
                .ok());
    status = driver.status(repo.path());
    REQUIRE(status.ok());
    entry = find_entry(status.value(), "wide.txt");
    REQUIRE(entry != nullptr);
    CHECK(entry->y == '.');
}

TEST_CASE("git integration: whole-file discard restores HEAD in index and worktree",
          "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    GitDriver driver;

    // a.txt arrives modified from the fixture; stage part of that state so
    // both sides are dirty, then discard the lot.
    REQUIRE(driver.stage(repo.path(), "a.txt").ok());
    auto status = driver.status(repo.path());
    REQUIRE(status.ok());
    REQUIRE(find_entry(status.value(), "a.txt") != nullptr);

    REQUIRE(driver.discard_file(repo.path(), "a.txt").ok());
    status = driver.status(repo.path());
    REQUIRE(status.ok());
    CHECK(find_entry(status.value(), "a.txt") == nullptr);

    // Untracked files are outside restore's reach — git's error, not a hang.
    CHECK(!driver.discard_file(repo.path(), "untracked.txt").ok());
}

TEST_CASE("git integration: hunk ops round-trip CRLF and no-newline files",
          "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    GitDriver driver;
    raw_git(repo.path(), {"stash", "push", "--include-untracked", "-m", "park"});

    SECTION("CRLF line endings survive the parse/serialize round-trip") {
        std::ofstream(repo.path() / "dos.txt", std::ios::binary)
            << "one\r\ntwo\r\nthree\r\n";
        raw_git(repo.path(), {"add", "dos.txt"});
        raw_git(repo.path(), {"commit", "--quiet", "-m", "dos baseline"});
        std::ofstream(repo.path() / "dos.txt", std::ios::binary)
            << "one\r\nTWO\r\nthree\r\n";

        const auto diff = driver.worktree_diff(repo.path(), "dos.txt");
        REQUIRE(diff.ok());
        REQUIRE(diff.value().size() == 1);
        REQUIRE(supports_hunk_ops(diff.value()[0]));
        const std::string patch = single_hunk_patch(diff.value()[0], 0);
        const auto applied = driver.apply_patch(repo.path(), patch,
                                                /*cached=*/true, /*reverse=*/false);
        if (!applied.ok()) {
            UNSCOPED_INFO("apply error: " << applied.error().stderr_excerpt);
        }
        REQUIRE(applied.ok());
        const auto status = driver.status(repo.path());
        REQUIRE(status.ok());
        const auto* entry = find_entry(status.value(), "dos.txt");
        REQUIRE(entry != nullptr);
        CHECK(entry->x == 'M');
        CHECK(entry->y == '.');
    }

    SECTION("a file without trailing newline stages and discards cleanly") {
        std::ofstream(repo.path() / "noeol.txt", std::ios::binary) << "alpha\nomega";
        raw_git(repo.path(), {"add", "noeol.txt"});
        raw_git(repo.path(), {"commit", "--quiet", "-m", "noeol baseline"});
        std::ofstream(repo.path() / "noeol.txt", std::ios::binary) << "alpha\nOMEGA";

        // The diff carries TWO backslash markers (one per side).
        const auto diff = driver.worktree_diff(repo.path(), "noeol.txt");
        REQUIRE(diff.ok());
        REQUIRE(diff.value().size() == 1);
        const std::string patch = single_hunk_patch(diff.value()[0], 0);
        REQUIRE(driver
                    .apply_patch(repo.path(), patch, /*cached=*/true, /*reverse=*/false)
                    .ok());
        auto status = driver.status(repo.path());
        REQUIRE(status.ok());
        const auto* entry = find_entry(status.value(), "noeol.txt");
        REQUIRE(entry != nullptr);
        CHECK(entry->x == 'M');
        CHECK(entry->y == '.');

        // And back out of the index, then out of the worktree: pristine.
        REQUIRE(driver
                    .apply_patch(repo.path(), patch, /*cached=*/true, /*reverse=*/true)
                    .ok());
        REQUIRE(driver
                    .apply_patch(repo.path(), patch, /*cached=*/false, /*reverse=*/true)
                    .ok());
        status = driver.status(repo.path());
        REQUIRE(status.ok());
        CHECK(find_entry(status.value(), "noeol.txt") == nullptr);
    }

    SECTION("non-ASCII paths round-trip (quotepath off)") {
#ifdef _WIN32
        // A narrow "na\u00efve.txt" source literal is encoded differently
        // than git's UTF-8 view of the filename on Windows, so `git add`
        // cannot find the file this harness creates. The product's UTF-8
        // path handling is exercised on POSIX.
        SKIP("non-ASCII filename harness is POSIX-specific");
#else
        std::ofstream(repo.path() / "na\u00efve.txt") << "x\ny\n";
        raw_git(repo.path(), {"add", "na\u00efve.txt"});
        raw_git(repo.path(), {"commit", "--quiet", "-m", "utf8 baseline"});
        std::ofstream(repo.path() / "na\u00efve.txt") << "x\nY\n";

        const auto diff = driver.worktree_diff(repo.path());
        REQUIRE(diff.ok());
        REQUIRE(diff.value().size() == 1);
        CHECK(diff.value()[0].new_path == "na\u00efve.txt");
        REQUIRE(supports_hunk_ops(diff.value()[0]));
        REQUIRE(driver
                    .apply_patch(repo.path(), single_hunk_patch(diff.value()[0], 0),
                                 /*cached=*/true, /*reverse=*/false)
                    .ok());
#endif
    }
}

TEST_CASE("git integration: literal pathspecs defuse magic-named files",
          "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
#ifdef _WIN32
    // ":(glob)*" contains ':' and '*', which are illegal in a Windows
    // filename — the pathspec-magic attack cannot be constructed here.
    SKIP("magic-named files require a POSIX filename");
#else
    FixtureRepo repo;
    GitDriver driver;
    raw_git(repo.path(), {"stash", "push", "--include-untracked", "-m", "park"});

    // A tracked file literally named ":(glob)*" — legal bytes on Linux. As a
    // pathspec that string matches EVERYTHING unless magic is off; a discard
    // on it must not touch other files.
    std::ofstream(repo.path() / ":(glob)*") << "trap\n";
    raw_git(repo.path(), {"add", "--", ":(glob)*"});
    raw_git(repo.path(), {"commit", "--quiet", "-m", "trap file"});
    std::ofstream(repo.path() / "victim.txt") << "precious\n";
    raw_git(repo.path(), {"add", "victim.txt"});
    raw_git(repo.path(), {"commit", "--quiet", "-m", "victim"});
    std::ofstream(repo.path() / ":(glob)*") << "trap edited\n";
    std::ofstream(repo.path() / "victim.txt") << "precious edited\n";

    REQUIRE(driver.discard_file(repo.path(), ":(glob)*").ok());
    const auto status = driver.status(repo.path());
    REQUIRE(status.ok());
    CHECK(find_entry(status.value(), ":(glob)*") == nullptr); // discarded
    const auto* victim = find_entry(status.value(), "victim.txt");
    REQUIRE(victim != nullptr); // the other edit SURVIVED
    CHECK(victim->y == 'M');
#endif
}

TEST_CASE("git integration: a streaming fetch drives the chunk sink",
          "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    GitDriver driver;

    // A bare remote a commit ahead, so the fetch has objects to report.
    const auto bare = repo.path().parent_path() /
                      (repo.path().filename().string() + "-sremote.git");
    raw_git(repo.path().parent_path(),
            {"init", "--bare", "--quiet", bare.filename().string()});
    raw_git(repo.path(), {"remote", "add", "origin", bare.string()});
    raw_git(repo.path(), {"push", "--quiet", "--set-upstream", "origin", "HEAD:main"});
    // Advance the remote via a throwaway clone.
    const auto work = repo.path().parent_path() /
                      (repo.path().filename().string() + "-sremote-work");
    raw_git(repo.path().parent_path(),
            {"clone", "--quiet", "--branch", "main", bare.string(), work.string()});
    raw_git(work, {"config", "user.email", "w@t"});
    raw_git(work, {"config", "user.name", "w"});
    std::ofstream(work / "streamed.txt") << "hello from the remote\n";
    raw_git(work, {"add", "streamed.txt"});
    raw_git(work, {"commit", "--quiet", "-m", "remote advance"});
    raw_git(work, {"push", "--quiet", "origin", "HEAD:main"});

    std::string streamed;
    const auto fetched = driver.fetch(
        repo.path(),
        [&](std::string_view chunk, bool /*is_stderr*/) {
            streamed.append(chunk);
        },
        nullptr);
    if (!fetched.ok()) {
        UNSCOPED_INFO("fetch error: " << fetched.error().stderr_excerpt);
    }
    REQUIRE(fetched.ok());
    // git --progress writes object-counting lines to stderr; the sink saw them.
    CHECK(!streamed.empty());

    std::filesystem::remove_all(bare);
    std::filesystem::remove_all(work);
}

TEST_CASE("git integration: option-looking user strings stay data",
          "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    GitDriver driver;

    SECTION("stash message") {
        // Must stash the tracked change, leave untracked.txt on disk, and
        // record the message verbatim — not act as --include-untracked.
        REQUIRE(driver.stash_save(repo.path(), "--include-untracked").ok());
        const auto status = driver.status(repo.path());
        REQUIRE(status.ok());
        const auto* untracked = find_entry(status.value(), "untracked.txt");
        REQUIRE(untracked != nullptr);
        CHECK(untracked->kind == EntryKind::Untracked);
        REQUIRE(driver.stash_pop(repo.path()).ok());
    }

    SECTION("tag name") {
        const auto log = driver.log(repo.path(), {});
        REQUIRE(log.ok());
        const std::string target = log.value().front().hash;
        // Without --end-of-options this would SUCCEED as `git tag -f <hash>`.
        CHECK(!driver.create_tag(repo.path(), "-f", target).ok());
        CHECK(!driver.create_tag(repo.path(), "-d", target).ok());
        const auto refs = driver.refs(repo.path());
        REQUIRE(refs.ok());
        CHECK(std::none_of(refs.value().begin(), refs.value().end(),
                           [](const auto& r) { return r.kind == RefKind::Tag; }));
    }
}

TEST_CASE("git integration: read-only trust refuses every mutation up front",
          "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    repomancer::vcs::git::GitConfig config;
    config.trust = repomancer::vcs::git::RepoTrust::ReadOnly;
    GitDriver driver(config);

    // Reads still work…
    REQUIRE(driver.status(repo.path()).ok());
    REQUIRE(driver.log(repo.path(), {}).ok());
    REQUIRE(driver.worktree_diff(repo.path()).ok());

    // …every mutation refuses with the trust error, and no state changes.
    const auto before = driver.status(repo.path());
    REQUIRE(before.ok());
    const auto refused = [](const auto& result) {
        return !result.ok() &&
               result.error().kind == VcsError::Kind::UntrustedRepo;
    };
    CHECK(refused(driver.stage(repo.path(), "a.txt")));
    CHECK(refused(driver.unstage(repo.path(), "a.txt")));
    CHECK(refused(driver.commit(repo.path(), "nope")));
    CHECK(refused(driver.switch_branch(repo.path(), "feature")));
    CHECK(refused(driver.create_branch(repo.path(), "evil", false)));
    CHECK(refused(driver.stash_save(repo.path(), "")));
    CHECK(refused(driver.stash_pop(repo.path())));
    CHECK(refused(driver.create_tag(repo.path(), "t", "")));
    CHECK(refused(driver.discard_file(repo.path(), "a.txt")));
    CHECK(refused(driver.apply_patch(repo.path(), "--- a/x\n", true, false)));
    CHECK(refused(driver.fetch(repo.path())));
    CHECK(refused(driver.pull(repo.path())));
    CHECK(refused(driver.push(repo.path())));
    const auto after = driver.status(repo.path());
    REQUIRE(after.ok());
    CHECK(after.value().entries.size() == before.value().entries.size());
}

TEST_CASE("git integration: hostile fsmonitor config never executes",
          "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;

    // A hostile repo sets core.fsmonitor to a command; plain `git status`
    // would execute it. The driver must not, trusted or not (§13.1 — the
    // fsmonitor neutralization is unconditional).
    const auto marker = repo.path() / "PWNED";
    raw_git(repo.path(),
            {"config", "core.fsmonitor", "touch " + marker.string()});

    for (const auto trust : {repomancer::vcs::git::RepoTrust::Trusted,
                             repomancer::vcs::git::RepoTrust::ReadOnly}) {
        repomancer::vcs::git::GitConfig config;
        config.trust = trust;
        GitDriver driver(config);
        REQUIRE(driver.status(repo.path()).ok());
        REQUIRE(driver.log(repo.path(), {}).ok());
        std::error_code ec;
        CHECK(!std::filesystem::exists(marker, ec));
    }
}

TEST_CASE("git integration: untrusted read paths never execute textconv or hooks",
          "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    const auto tc = repo.path() / "PWNED_TEXTCONV";
    const auto hook = repo.path() / "PWNED_HOOK";
    // a.txt already arrives modified from the fixture, so a clean filter
    // would fire on the worktree diff.

    // A hostile repo wires a textconv diff driver and a post-index-change
    // hook, both to touch a marker.
    raw_git(repo.path(),
            {"config", "diff.evil.textconv", "touch " + tc.string() + "; cat"});
    const auto flt = repo.path() / "PWNED_FILTER";
    raw_git(repo.path(),
            {"config", "filter.evil.clean", "touch " + flt.string() + "; cat"});
    raw_git(repo.path(),
            {"config", "filter.evil.smudge", "touch " + flt.string() + "; cat"});
    std::ofstream(repo.path() / ".gitattributes")
        << "a.txt diff=evil\na.txt filter=evil\n";
    const auto hooksdir = repo.path() / "eviltrap";
    std::filesystem::create_directories(hooksdir);
    {
        std::ofstream h(hooksdir / "post-index-change");
        h << "#!/bin/sh\ntouch " << hook.string() << "\n";
    }
    std::filesystem::permissions(hooksdir / "post-index-change",
                                 std::filesystem::perms::owner_all);
    raw_git(repo.path(), {"config", "core.hooksPath", hooksdir.string()});

    repomancer::vcs::git::GitConfig config;
    config.trust = repomancer::vcs::git::RepoTrust::ReadOnly;
    // Filter driver names are arbitrary; the read_only_overrides step reads
    // the repo's own config and produces the "-c filter.evil.clean=" pairs
    // (exactly what the GUI does at the trust gate).
    {
        GitDriver probe(config);
        auto ov = probe.read_only_overrides(repo.path());
        REQUIRE(ov.ok());
        config.extra_neutralize = std::move(ov).value();
        CHECK(!config.extra_neutralize.empty());
    }
    GitDriver driver(config);

    // Every read path an untrusted repo exposes.
    REQUIRE(driver.status(repo.path()).ok());
    REQUIRE(driver.log(repo.path(), {}).ok());
    REQUIRE(driver.refs(repo.path()).ok());
    REQUIRE(driver.worktree_diff(repo.path()).ok());
    REQUIRE(driver.blame(repo.path(), "a.txt").ok());
    const auto commits = driver.log(repo.path(), {});
    REQUIRE(commits.ok());
    if (!commits.value().empty()) {
        (void)driver.changed_files(repo.path(), commits.value().front().hash);
        (void)driver.file_diff(repo.path(), commits.value().front().hash, "a.txt");
    }

    std::error_code ec;
    CHECK(!std::filesystem::exists(tc, ec));
    CHECK(!std::filesystem::exists(hook, ec));
    CHECK(!std::filesystem::exists(flt, ec));
}

TEST_CASE("git integration: without overrides a filter WOULD run (guard is real)",
          "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
#ifdef _WIN32
    // The proof filter "touch <path>; cat" is POSIX shell, and the Windows
    // marker path (backslashes) does not survive git's sh filter context.
    // The neutralization itself is platform-agnostic driver code.
    SKIP("the load-bearing-filter proof is POSIX-shell-specific");
#else
    FixtureRepo repo;
    const auto flt = repo.path() / "PWNED_FILTER";
    raw_git(repo.path(),
            {"config", "filter.evil.clean", "touch " + flt.string() + "; cat"});
    std::ofstream(repo.path() / ".gitattributes") << "a.txt filter=evil\n";

    // Trusted config (no extra_neutralize): the clean filter fires on diff —
    // proving the neutralization above is load-bearing, not a no-op.
    repomancer::vcs::git::GitConfig trusted;
    (void)GitDriver(trusted).worktree_diff(repo.path(), "a.txt");
    std::error_code ec;
    CHECK(std::filesystem::exists(flt, ec));
#endif
}

TEST_CASE("git integration: commit amend and sign-off", "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    GitDriver driver;
    using Options = repomancer::vcs::git::CommitOptions;

    // head_message reads HEAD's %B; the fixture's tip has a known subject.
    const auto head = driver.head_message(repo.path());
    REQUIRE(head.ok());
    CHECK(!head.value().empty());

    const auto before = driver.log(repo.path(), {});
    REQUIRE(before.ok());
    const std::string before_tip = before.value().front().hash;

    // Amend with a sign-off: HEAD is replaced (count unchanged), the subject
    // is the new one, and a Signed-off-by trailer is present.
    const auto amended = driver.commit(repo.path(), "reworded subject\n\nbody",
                                       Options{.amend = true, .signoff = true});
    if (!amended.ok()) {
        UNSCOPED_INFO("amend error: " << amended.error().stderr_excerpt);
    }
    REQUIRE(amended.ok());
    const auto after = driver.log(repo.path(), {});
    REQUIRE(after.ok());
    CHECK(after.value().size() == before.value().size()); // replaced, not added
    CHECK(after.value().front().hash != before_tip);      // new commit object
    CHECK(after.value().front().subject == "reworded subject");
    CHECK(after.value().front().body.find("Signed-off-by:") != std::string::npos);
}

TEST_CASE("git integration: read-only refuses an amend", "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    repomancer::vcs::git::GitConfig config;
    config.trust = repomancer::vcs::git::RepoTrust::ReadOnly;
    GitDriver driver(config);
    const auto r = driver.commit(repo.path(), "no", {/*amend=*/true, false, false});
    CHECK(!r.ok());
    CHECK(r.error().kind == VcsError::Kind::UntrustedRepo);
}

TEST_CASE("git integration: a clean merge fast-forwards or makes a merge commit",
          "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    GitDriver driver;
    // Start from a clean tree on main.
    raw_git(repo.path(), {"stash", "push", "--include-untracked", "-m", "park"});
    raw_git(repo.path(), {"switch", "-c", "topic", "main"});
    std::ofstream(repo.path() / "topic.txt") << "topic work\n";
    raw_git(repo.path(), {"add", "topic.txt"});
    raw_git(repo.path(), {"commit", "--quiet", "-m", "topic commit"});
    raw_git(repo.path(), {"switch", "main"});

    const auto before = driver.log(repo.path(), {});
    REQUIRE(before.ok());
    const auto merged = driver.merge(repo.path(), "topic", /*no_ff=*/true);
    if (!merged.ok()) {
        UNSCOPED_INFO("merge error: " << merged.error().stderr_excerpt);
    }
    REQUIRE(merged.ok());
    auto status = driver.status(repo.path());
    REQUIRE(status.ok());
    CHECK(!status.value().merging); // clean merge auto-committed
    const auto after = driver.log(repo.path(), {});
    REQUIRE(after.ok());
    // --no-ff adds a merge commit on top of topic's commit.
    CHECK(after.value().size() > before.value().size());
}

TEST_CASE("git integration: a conflicting merge is resolved and committed",
          "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    GitDriver driver;
    raw_git(repo.path(), {"stash", "push", "--include-untracked", "-m", "park"});

    // Two branches edit the same line of the same file → conflict.
    std::ofstream(repo.path() / "conf.txt") << "base\n";
    raw_git(repo.path(), {"add", "conf.txt"});
    raw_git(repo.path(), {"commit", "--quiet", "-m", "conf base"});
    raw_git(repo.path(), {"switch", "-c", "sideA", "main"});
    std::ofstream(repo.path() / "conf.txt") << "from A\n";
    raw_git(repo.path(), {"commit", "--quiet", "-am", "A edit"});
    raw_git(repo.path(), {"switch", "main"});
    std::ofstream(repo.path() / "conf.txt") << "from main\n";
    raw_git(repo.path(), {"commit", "--quiet", "-am", "main edit"});

    // The merge conflicts: non-zero, merge in progress, unmerged entry.
    const auto merged = driver.merge(repo.path(), "sideA", /*no_ff=*/false);
    CHECK(!merged.ok());
    auto status = driver.status(repo.path());
    REQUIRE(status.ok());
    CHECK(status.value().merging);
    const auto* conf = find_entry(status.value(), "conf.txt");
    REQUIRE(conf != nullptr);
    CHECK(conf->kind == EntryKind::Unmerged);

    // Resolve by taking THEIR side ("from A"), which differs from HEAD, so
    // the resolution shows as a staged change; the conflict is gone.
    REQUIRE(driver.checkout_conflict(repo.path(), "conf.txt", /*ours=*/false).ok());
    status = driver.status(repo.path());
    REQUIRE(status.ok());
    CHECK(status.value().merging);          // still merging until commit
    const auto* resolved = find_entry(status.value(), "conf.txt");
    REQUIRE(resolved != nullptr);
    CHECK(resolved->kind == EntryKind::Ordinary);
    CHECK(resolved->x != '.');               // staged
    REQUIRE(driver.commit(repo.path(), "merge sideA (took theirs)").ok());
    status = driver.status(repo.path());
    REQUIRE(status.ok());
    CHECK(!status.value().merging);         // merge finished
    CHECK(status.value().entries.empty());
}

TEST_CASE("git integration: an external merge tool resolves a conflict",
          "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
#ifdef _WIN32
    // The fake tool below is a POSIX-shell cmd; git mergetool runs it via sh.
    SKIP("the fake merge-tool cmd is POSIX-shell-specific");
#else
    FixtureRepo repo;
    GitDriver driver;
    raw_git(repo.path(), {"stash", "push", "--include-untracked", "-m", "park"});

    // A conflict on one line of one file.
    std::ofstream(repo.path() / "conf.txt") << "base\n";
    raw_git(repo.path(), {"add", "conf.txt"});
    raw_git(repo.path(), {"commit", "--quiet", "-m", "conf base"});
    raw_git(repo.path(), {"switch", "-c", "sideA", "main"});
    std::ofstream(repo.path() / "conf.txt") << "from A\n";
    raw_git(repo.path(), {"commit", "--quiet", "-am", "A edit"});
    raw_git(repo.path(), {"switch", "main"});
    std::ofstream(repo.path() / "conf.txt") << "from main\n";
    raw_git(repo.path(), {"commit", "--quiet", "-am", "main edit"});
    REQUIRE(!driver.merge(repo.path(), "sideA", /*no_ff=*/false).ok());

    // A non-interactive "tool": take THEIR side, trusting its exit code.
    raw_git(repo.path(),
            {"config", "mergetool.faketool.cmd", "cp \"$REMOTE\" \"$MERGED\""});
    raw_git(repo.path(), {"config", "mergetool.faketool.trustExitCode", "true"});

    const auto resolved = driver.resolve_with_tool(repo.path(), "conf.txt", "faketool");
    if (!resolved.ok()) {
        UNSCOPED_INFO("mergetool error: " << resolved.error().stderr_excerpt);
    }
    REQUIRE(resolved.ok());
    auto status = driver.status(repo.path());
    REQUIRE(status.ok());
    const auto* conf = find_entry(status.value(), "conf.txt");
    REQUIRE(conf != nullptr);
    CHECK(conf->kind == repomancer::vcs::EntryKind::Ordinary); // no longer Unmerged
    CHECK(conf->x != '.');                                     // staged as resolved
#endif
}

TEST_CASE("git integration: read-only refuses the merge tool", "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    repomancer::vcs::git::GitConfig config;
    config.trust = repomancer::vcs::git::RepoTrust::ReadOnly;
    GitDriver driver(config);
    const auto r = driver.resolve_with_tool(repo.path(), "a.txt", "meld");
    CHECK(!r.ok());
    CHECK(r.error().kind == VcsError::Kind::UntrustedRepo);
}

TEST_CASE("git integration: aborting a conflicting merge restores the tree",
          "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    GitDriver driver;
    raw_git(repo.path(), {"stash", "push", "--include-untracked", "-m", "park"});
    std::ofstream(repo.path() / "c.txt") << "base\n";
    raw_git(repo.path(), {"add", "c.txt"});
    raw_git(repo.path(), {"commit", "--quiet", "-m", "c base"});
    raw_git(repo.path(), {"switch", "-c", "other", "main"});
    std::ofstream(repo.path() / "c.txt") << "other\n";
    raw_git(repo.path(), {"commit", "--quiet", "-am", "other edit"});
    raw_git(repo.path(), {"switch", "main"});
    std::ofstream(repo.path() / "c.txt") << "mine\n";
    raw_git(repo.path(), {"commit", "--quiet", "-am", "my edit"});

    REQUIRE(!driver.merge(repo.path(), "other", /*no_ff=*/false).ok());
    REQUIRE(driver.status(repo.path()).value().merging);
    REQUIRE(driver.merge_abort(repo.path()).ok());
    const auto status = driver.status(repo.path());
    REQUIRE(status.ok());
    CHECK(!status.value().merging);
    CHECK(status.value().entries.empty()); // clean again
}

TEST_CASE("git integration: read-only refuses merge operations", "[integration]") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    repomancer::vcs::git::GitConfig config;
    config.trust = repomancer::vcs::git::RepoTrust::ReadOnly;
    GitDriver driver(config);
    const auto refused = [](const auto& r) {
        return !r.ok() && r.error().kind == VcsError::Kind::UntrustedRepo;
    };
    CHECK(refused(driver.merge(repo.path(), "feature", false)));
    CHECK(refused(driver.merge_abort(repo.path())));
    CHECK(refused(driver.checkout_conflict(repo.path(), "a.txt", true)));
}

TEST_CASE("git integration: clone copies a repository and streams progress") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    const auto dest =
        std::filesystem::temp_directory_path() / "repomancer-clone-fixture";
    std::error_code pre;
    std::filesystem::remove_all(dest, pre); // a previous run may have left it
    GitDriver driver;

    std::string streamed;
    const auto result = driver.clone(repo.path().string(), dest,
                                     [&](std::string_view chunk, bool) {
                                         streamed.append(chunk);
                                     });
    REQUIRE(result.ok());
    CHECK(std::filesystem::exists(dest / ".git"));
    // The clone is a working repository: its log carries the fixture history.
    const auto log = driver.log(dest, {});
    REQUIRE(log.ok());
    CHECK_FALSE(log.value().empty());

    std::error_code ec;
    std::filesystem::remove_all(dest, ec);
}

TEST_CASE("git integration: clone refuses an existing destination") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    GitDriver driver;
    // The fixture's own path certainly exists — cloning onto it must be
    // refused rather than letting git decide.
    const auto result = driver.clone(repo.path().string(), repo.path());
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().message.find("already exists") != std::string::npos);
}

TEST_CASE("git integration: clone rejects empty url or destination") {
    GitDriver driver;
    CHECK_FALSE(driver.clone("", "/tmp/repomancer-nowhere").ok());
    CHECK_FALSE(driver.clone("https://example.invalid/r.git", "").ok());
}

TEST_CASE("git integration: delete_branch removes a merged branch") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    GitDriver driver;
    REQUIRE(driver.create_branch(repo.path(), "throwaway", false).ok());

    const auto before = driver.refs(repo.path());
    REQUIRE(before.ok());
    const auto has = [](const auto& refs, const std::string& name) {
        return std::any_of(refs.begin(), refs.end(),
                           [&](const Ref& r) { return r.short_name == name; });
    };
    REQUIRE(has(before.value(), "throwaway"));

    REQUIRE(driver.delete_branch(repo.path(), "throwaway").ok());
    const auto after = driver.refs(repo.path());
    REQUIRE(after.ok());
    CHECK_FALSE(has(after.value(), "throwaway"));
}

TEST_CASE("git integration: delete_branch refuses unmerged work until forced") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    GitDriver driver;
    // `feature` in the fixture is merged; make a branch with a commit that is
    // not reachable from anywhere else.
    raw_git(repo.path(), {"checkout", "-q", "-b", "unmerged"});
    raw_git(repo.path(), {"-c", "user.email=a@b", "-c", "user.name=A", "commit",
                          "-q", "--allow-empty", "-m", "unmerged work"});
    raw_git(repo.path(), {"checkout", "-q", "main"});

    const auto refused = driver.delete_branch(repo.path(), "unmerged");
    REQUIRE_FALSE(refused.ok());
    // git's own words explain why, so the GUI can offer to force.
    CHECK(refused.error().stderr_excerpt.find("not fully merged") != std::string::npos);

    CHECK(driver.delete_branch(repo.path(), "unmerged", /*force=*/true).ok());
}

TEST_CASE("git integration: delete_branch refuses the checked-out branch") {
    if (!FixtureRepo::git_available()) {
        SKIP("git not found on PATH");
    }
    FixtureRepo repo;
    GitDriver driver;
    CHECK_FALSE(driver.delete_branch(repo.path(), "main").ok());
}
