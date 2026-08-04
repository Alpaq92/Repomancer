// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/vcs/graph.h>

#include <algorithm>

namespace repomancer::vcs {

namespace {

// A lane is "free" when it waits for nothing.
constexpr int kNoLane = -1;

struct LaneState {
    std::string waiting_for; // empty ⇒ free
    int color = 0;
};

int find_lane_waiting_for(const std::vector<LaneState>& lanes, const std::string& hash) {
    for (std::size_t i = 0; i < lanes.size(); ++i) {
        if (lanes[i].waiting_for == hash) {
            return static_cast<int>(i);
        }
    }
    return kNoLane;
}

int allocate_lane(std::vector<LaneState>& lanes, int lane_cap) {
    for (std::size_t i = 0; i < lanes.size(); ++i) {
        if (lanes[i].waiting_for.empty()) {
            return static_cast<int>(i);
        }
    }
    if (static_cast<int>(lanes.size()) < lane_cap) {
        lanes.emplace_back();
        return static_cast<int>(lanes.size()) - 1;
    }
    // Pathological width: fold everything into the last lane rather than
    // growing without bound.
    return lane_cap - 1;
}

int active_width(const std::vector<LaneState>& lanes) {
    int width = 0;
    for (std::size_t i = 0; i < lanes.size(); ++i) {
        if (!lanes[i].waiting_for.empty()) {
            width = static_cast<int>(i) + 1;
        }
    }
    return width;
}

} // namespace

GraphLayout compute_graph_layout(const std::vector<Commit>& commits, int lane_cap) {
    GraphLayout layout;
    layout.rows.reserve(commits.size());
    if (lane_cap < 1) {
        lane_cap = 1;
    }

    std::vector<LaneState> lanes;
    int next_color = 0;

    for (const auto& commit : commits) {
        GraphRow row;
        row.is_merge = commit.parents.size() > 1;

        // 1. Lanes waiting for this commit converge into its dot.
        for (std::size_t i = 0; i < lanes.size(); ++i) {
            if (lanes[i].waiting_for == commit.hash) {
                row.children_in.push_back(GraphEdge{static_cast<int>(i), lanes[i].color});
            }
        }

        if (row.children_in.empty()) {
            // A branch head nobody pointed at yet.
            row.lane = allocate_lane(lanes, lane_cap);
            lanes[static_cast<std::size_t>(row.lane)].color = next_color++;
        } else {
            row.lane = row.children_in.front().lane;
        }
        row.color = lanes[static_cast<std::size_t>(row.lane)].color;

        // Snapshot of what was live before we rewire, for pass-through lines.
        std::vector<LaneState> before = lanes;

        // 2. The extra converging lanes are consumed by the merge.
        for (std::size_t i = 1; i < row.children_in.size(); ++i) {
            lanes[static_cast<std::size_t>(row.children_in[i].lane)].waiting_for.clear();
        }

        // 3. Wire parents. The first parent inherits this lane (and colour) so
        //    mainline history stays in one column; further parents reuse a lane
        //    already waiting for them, or get a fresh one.
        if (commit.parents.empty()) {
            lanes[static_cast<std::size_t>(row.lane)].waiting_for.clear();
        } else {
            lanes[static_cast<std::size_t>(row.lane)].waiting_for = commit.parents[0];
            row.parents_out.push_back(
                GraphEdge{row.lane, lanes[static_cast<std::size_t>(row.lane)].color});

            for (std::size_t p = 1; p < commit.parents.size(); ++p) {
                const auto& parent = commit.parents[p];
                int lane = find_lane_waiting_for(lanes, parent);
                if (lane == kNoLane) {
                    lane = allocate_lane(lanes, lane_cap);
                    lanes[static_cast<std::size_t>(lane)].waiting_for = parent;
                    lanes[static_cast<std::size_t>(lane)].color = next_color++;
                }
                const auto same_lane = [lane](const GraphEdge& edge) {
                    return edge.lane == lane;
                };
                if (std::find_if(row.parents_out.begin(), row.parents_out.end(), same_lane) ==
                    row.parents_out.end()) {
                    row.parents_out.push_back(
                        GraphEdge{lane, lanes[static_cast<std::size_t>(lane)].color});
                }
            }
        }

        // 4. Everything live before and after that this commit did not touch
        //    simply continues straight down.
        const std::size_t common = std::min(before.size(), lanes.size());
        for (std::size_t i = 0; i < common; ++i) {
            const int lane = static_cast<int>(i);
            if (lane == row.lane || before[i].waiting_for.empty() ||
                lanes[i].waiting_for.empty()) {
                continue;
            }
            const auto same_lane = [lane](const GraphEdge& edge) { return edge.lane == lane; };
            if (std::find_if(row.children_in.begin(), row.children_in.end(), same_lane) !=
                row.children_in.end()) {
                continue; // consumed above
            }
            row.pass.push_back(GraphSegment{lane, lane, before[i].color});
        }

        row.width = std::max({active_width(before), active_width(lanes), row.lane + 1});
        layout.max_lanes = std::max(layout.max_lanes, row.width);
        layout.rows.push_back(std::move(row));
    }

    return layout;
}

} // namespace repomancer::vcs
