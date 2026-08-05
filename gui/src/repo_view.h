// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// The sidebar: the same header strip every pane gets, over a scrolled canvas
// that draws the repository's details as a block of rows. The content is not
// a tree view: its rows have varying heights, and GTK's tree view never
// scrolls variable-height rows reliably — the scroll range and the painted
// rows drift apart, leaving gaps and scrolling past the content. A plain
// scrolled window owns its geometry exactly.

#pragma once

#include <wx/colour.h>
#include <wx/panel.h>

#include <functional>
#include <utility>
#include <vector>

namespace repomancer::gui {

class PaneHeader;
class DetailsCanvas;

class RepoView : public wxPanel {
public:
    // What a row points at: the object it names, and the name itself for when
    // that object is not in the log — an annotated tag's id is the tag
    // object, not the commit it marks.
    struct Target {
        wxString hash;
        wxString name;
        [[nodiscard]] bool empty() const { return hash.empty() && name.empty(); }
    };

    // One row of the details block. A row is plain text, a heading, a legend
    // entry with a colour dot, or — when `bar` is non-empty — a stacked
    // proportion bar in the GitHub style.
    struct Row {
        wxString text;
        Target target;
        bool heading = false;
        bool dot = false;
        wxColour colour;
        std::vector<std::pair<wxColour, double>> bar; // colour and percent
    };

    explicit RepoView(wxWindow* parent);

    // Replaces the content rows.
    void SetRows(std::vector<Row> rows);

    void SetOnActivate(std::function<void(const Target&)> handler);

    // The scrolled content, for theme colours.
    [[nodiscard]] wxWindow* canvas() const;

private:
    PaneHeader* header_ = nullptr;
    DetailsCanvas* canvas_ = nullptr;
};

} // namespace repomancer::gui
