// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// The sidebar: a table with one column and one row. The column header is the
// pane's title — drawn by the same widget that titles every other pane — and
// the single cell holds the repository's details as a block of lines.

#pragma once

#include <wx/dataview.h>

#include <string>

namespace repomancer::gui {

class RepoView : public wxDataViewListCtrl {
public:
    explicit RepoView(wxWindow* parent);

    // Replaces the cell's contents. Lines without a leading space are drawn
    // as section headings.
    void SetDetails(const wxString& details);

private:
    wxDataViewColumn* column_ = nullptr;
};

} // namespace repomancer::gui
