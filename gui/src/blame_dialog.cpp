// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "blame_dialog.h"

#include "text_sanitize.h"

#include <wx/button.h>
#include <wx/datetime.h>
#include <wx/sizer.h>

#include <utility>

namespace repomancer::gui {

BlameDialog::BlameDialog(wxWindow* parent, const wxString& path,
                         std::vector<repomancer::vcs::BlameLine> lines)
    : wxDialog(parent, wxID_ANY, wxString::Format(_("Blame — %s"), path),
               wxDefaultPosition, wxSize(760, 480),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      lines_(std::move(lines)) {
    auto* root = new wxBoxSizer(wxVERTICAL);

    list_ = new wxDataViewListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                   wxDV_SINGLE | wxDV_ROW_LINES);
    list_->AppendTextColumn(_("Commit"), wxDATAVIEW_CELL_INERT, 90);
    list_->AppendTextColumn(_("Author"), wxDATAVIEW_CELL_INERT, 110);
    list_->AppendTextColumn(_("Date"), wxDATAVIEW_CELL_INERT, 90);
    list_->AppendTextColumn(_("Line"), wxDATAVIEW_CELL_INERT, 50, wxALIGN_RIGHT);
    list_->AppendTextColumn(_("Content"), wxDATAVIEW_CELL_INERT, 360);
    list_->Freeze(); // one relayout for the whole fill, not one per row
    long line_no = 1;
    for (const auto& line : lines_) {
        const wxDateTime when(static_cast<time_t>(line.author_time));
        wxVector<wxVariant> row;
        row.push_back(wxVariant(wxString::FromUTF8(line.hash.substr(0, 10))));
        row.push_back(wxVariant(sanitized_utf8(line.author)));
        row.push_back(wxVariant(when.Format("%Y-%m-%d")));
        row.push_back(wxVariant(wxString::Format("%ld", line_no++)));
        row.push_back(wxVariant(sanitized_utf8(line.content)));
        list_->AppendItem(row);
    }
    list_->Thaw();
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

void BlameDialog::AcceptSelection() {
    const int row = list_->GetSelectedRow();
    if (row != wxNOT_FOUND && static_cast<std::size_t>(row) < lines_.size()) {
        selected_hash_ = lines_[static_cast<std::size_t>(row)].hash;
    }
    EndModal(wxID_OK);
}

} // namespace repomancer::gui
