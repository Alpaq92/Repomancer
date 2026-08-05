// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "commit_dialog.h"

#include <wx/button.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/stc/stc.h>

namespace repomancer::gui {

CommitDialog::CommitDialog(wxWindow* parent, int staged_count)
    : wxDialog(parent, wxID_ANY, _("Commit"), wxDefaultPosition, wxSize(560, 360),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
    auto* root = new wxBoxSizer(wxVERTICAL);

    root->Add(new wxStaticText(
                  this, wxID_ANY,
                  wxString::Format(wxPLURAL("Committing %d staged file.",
                                            "Committing %d staged files.", staged_count),
                                   staged_count)),
              0, wxLEFT | wxRIGHT | wxTOP, 10);

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
    // The convention line: subjects at 50, bodies wrapped at 72.
    editor_->SetEdgeMode(wxSTC_EDGE_LINE);
    editor_->SetEdgeColumn(72);
    root->Add(editor_, 1, wxEXPAND | wxALL, 10);

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

} // namespace repomancer::gui
