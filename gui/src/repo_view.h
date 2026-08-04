// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// The sidebar: a table with one column and one row. The column header is the
// pane's title — drawn by the same widget that titles every other pane — and
// the single cell holds the repository's details as a block of lines.

#pragma once

#include <wx/dataview.h>

#include <functional>
#include <vector>

namespace repomancer::gui {

class RepoView : public wxDataViewListCtrl {
public:
    // What a line in the details block points at: the object it names, and
    // the name itself for when that object is not in the log — an annotated
    // tag's id is the tag object, not the commit it marks.
    struct Target {
        wxString hash;
        wxString name;
        [[nodiscard]] bool empty() const { return hash.empty() && name.empty(); }
    };

    explicit RepoView(wxWindow* parent);

    // Replaces the cell's contents. Lines without a leading space are drawn
    // as section headings. `targets` pairs with the lines of `details`; a
    // click on a line whose entry is non-empty activates it.
    void SetDetails(const wxString& details, std::vector<Target> targets = {});

    void SetOnActivate(std::function<void(const Target&)> handler) {
        on_activate_ = std::move(handler);
    }

    // A press at `point` (client coordinates): highlights the item under it
    // and activates its target, if any.
    void OnItemPress(const wxPoint& point);

private:
    // The line under `point`, in the control's client coordinates, or -1.
    [[nodiscard]] int LineAt(const wxPoint& point);

    wxDataViewColumn* column_ = nullptr;
    // The line the user last clicked, highlighted like a list selection so a
    // click on an item visibly lands on that item.
    int selected_line_ = -1;
    std::vector<wxString> lines_;
    std::vector<Target> targets_;
    std::function<void(const Target&)> on_activate_;
};

} // namespace repomancer::gui
