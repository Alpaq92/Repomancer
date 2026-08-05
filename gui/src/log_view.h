// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// The commit log: a native multi-column header strip over an owner-drawn,
// scrolled canvas. The canvas is not a tree view — every attempt to keep
// GtkTreeView honest under this platform's smooth-scroll input failed, while
// the sidebar's identical symptom was cured by exactly this architecture:
// wx-side scrolling over geometry the widget owns outright.

#pragma once

#include "graph_renderer.h"
#include "log_model.h"

#include <wx/panel.h>

#include <functional>

namespace repomancer::gui {

class LogHeaderCtrl;
class LogCanvasCtrl;

class LogView : public wxPanel {
public:
    // `model` must outlive the view; it is the row data, not a live
    // dataview binding.
    LogView(wxWindow* parent, CommitLogModel* model);

    // The rows changed wholesale (a repository was loaded).
    void ModelChanged();

    void SetStyle(GraphStyle style);
    // Strips depend on theme colours; drop and repaint them.
    void InvalidateStrips();

    // Distributes column widths: graph by lane count, author/date/hash by
    // content, subject takes the rest.
    void FitColumns();

    // Selects `row`, scrolls it into view and notifies, exactly as a click
    // on it would.
    void Select(int row);
    [[nodiscard]] int SelectedRow() const;

    void SetOnSelect(std::function<void(int)> handler);

    [[nodiscard]] wxWindow* canvas() const;

private:
    void MeasureContentWidths();

    LogHeaderCtrl* header_ = nullptr;
    LogCanvasCtrl* canvas_ = nullptr;
    int graph_width_ = 60;
    int author_width_ = 160;
    int date_width_ = 140;
    int hash_width_ = 100;
};

} // namespace repomancer::gui
