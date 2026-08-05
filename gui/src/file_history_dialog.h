// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// The history of one file (implementation-plan.md §4, M1): the commits that
// touched a path, renames followed. Chosen from the Changed files pane;
// accepting a row jumps the main log to that commit.

#pragma once

#include <repomancer/vcs/model.h>

#include <wx/dataview.h>
#include <wx/dialog.h>

#include <string>
#include <vector>

namespace repomancer::gui {

class FileHistoryDialog : public wxDialog {
public:
    FileHistoryDialog(wxWindow* parent, const wxString& path,
                      std::vector<repomancer::vcs::Commit> commits);

    // The commit accepted with "Show in Log" (or a double-click); empty when
    // the dialog was dismissed.
    [[nodiscard]] const std::string& SelectedHash() const { return selected_hash_; }

private:
    void AcceptSelection();

    std::vector<repomancer::vcs::Commit> commits_;
    wxDataViewListCtrl* list_ = nullptr;
    std::string selected_hash_;
};

} // namespace repomancer::gui
