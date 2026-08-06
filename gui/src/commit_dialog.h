// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// The commit message editor (implementation-plan.md: STC editor, 72-column
// guide). Returns the message verbatim; the driver sends it via stdin.

#pragma once

#include <wx/dialog.h>

class wxStyledTextCtrl;
class wxCheckBox;

namespace repomancer::gui {

class CommitDialog : public wxDialog {
public:
    // `seed` (optional) pre-fills the editor — used for a merge commit,
    // where it also switches the heading from the staged-file count.
    // `amend_message` (optional), when non-empty, enables an "Amend last
    // commit" checkbox that swaps the editor to HEAD's message.
    explicit CommitDialog(wxWindow* parent, int staged_count,
                          const wxString& seed = {},
                          const wxString& amend_message = {});

    [[nodiscard]] wxString Message() const;
    [[nodiscard]] bool Amend() const;
    [[nodiscard]] bool SignOff() const;
    [[nodiscard]] bool GpgSign() const;

private:
    wxStyledTextCtrl* editor_ = nullptr;
    wxCheckBox* amend_ = nullptr;
    wxCheckBox* signoff_ = nullptr;
    wxCheckBox* gpg_ = nullptr;
};

} // namespace repomancer::gui
