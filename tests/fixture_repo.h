// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Deterministic on-disk git repository for integration tests, built by
// driving the real `git` binary through ProcessRunner (dogfooding the same
// code path the product uses). Fixed identities and dates make the history
// reproducible.

#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace repomancer::test {

class FixtureRepo {
public:
    // History: c1 "Initial commit" (a.txt) → c2 "Add b" (b.txt) on main,
    // c3 "Feature work" on branch `feature` (from c1), c4 no-ff merge of
    // feature into main. Working state: a.txt modified (unstaged),
    // staged.txt added (staged), untracked.txt untracked,
    // b.txt renamed to renamed.txt (staged rename).
    FixtureRepo(); // throws std::runtime_error if git fails
    ~FixtureRepo();

    FixtureRepo(const FixtureRepo&) = delete;
    FixtureRepo& operator=(const FixtureRepo&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    static bool git_available();

    static constexpr const char* kAuthorName = "Alice Fixture";
    static constexpr const char* kAuthorEmail = "alice@fixture.test";

private:
    void git(std::vector<std::string> args,
             std::map<std::string, std::string> env_extra = {});
    void write_file(const std::string& relative, const std::string& content);

    std::filesystem::path dir_;
};

} // namespace repomancer::test
