// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/vcs/graph.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <initializer_list>
#include <string>
#include <vector>

using namespace repomancer::vcs;

namespace {

Commit make(std::string hash, std::initializer_list<std::string> parents) {
    Commit commit;
    commit.hash = std::move(hash);
    commit.parents = parents;
    return commit;
}

std::vector<int> lanes_of(const std::vector<GraphEdge>& edges) {
    std::vector<int> lanes;
    lanes.reserve(edges.size());
    for (const auto& edge : edges) {
        lanes.push_back(edge.lane);
    }
    return lanes;
}

bool has_lane(const std::vector<GraphEdge>& edges, int lane) {
    return std::any_of(edges.begin(), edges.end(),
                       [lane](const GraphEdge& edge) { return edge.lane == lane; });
}

} // namespace

TEST_CASE("graph: empty history") {
    const auto layout = compute_graph_layout({});
    CHECK(layout.rows.empty());
    CHECK(layout.max_lanes == 0);
}

TEST_CASE("graph: linear history stays in lane 0") {
    const std::vector<Commit> commits = {
        make("a", {"b"}),
        make("b", {"c"}),
        make("c", {}),
    };
    const auto layout = compute_graph_layout(commits);
    REQUIRE(layout.rows.size() == 3);
    CHECK(layout.max_lanes == 1);

    for (const auto& row : layout.rows) {
        CHECK(row.lane == 0);
        CHECK(row.pass.empty());
        CHECK_FALSE(row.is_merge);
    }
    CHECK(layout.rows[0].children_in.empty()); // tip
    CHECK(lanes_of(layout.rows[0].parents_out) == std::vector<int>{0});
    CHECK(lanes_of(layout.rows[1].children_in) == std::vector<int>{0});
    CHECK(layout.rows[2].parents_out.empty()); // root closes the lane
}

TEST_CASE("graph: merge opens a second lane and the diamond closes") {
    // m ── merge of a and b, both rooted at r
    const std::vector<Commit> commits = {
        make("m", {"a", "b"}),
        make("a", {"r"}),
        make("b", {"r"}),
        make("r", {}),
    };
    const auto layout = compute_graph_layout(commits);
    REQUIRE(layout.rows.size() == 4);
    CHECK(layout.max_lanes == 2);

    const auto& merge = layout.rows[0];
    CHECK(merge.is_merge);
    CHECK(merge.lane == 0);
    CHECK(merge.parents_out.size() == 2);
    CHECK(has_lane(merge.parents_out, 0));
    CHECK(has_lane(merge.parents_out, 1));

    const auto& first = layout.rows[1]; // a — lane 0, b passes on lane 1
    CHECK(first.lane == 0);
    CHECK(lanes_of(first.children_in) == std::vector<int>{0});
    REQUIRE(first.pass.size() == 1);
    CHECK(first.pass[0].from == 1);
    CHECK(first.pass[0].to == 1);

    const auto& second = layout.rows[2]; // b — lane 1, r-lane passes on 0
    CHECK(second.lane == 1);
    CHECK(lanes_of(second.children_in) == std::vector<int>{1});
    REQUIRE(second.pass.size() == 1);
    CHECK(second.pass[0].from == 0);

    const auto& root = layout.rows[3]; // both lanes converge
    CHECK(root.lane == 0);
    CHECK(root.children_in.size() == 2);
    CHECK(root.parents_out.empty());
    CHECK(root.pass.empty());
}

TEST_CASE("graph: independent roots each get a lane") {
    const std::vector<Commit> commits = {
        make("a", {}),
        make("b", {}),
    };
    const auto layout = compute_graph_layout(commits);
    REQUIRE(layout.rows.size() == 2);
    CHECK(layout.rows[0].lane == 0);
    // Lane 0 was freed by the first root, so it is reused.
    CHECK(layout.rows[1].lane == 0);
    CHECK(layout.max_lanes == 1);
}

TEST_CASE("graph: colours are stable along a lane and differ across branches") {
    const std::vector<Commit> commits = {
        make("m", {"a", "b"}),
        make("a", {"r"}),
        make("b", {"r"}),
        make("r", {}),
    };
    const auto layout = compute_graph_layout(commits);
    // First parent inherits the merge commit's colour…
    CHECK(layout.rows[1].color == layout.rows[0].color);
    // …the side branch gets its own.
    CHECK(layout.rows[2].color != layout.rows[0].color);
}

TEST_CASE("graph: truncated history keeps the lane open") {
    // Parent "gone" is outside the fetched window.
    const std::vector<Commit> commits = {make("a", {"gone"})};
    const auto layout = compute_graph_layout(commits);
    REQUIRE(layout.rows.size() == 1);
    CHECK(lanes_of(layout.rows[0].parents_out) == std::vector<int>{0});
}

TEST_CASE("graph: octopus merge fans out to every parent") {
    const std::vector<Commit> commits = {
        make("o", {"p1", "p2", "p3"}),
    };
    const auto layout = compute_graph_layout(commits);
    REQUIRE(layout.rows.size() == 1);
    CHECK(layout.rows[0].is_merge);
    CHECK(layout.rows[0].parents_out.size() == 3);
    CHECK(layout.max_lanes == 3);
}

TEST_CASE("graph: lane cap bounds the width on pathological input") {
    std::vector<Commit> commits;
    std::vector<std::string> parents;
    for (int i = 0; i < 40; ++i) {
        parents.push_back("p" + std::to_string(i));
    }
    Commit octopus;
    octopus.hash = "wide";
    octopus.parents = parents;
    commits.push_back(octopus);

    const auto layout = compute_graph_layout(commits, GraphOptions{.lane_cap = 8});
    CHECK(layout.max_lanes <= 8);
}

TEST_CASE("graph: no row references a lane beyond its own width") {
    const std::vector<Commit> commits = {
        make("m", {"a", "b"}), make("a", {"c"}), make("b", {"c"}), make("c", {"d"}),
        make("d", {}),
    };
    const auto layout = compute_graph_layout(commits);
    for (const auto& row : layout.rows) {
        CHECK(row.lane < row.width);
        for (const auto& edge : row.children_in) {
            CHECK(edge.lane < row.width);
        }
        for (const auto& edge : row.parents_out) {
            CHECK(edge.lane < row.width);
        }
        for (const auto& segment : row.pass) {
            CHECK(segment.from < row.width);
            CHECK(segment.to < row.width);
        }
    }
}
