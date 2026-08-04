// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Layout behaviour that depends on the branching model.

#include <repomancer/vcs/graph.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace repomancer::vcs;

namespace {

Commit head(std::string hash, std::string refs, std::vector<std::string> parents = {}) {
    Commit commit;
    commit.hash = std::move(hash);
    commit.refs = std::move(refs);
    commit.parents = std::move(parents);
    return commit;
}

} // namespace

TEST_CASE("graph layout: a classified head takes its class colour") {
    const std::vector<Commit> commits = {head("a", "HEAD -> main")};
    const auto layout = compute_graph_layout(commits);
    REQUIRE(layout.rows.size() == 1);
    CHECK(layout.rows[0].lane == 0);
    CHECK(layout.rows[0].color == branch_class_color(BranchClass::Main));
}

TEST_CASE("graph layout: the first head is left-most whatever its class") {
    // A lone feature branch must not be pushed to column 3, leaving empty
    // gutters beside it.
    const std::vector<Commit> commits = {head("a", "feature/login")};
    const auto layout = compute_graph_layout(commits);
    REQUIRE(layout.rows.size() == 1);
    CHECK(layout.rows[0].lane == 0);
    CHECK(layout.max_lanes == 1);
    CHECK(layout.rows[0].color == branch_class_color(BranchClass::Feature));
}

TEST_CASE("graph layout: a topic branch opens to the right of the mainline") {
    const std::vector<Commit> commits = {
        head("m", "HEAD -> main", {"m2"}),
        head("f", "feature/login", {"m2"}),
        head("m2", ""),
    };
    const auto layout = compute_graph_layout(commits);
    REQUIRE(layout.rows.size() == 3);
    CHECK(layout.rows[0].lane == 0); // main
    CHECK(layout.rows[1].lane == 1); // feature sits beside it, not on top
    CHECK(layout.rows[0].color == branch_class_color(BranchClass::Main));
    CHECK(layout.rows[1].color == branch_class_color(BranchClass::Feature));
}

TEST_CASE("graph layout: model none falls back to structural layout") {
    GraphOptions options;
    options.model = BranchModel::none();
    const std::vector<Commit> commits = {head("a", "HEAD -> main")};
    const auto layout = compute_graph_layout(commits, options);
    REQUIRE(layout.rows.size() == 1);
    CHECK(layout.rows[0].lane == 0);
    // Cycling palette starts at 0, which happens to be Main's colour too, so
    // assert the branch model was not consulted by checking a feature head.
    const std::vector<Commit> feature = {head("b", "feature/x")};
    const auto plain = compute_graph_layout(feature, options);
    CHECK(plain.rows[0].color != branch_class_color(BranchClass::Feature));
}

TEST_CASE("graph layout: unclassified heads still reuse freed lanes") {
    const std::vector<Commit> commits = {head("a", ""), head("b", "")};
    const auto layout = compute_graph_layout(commits);
    REQUIRE(layout.rows.size() == 2);
    CHECK(layout.rows[1].lane == 0);
    CHECK(layout.max_lanes == 1);
}
