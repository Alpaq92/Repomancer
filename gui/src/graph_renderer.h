// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Draws the commit-graph column of the log view (implementation-plan.md
// §4.2). The renderer owns no data: it paints the GraphRow the model hands
// it, so a 100k-row log costs nothing beyond the layout itself.

#pragma once

#include <repomancer/vcs/graph.h>

#include <wx/colour.h>
#include <wx/dataview.h>

#include <string_view>
#include <vector>

namespace repomancer::gui {

// How a line moves between lanes.
enum class GraphStyle {
    Angular, // straight diagonals — the Git Extensions / git-graph look
    Rounded, // the diagonal eased into the lane at both ends
};

[[nodiscard]] GraphStyle graph_style_from_string(std::string_view value);
[[nodiscard]] const char* graph_style_to_string(GraphStyle style);

// Lane colours: Tableau-10 hues, legible on light and dark backgrounds.
[[nodiscard]] wxColour lane_colour(int index);

class GraphRenderer : public wxDataViewCustomRenderer {
public:
    // `rows` must outlive the renderer (owned by the model).
    explicit GraphRenderer(const std::vector<repomancer::vcs::GraphRow>* rows);

    bool SetValue(const wxVariant& value) override;
    bool GetValue(wxVariant& value) const override;
    wxSize GetSize() const override;
    bool Render(wxRect cell, wxDC* dc, int state) override;

    void SetMaxLanes(int lanes) { max_lanes_ = lanes < 1 ? 1 : lanes; }
    void SetStyle(GraphStyle style) { style_ = style; }

    // Row pitch of the owning control. The renderer reports exactly this
    // height so its cell spans the whole row — otherwise the control centres a
    // shorter cell and the lane lines break between rows.
    static constexpr int kRowHeight = 26;

private:
    [[nodiscard]] const repomancer::vcs::GraphRow* current_row() const;

    const std::vector<repomancer::vcs::GraphRow>* rows_;
    long row_index_ = -1;
    int max_lanes_ = 1;
    GraphStyle style_ = GraphStyle::Angular;
};

} // namespace repomancer::gui
