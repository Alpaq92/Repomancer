// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Line attribution for one file (implementation-plan.md §4, M1). Accepting a
// row jumps the main log to the commit that introduced the line.

#pragma once

#include <repomancer/vcs/blame.h>

#include <wx/dataview.h>
#include <wx/dialog.h>

#include <string>
#include <vector>

namespace repomancer::gui {

class BlameDialog : public wxDialog {
public:
    BlameDialog(wxWindow* parent, const wxString& path,
                std::vector<repomancer::vcs::BlameLine> lines);

    // The commit accepted with "Show in Log" (or a double-click); empty when
    // the dialog was dismissed.
    [[nodiscard]] const std::string& SelectedHash() const { return selected_hash_; }

private:
    void AcceptSelection();

    std::vector<repomancer::vcs::BlameLine> lines_;
    wxDataViewListCtrl* list_ = nullptr;
    std::string selected_hash_;
};

} // namespace repomancer::gui
