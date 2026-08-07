// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "clone_dialog.h"

#include <repomancer/vcs/remote_url.h>

#include <wx/button.h>
#include <wx/dirdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <algorithm>
#include <utility>

namespace repomancer::gui {

CloneDialog::CloneDialog(wxWindow* parent, std::filesystem::path default_parent)
    : wxDialog(parent, wxID_ANY, _("Clone Repository"), wxDefaultPosition,
               wxSize(560, -1), wxDEFAULT_DIALOG_STYLE),
      parent_(std::move(default_parent)) {
    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(new wxStaticText(this, wxID_ANY,
                               _("Clone a repository into a new folder.")),
              0, wxLEFT | wxRIGHT | wxTOP, 12);

    auto* form = new wxFlexGridSizer(3, wxSize(8, 8));
    form->AddGrowableCol(1, 1);

    form->Add(new wxStaticText(this, wxID_ANY, _("&URL:")), 0,
              wxALIGN_CENTER_VERTICAL);
    url_ = new wxTextCtrl(this, wxID_ANY);
    form->Add(url_, 1, wxEXPAND);
    form->AddSpacer(0);

    form->Add(new wxStaticText(this, wxID_ANY, _("&Into:")), 0,
              wxALIGN_CENTER_VERTICAL);
    dest_ = new wxTextCtrl(this, wxID_ANY,
                           wxString::FromUTF8(parent_.string()));
    form->Add(dest_, 1, wxEXPAND);
    auto* browse = new wxButton(this, wxID_ANY, _("&Browse…"),
                                wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    form->Add(browse, 0);

    root->Add(form, 0, wxEXPAND | wxALL, 12);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer();
    auto* ok = new wxButton(this, wxID_OK, _("&Clone"));
    buttons->Add(ok, 0, wxRIGHT, 6);
    buttons->Add(new wxButton(this, wxID_CANCEL, _("Cancel")), 0);
    root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    SetSizerAndFit(root);
    // Fit sizes to the labels; URLs and paths need considerably more room.
    SetSize(wxSize(std::max(560, GetSize().GetWidth()), GetSize().GetHeight()));
    CentreOnParent();
    ok->SetDefault();
    url_->SetFocus();

    url_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { SyncDestination(); });
    // ChangeValue in SyncDestination emits no event, so this fires only on a
    // real user edit — after which the destination is left alone.
    dest_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { dest_edited_ = true; });
    browse->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        wxDirDialog picker(this, _("Clone into"),
                           wxString::FromUTF8(parent_.string()));
        if (picker.ShowModal() == wxID_OK) {
            parent_ = std::filesystem::path(std::string(picker.GetPath().utf8_string()));
            dest_edited_ = false; // a fresh folder choice re-enables the suggestion
            SyncDestination();
        }
    });

    Bind(wxEVT_BUTTON, [this](wxCommandEvent& event) {
        if (event.GetId() == wxID_OK &&
            (url_->GetValue().Trim().IsEmpty() || dest_->GetValue().Trim().IsEmpty())) {
            wxBell();
            return;
        }
        event.Skip();
    });
}

void CloneDialog::SyncDestination() {
    if (dest_edited_) {
        return;
    }
    const std::string url(url_->GetValue().Trim(true).Trim(false).utf8_string());
    std::filesystem::path target = parent_;
    if (const auto parsed = repomancer::vcs::parse_remote_url(url);
        parsed && !parsed->repo.empty()) {
        target /= parsed->repo;
    } else if (!url.empty()) {
        // Not a network URL — cloning from a local path is legitimate, so take
        // the name from its last component. Without this the destination would
        // stay at the parent folder, which already exists and would be refused.
        std::filesystem::path local(url);
        if (local.filename().empty()) {
            local = local.parent_path(); // a trailing separator
        }
        std::string name = local.filename().string();
        if (const auto git = name.rfind(".git");
            git != std::string::npos && git + 4 == name.size()) {
            name.erase(git);
        }
        if (!name.empty() && name != "." && name != "..") {
            target /= name;
        }
    }
    dest_->ChangeValue(wxString::FromUTF8(target.string()));
}

std::string CloneDialog::Url() const {
    return std::string(url_->GetValue().Trim(true).Trim(false).utf8_string());
}

std::filesystem::path CloneDialog::Destination() const {
    return std::filesystem::path(
        std::string(dest_->GetValue().Trim(true).Trim(false).utf8_string()));
}

} // namespace repomancer::gui
