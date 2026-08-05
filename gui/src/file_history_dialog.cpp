// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "file_history_dialog.h"

#include "text_sanitize.h"

#include <wx/button.h>
#include <wx/datetime.h>
#include <wx/sizer.h>

#include <utility>

namespace repomancer::gui {



FileHistoryDialog::FileHistoryDialog(wxWindow* parent, const wxString& path,
                                     std::vector<repomancer::vcs::Commit> commits)
    : wxDialog(parent, wxID_ANY, wxString::Format(_("History — %s"), path),
               wxDefaultPosition, wxSize(640, 420),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      commits_(std::move(commits)) {
    auto* root = new wxBoxSizer(wxVERTICAL);

    list_ = new wxDataViewListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                   wxDV_SINGLE | wxDV_ROW_LINES);
    list_->AppendTextColumn(_("Subject"), wxDATAVIEW_CELL_INERT, 300);
    list_->AppendTextColumn(_("Author"), wxDATAVIEW_CELL_INERT, 120);
    list_->AppendTextColumn(_("Date"), wxDATAVIEW_CELL_INERT, 120);
    list_->AppendTextColumn(_("Hash"), wxDATAVIEW_CELL_INERT, 90);
    list_->Freeze(); // one relayout for the whole fill, not one per row
    for (const auto& commit : commits_) {
        const wxDateTime when(static_cast<time_t>(commit.commit_time));
        wxVector<wxVariant> row;
        row.push_back(wxVariant(sanitized_utf8(commit.subject)));
        row.push_back(wxVariant(sanitized_utf8(commit.author_name)));
        row.push_back(wxVariant(when.Format("%Y-%m-%d %H:%M")));
        row.push_back(wxVariant(wxString::FromUTF8(commit.hash.substr(0, 10))));
        list_->AppendItem(row);
    }
    list_->Thaw();
    if (!commits_.empty()) {
        list_->SelectRow(0);
    }
    root->Add(list_, 1, wxEXPAND | wxALL, 8);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer();
    auto* show = new wxButton(this, wxID_OK, _("Show in Log"));
    buttons->Add(show, 0, wxRIGHT, 6);
    buttons->Add(new wxButton(this, wxID_CANCEL, _("Close")), 0);
    root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    SetSizer(root);
    show->SetDefault();

    list_->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED,
                [this](wxDataViewEvent&) { AcceptSelection(); });
    Bind(wxEVT_BUTTON, [this](wxCommandEvent& event) {
        if (event.GetId() == wxID_OK) {
            AcceptSelection();
        } else {
            event.Skip();
        }
    });
}

void FileHistoryDialog::AcceptSelection() {
    const int row = list_->GetSelectedRow();
    if (row != wxNOT_FOUND && static_cast<std::size_t>(row) < commits_.size()) {
        selected_hash_ = commits_[static_cast<std::size_t>(row)].hash;
    }
    EndModal(wxID_OK);
}

} // namespace repomancer::gui
