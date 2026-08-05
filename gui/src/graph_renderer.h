// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Paints the commit-graph strips of the log view (implementation-plan.md
// §4.2), plus the lane palette and graph style shared across the GUI.

#pragma once

#include <repomancer/vcs/graph.h>

#include <wx/bitmap.h>
#include <wx/colour.h>

#include <string_view>
#include <vector>

namespace repomancer::gui {

// How a line moves between lanes.
enum class GraphStyle {
    Angular, // orthogonal legs and square corners — the Git Extensions look
    Rounded, // the diagonal eased into the lane at both ends
};

[[nodiscard]] GraphStyle graph_style_from_string(std::string_view value);
[[nodiscard]] const char* graph_style_to_string(GraphStyle style);

// Lane colours: Open Color shade 6, legible on light and dark backgrounds.
[[nodiscard]] wxColour lane_colour(int index);

// Row pitch shared by the strip painter and every scroll computation.
inline constexpr int kRowHeight = 26;

// Width the graph column needs for `lanes` lanes.
[[nodiscard]] int graph_strip_width(int lanes);

// Renders one row's graph strip: `width` px wide, centred on a row of
// `row_height` px with half a row of overhang above and below so straight
// runs meet across row boundaries. Returns a null bitmap when no graphics
// context is available.
[[nodiscard]] wxBitmap render_graph_strip(const repomancer::vcs::GraphRow& row,
                                          bool first_row, bool last_row, int width,
                                          int row_height, bool selected,
                                          GraphStyle style);


} // namespace repomancer::gui
