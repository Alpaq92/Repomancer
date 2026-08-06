// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "commit_dialog.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/stc/stc.h>

namespace repomancer::gui {

CommitDialog::CommitDialog(wxWindow* parent, int staged_count, const wxString& seed,
                           const wxString& amend_message)
    : wxDialog(parent, wxID_ANY, seed.empty() ? _("Commit") : _("Commit Merge"),
               wxDefaultPosition, wxSize(560, 400),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
    auto* root = new wxBoxSizer(wxVERTICAL);

    const wxString heading =
        seed.empty()
            ? wxString::Format(wxPLURAL("Committing %d staged file.",
                                        "Committing %d staged files.", staged_count),
                               staged_count)
            : _("Finish the merge — edit the message and commit.");
    root->Add(new wxStaticText(this, wxID_ANY, heading), 0,
              wxLEFT | wxRIGHT | wxTOP, 10);

    editor_ = new wxStyledTextCtrl(this);
    editor_->SetLexer(wxSTC_LEX_NULL);
    editor_->SetWrapMode(wxSTC_WRAP_NONE);
    for (int margin = 0; margin < 3; ++margin) {
        editor_->SetMarginWidth(margin, 0);
    }
    editor_->SetMarginLeft(6);
    editor_->StyleSetFont(wxSTC_STYLE_DEFAULT,
                          wxFont(wxFontInfo().Family(wxFONTFAMILY_TELETYPE)));
    editor_->StyleSetForeground(wxSTC_STYLE_DEFAULT,
                                wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    editor_->StyleSetBackground(wxSTC_STYLE_DEFAULT,
                                wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX));
    editor_->StyleClearAll();
    if (!seed.empty()) {
        editor_->SetText(seed);
        editor_->GotoPos(editor_->GetLength()); // cursor after the seed
    }
    // The convention line: subjects at 50, bodies wrapped at 72.
    editor_->SetEdgeMode(wxSTC_EDGE_LINE);
    editor_->SetEdgeColumn(72);
    root->Add(editor_, 1, wxEXPAND | wxALL, 10);

    // Commit options. Amend is offered only when there is a HEAD message to
    // amend; toggling it swaps the editor between the fresh and HEAD text.
    auto* options = new wxBoxSizer(wxHORIZONTAL);
    if (!amend_message.empty()) {
        amend_ = new wxCheckBox(this, wxID_ANY, _("&Amend last commit"));
        options->Add(amend_, 0, wxRIGHT, 12);
        amend_->Bind(wxEVT_CHECKBOX, [this, seed, amend_message](wxCommandEvent&) {
            editor_->SetText(amend_->GetValue() ? amend_message : seed);
            editor_->GotoPos(editor_->GetLength());
        });
    }
    signoff_ = new wxCheckBox(this, wxID_ANY, _("Sign &off"));
    options->Add(signoff_, 0, wxRIGHT, 12);
    gpg_ = new wxCheckBox(this, wxID_ANY, _("&GPG sign"));
    options->Add(gpg_, 0);
    root->Add(options, 0, wxLEFT | wxRIGHT, 10);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer();
    auto* commit = new wxButton(this, wxID_OK, _("Commit"));
    buttons->Add(commit, 0, wxRIGHT, 6);
    buttons->Add(new wxButton(this, wxID_CANCEL, _("Cancel")), 0);
    root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    SetSizer(root);
    commit->SetDefault();
    editor_->SetFocus();

    // An empty message must not slip through as a click on the default.
    Bind(wxEVT_BUTTON,
         [this](wxCommandEvent& event) {
             if (event.GetId() == wxID_OK &&
                 editor_->GetText().Trim(true).Trim(false).empty()) {
                 wxBell();
                 return;
             }
             event.Skip();
         });
}

wxString CommitDialog::Message() const { return editor_->GetText(); }

bool CommitDialog::Amend() const { return amend_ != nullptr && amend_->GetValue(); }
bool CommitDialog::SignOff() const { return signoff_->GetValue(); }
bool CommitDialog::GpgSign() const { return gpg_->GetValue(); }

} // namespace repomancer::gui
