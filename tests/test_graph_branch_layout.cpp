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
    // Topic branches cycle, so the only guarantee is that it is not the
    // mainline's colour.
    CHECK(layout.rows[1].color != layout.rows[0].color);
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
    const std::vector<Commit> named = {head("b", "develop")};
    const auto plain = compute_graph_layout(named, options);
    CHECK(plain.rows[0].color != branch_class_color(BranchClass::Develop));
}

TEST_CASE("graph layout: unclassified heads still reuse freed lanes") {
    const std::vector<Commit> commits = {head("a", ""), head("b", "")};
    const auto layout = compute_graph_layout(commits);
    REQUIRE(layout.rows.size() == 2);
    CHECK(layout.rows[1].lane == 0);
    CHECK(layout.max_lanes == 1);
}

TEST_CASE("graph layout: main keeps the left-most lane even when reached later") {
    // develop's tip comes first in topological order; without a reservation it
    // would claim lane 0 and push the mainline right.
    const std::vector<Commit> commits = {
        head("d2", "HEAD -> develop", {"d1"}),
        head("d1", "", {"base"}),
        head("m1", "main", {"base"}),
        head("base", ""),
    };
    const auto layout = compute_graph_layout(commits);
    REQUIRE(layout.rows.size() == 4);
    CHECK(layout.rows[2].lane == 0); // main
    CHECK(layout.rows[0].lane == 1); // develop steps over the reserved column
    CHECK(layout.rows[2].color == branch_class_color(BranchClass::Main));
    CHECK(layout.rows[0].color == branch_class_color(BranchClass::Develop));
}

TEST_CASE("graph layout: develop takes lane 0 when there is no main") {
    const std::vector<Commit> commits = {
        head("d2", "HEAD -> develop", {"d1"}),
        head("d1", ""),
    };
    const auto layout = compute_graph_layout(commits);
    REQUIRE(layout.rows.size() == 2);
    CHECK(layout.rows[0].lane == 0);
    CHECK(layout.max_lanes == 1);
}

TEST_CASE("graph layout: topic branches do not reserve columns") {
    // Only persistent classes reserve; three feature tips must not open three
    // columns before any of them is reached.
    const std::vector<Commit> commits = {
        head("f1", "feature/a", {"base"}),
        head("f2", "feature/b", {"base"}),
        head("base", ""),
    };
    const auto layout = compute_graph_layout(commits);
    REQUIRE(layout.rows.size() == 3);
    CHECK(layout.rows[0].lane == 0);
    CHECK(layout.max_lanes == 2);
}
