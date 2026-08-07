// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "ssh_key_dialog.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

namespace repomancer::gui {

namespace {
const char* base_name_for(int selection) {
    switch (selection) {
    case 1: return "id_rsa";
    case 2: return "id_ecdsa";
    default: return "id_ed25519";
    }
}
} // namespace

SshKeyDialog::SshKeyDialog(wxWindow* parent, std::filesystem::path default_dir)
    : wxDialog(parent, wxID_ANY, _("Generate SSH Key"), wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE),
      dir_(std::move(default_dir)) {
    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(new wxStaticText(this, wxID_ANY,
                               _("Create a new SSH key pair with ssh-keygen.")),
              0, wxLEFT | wxRIGHT | wxTOP, 12);

    auto* form = new wxFlexGridSizer(2, wxSize(10, 8));
    form->AddGrowableCol(1, 1);
    const auto row = [&](const wxString& label, wxWindow* field) {
        form->Add(new wxStaticText(this, wxID_ANY, label), 0,
                  wxALIGN_CENTER_VERTICAL);
        form->Add(field, 1, wxEXPAND);
    };

    type_ = new wxChoice(this, wxID_ANY);
    type_->Append(_("Ed25519 (recommended)"));
    type_->Append(_("RSA (3072-bit)"));
    type_->Append(_("ECDSA"));
    type_->SetSelection(0);
    row(_("&Type:"), type_);

    comment_ = new wxTextCtrl(this, wxID_ANY,
                              wxGetUserId() + "@" + wxGetHostName());
    row(_("&Comment:"), comment_);

    path_ = new wxTextCtrl(this, wxID_ANY);
    row(_("&File:"), path_);

    passphrase_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                 wxDefaultSize, wxTE_PASSWORD);
    row(_("&Passphrase:"), passphrase_);

    confirm_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                              wxDefaultSize, wxTE_PASSWORD);
    row(_("Con&firm:"), confirm_);

    root->Add(form, 0, wxEXPAND | wxALL, 12);
    root->Add(new wxStaticText(
                  this, wxID_ANY,
                  _("Leave the passphrase empty for an unencrypted key.")),
              0, wxLEFT | wxRIGHT, 12);

    // The optional key ceremony. Ed25519-only: it runs our own generator, which
    // derives a seed the user helped stir; RSA/ECDSA go through ssh-keygen,
    // which cannot be handed a seed.
    ceremony_ = new wxCheckBox(this, wxID_ANY,
                               _("Stir randomness with &mouse movement"));
    ceremony_->SetToolTip(
        _("Collect mouse movement to add to the key's random seed. Your "
          "system's secure random source is always mixed in as well."));
    root->Add(ceremony_, 0, wxLEFT | wxRIGHT | wxTOP, 12);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer();
    auto* generate = new wxButton(this, wxID_OK, _("&Generate"));
    buttons->Add(generate, 0, wxRIGHT, 6);
    buttons->Add(new wxButton(this, wxID_CANCEL, _("Cancel")), 0);
    root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    SetSizerAndFit(root);
    generate->SetDefault();
    SyncDefaultPath();
    SyncCeremonyState();

    // ChangeValue (in SyncDefaultPath) does not emit wxEVT_TEXT, so this fires
    // only on real user edits — after which we stop retitling the path.
    path_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { path_edited_ = true; });
    type_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        SyncDefaultPath();
        SyncCeremonyState();
    });

    // Confirm the passphrases match before letting the dialog close on OK.
    Bind(wxEVT_BUTTON, [this](wxCommandEvent& event) {
        if (event.GetId() == wxID_OK) {
            if (path_->GetValue().Trim().IsEmpty()) {
                wxBell();
                return;
            }
            if (passphrase_->GetValue() != confirm_->GetValue()) {
                wxMessageBox(_("The passphrases do not match."),
                             _("Generate SSH Key"), wxOK | wxICON_WARNING, this);
                confirm_->SetFocus();
                return;
            }
        }
        event.Skip();
    });
}

void SshKeyDialog::SyncCeremonyState() {
    const bool ed25519 = type_->GetSelection() == 0;
    ceremony_->Enable(ed25519);
    if (!ed25519) {
        ceremony_->SetValue(false); // never leave a checked-but-ignored box
    }
}

bool SshKeyDialog::UseCeremony() const {
    return ceremony_->IsEnabled() && ceremony_->GetValue();
}

void SshKeyDialog::SyncDefaultPath() {
    if (path_edited_) {
        return;
    }
    const std::filesystem::path full = dir_ / base_name_for(type_->GetSelection());
    path_->ChangeValue(wxString::FromUTF8(full.string()));
}

repomancer::ssh::GenerateRequest SshKeyDialog::request() const {
    repomancer::ssh::GenerateRequest req;
    switch (type_->GetSelection()) {
    case 1:
        req.type = repomancer::ssh::KeyType::Rsa;
        req.bits = 3072;
        break;
    case 2:
        req.type = repomancer::ssh::KeyType::Ecdsa;
        break;
    default:
        req.type = repomancer::ssh::KeyType::Ed25519;
        break;
    }
    req.comment = std::string(comment_->GetValue().utf8_string());
    req.path = std::filesystem::path(std::string(path_->GetValue().utf8_string()));
    req.passphrase = std::string(passphrase_->GetValue().utf8_string());
    return req;
}

} // namespace repomancer::gui
