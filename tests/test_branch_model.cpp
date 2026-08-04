// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/vcs/branch_model.h>

#include <catch2/catch_test_macros.hpp>

using namespace repomancer::vcs;

TEST_CASE("branch model: git-flow classification") {
    const auto model = BranchModel::git_flow();

    CHECK(model.classify("main") == BranchClass::Main);
    CHECK(model.classify("master") == BranchClass::Main);
    CHECK(model.classify("trunk") == BranchClass::Main);
    CHECK(model.classify("develop") == BranchClass::Develop);
    CHECK(model.classify("dev") == BranchClass::Develop);
    CHECK(model.classify("feature/login") == BranchClass::Feature);
    CHECK(model.classify("release/1.2") == BranchClass::Release);
    CHECK(model.classify("hotfix-urgent") == BranchClass::Hotfix);
    CHECK(model.classify("bugfix/typo") == BranchClass::Bugfix);
    CHECK(model.classify("wip") == BranchClass::Unknown);
    CHECK(model.classify("") == BranchClass::Unknown);
    // "mainline" must not be mistaken for "main".
    CHECK(model.classify("mainline") == BranchClass::Unknown);
}

TEST_CASE("branch model: remote prefixes are stripped, paths are not") {
    const auto model = BranchModel::git_flow();
    CHECK(model.classify("origin/main") == BranchClass::Main);
    CHECK(model.classify("upstream/develop") == BranchClass::Develop);
    // A slash inside the branch name itself must survive.
    CHECK(model.classify("origin/feature/login") == BranchClass::Feature);
    CHECK(model.classify("feature/login") == BranchClass::Feature);
}

TEST_CASE("branch model: decoration strings") {
    const auto model = BranchModel::git_flow();

    CHECK(model.classify_refs("HEAD -> main, origin/main") == BranchClass::Main);
    CHECK(model.classify_refs("tag: v1.0") == BranchClass::Unknown);
    CHECK(model.classify_refs("") == BranchClass::Unknown);
    CHECK(model.classify_refs("HEAD") == BranchClass::Unknown);
    CHECK(model.classify_refs("feature/x") == BranchClass::Feature);

    // The most persistent branch on a commit wins, whatever the order.
    CHECK(model.classify_refs("feature/x, main") == BranchClass::Main);
    CHECK(model.classify_refs("main, feature/x") == BranchClass::Main);
    CHECK(model.classify_refs("tag: v2, HEAD -> develop, feature/y") == BranchClass::Develop);
}

TEST_CASE("branch model: simple and none") {
    const auto simple = BranchModel::simple();
    CHECK(simple.classify("main") == BranchClass::Main);
    CHECK(simple.classify("anything-else") == BranchClass::Feature);

    const auto none = BranchModel::none();
    CHECK(none.empty());
    CHECK(none.classify("main") == BranchClass::Unknown);
}

TEST_CASE("branch model: ordering puts persistent branches left") {
    CHECK(branch_class_order(BranchClass::Main) < branch_class_order(BranchClass::Develop));
    CHECK(branch_class_order(BranchClass::Develop) < branch_class_order(BranchClass::Feature));
    CHECK(branch_class_order(BranchClass::Feature) <=
          branch_class_order(BranchClass::Unknown));
}

TEST_CASE("branch model: long-lived classes are fixed, topics cycle") {
    // One of each is normally in flight, so a fixed colour identifies them.
    CHECK(branch_class_color(BranchClass::Main) >= 0);
    CHECK(branch_class_color(BranchClass::Develop) >= 0);
    CHECK(branch_class_color(BranchClass::Release) >= 0);
    CHECK(branch_class_color(BranchClass::Hotfix) >= 0);
    CHECK(branch_class_color(BranchClass::Main) != branch_class_color(BranchClass::Develop));

    // Several features run at once, so they take successive palette entries
    // rather than all sharing one.
    CHECK(branch_class_color(BranchClass::Feature) == -1);
    CHECK(branch_class_color(BranchClass::Bugfix) == -1);
    CHECK(branch_class_color(BranchClass::Unknown) == -1);
}
