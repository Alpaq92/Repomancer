// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Commit-graph lane layout (implementation-plan.md §4.2).
//
// Active-lanes model (the gitk/SourceGit family): each lane is a column that
// "waits" for a specific commit. Lanes keep their index until freed, so
// pass-through segments are always vertical and only edges touching a dot are
// diagonal. The layout is computed incrementally from a topologically ordered
// commit list, so appending a later batch never relayouts earlier rows.

#pragma once

#include <repomancer/vcs/branch_model.h>
#include <repomancer/vcs/model.h>

#include <cstddef>
#include <string>
#include <vector>

namespace repomancer::vcs {

// A line crossing one row without touching its dot.
struct GraphSegment {
    int from = 0; // lane index at the top edge of the row
    int to = 0;   // lane index at the bottom edge
    int color = 0;
};

// An edge between this row's dot and a lane on the row above or below. The
// colour is the *lane's*, not the commit's, so a line keeps one colour along
// its whole length even where it crosses a row boundary.
struct GraphEdge {
    int lane = 0;
    int color = 0;
};

struct GraphRow {
    int lane = 0;   // column of this commit's dot
    int color = 0;  // palette index of the dot
    int width = 1;  // lanes occupied by this row (dot + everything passing)
    bool is_merge = false;

    // Edges from the row above that converge into this dot.
    std::vector<GraphEdge> children_in;
    // Edges leaving this dot towards the row below.
    std::vector<GraphEdge> parents_out;
    // Lines passing this row untouched.
    std::vector<GraphSegment> pass;
};

struct GraphLayout {
    std::vector<GraphRow> rows;
    int max_lanes = 0;
};

struct GraphOptions {
    int lane_cap = 64;
    // When a branch head can be classified from its refs, the lane it opens is
    // placed according to the class's order (mainline left, topics right) and
    // takes the class's colour. Default-constructed BranchModel::none() keeps
    // the purely structural layout.
    BranchModel model = BranchModel::git_flow();
};

// Commits must be in the order produced by `git log --topo-order` (children
// before parents). Unknown parents (beyond the fetched window) simply keep
// their lane open, which is what a truncated log should look like.
[[nodiscard]] GraphLayout compute_graph_layout(const std::vector<Commit>& commits,
                                               const GraphOptions& options = {});

} // namespace repomancer::vcs
