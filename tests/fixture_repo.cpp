// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "fixture_repo.h"

#include <repomancer/process/process_runner.h>

#include <fstream>
#include <random>
#include <stdexcept>

namespace repomancer::test {

namespace {

std::filesystem::path make_temp_dir() {
    std::random_device rd;
    std::uniform_int_distribution<unsigned long long> dist;
    const auto name = "repomancer-test-" + std::to_string(dist(rd));
    auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::create_directories(dir);
    return dir;
}

// Distinct, fixed timestamps per commit keep the history deterministic.
std::map<std::string, std::string> commit_env(const std::string& iso_date) {
    return {
        {"GIT_AUTHOR_DATE", iso_date},
        {"GIT_COMMITTER_DATE", iso_date},
    };
}

} // namespace

bool FixtureRepo::git_available() {
    proc::RunSpec spec;
    spec.exe = "git";
    spec.args = {"--version"};
    spec.timeout = std::chrono::milliseconds(10'000);
    return proc::ProcessRunner::run(spec).ok();
}

void FixtureRepo::git(std::vector<std::string> args,
                      std::map<std::string, std::string> env_extra) {
    proc::RunSpec spec;
    spec.exe = "git";
    spec.cwd = dir_;
    spec.args = std::move(args);
    spec.env_extra = {
        {"GIT_AUTHOR_NAME", kAuthorName},
        {"GIT_AUTHOR_EMAIL", kAuthorEmail},
        {"GIT_COMMITTER_NAME", kAuthorName},
        {"GIT_COMMITTER_EMAIL", kAuthorEmail},
        {"GIT_TERMINAL_PROMPT", "0"},
        {"LC_ALL", "C"},
    };
    for (auto& [key, value] : env_extra) {
        spec.env_extra[key] = value;
    }
    spec.timeout = std::chrono::milliseconds(30'000);
    const auto result = proc::ProcessRunner::run(spec);
    if (!result.ok()) {
        throw std::runtime_error("fixture git command failed: " + result.err);
    }
}

void FixtureRepo::write_file(const std::string& relative, const std::string& content) {
    std::ofstream out(dir_ / relative, std::ios::binary);
    out << content;
    if (!out) {
        throw std::runtime_error("fixture write failed: " + relative);
    }
}

FixtureRepo::FixtureRepo() : dir_(make_temp_dir()) {
    // -c commit.gpgsign=false on every commit/merge: a developer's global
    // config must not break fixture determinism.
    git({"init", "-q", "-b", "main"});

    write_file("a.txt", "alpha\n");
    git({"add", "a.txt"});
    git({"-c", "commit.gpgsign=false", "commit", "-q", "-m", "Initial commit"},
        commit_env("2026-01-01T10:00:00+00:00"));
    git({"branch", "feature"});

    write_file("b.txt", "bravo\n");
    git({"add", "b.txt"});
    git({"-c", "commit.gpgsign=false", "commit", "-q", "-m", "Add b"},
        commit_env("2026-01-01T11:00:00+00:00"));

    git({"checkout", "-q", "feature"});
    write_file("f.txt", "feature\n");
    git({"add", "f.txt"});
    git({"-c", "commit.gpgsign=false", "commit", "-q", "-m", "Feature work"},
        commit_env("2026-01-01T12:00:00+00:00"));

    git({"checkout", "-q", "main"});
    git({"-c", "commit.gpgsign=false", "merge", "-q", "--no-ff", "-m", "Merge branch 'feature'",
         "feature"},
        commit_env("2026-01-01T13:00:00+00:00"));

    // Working-tree state for status tests.
    write_file("a.txt", "alpha\nmodified\n"); // unstaged modification
    write_file("staged.txt", "staged\n");
    git({"add", "staged.txt"});
    write_file("untracked.txt", "untracked\n");
    git({"mv", "b.txt", "renamed.txt"}); // staged rename
}

FixtureRepo::~FixtureRepo() {
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec); // best effort
}

} // namespace repomancer::test
