// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// The commit message editor (implementation-plan.md: STC editor, 72-column
// guide). Returns the message verbatim; the driver sends it via stdin.

#pragma once

#include <wx/dialog.h>

class wxStyledTextCtrl;

namespace repomancer::gui {

class CommitDialog : public wxDialog {
public:
    explicit CommitDialog(wxWindow* parent, int staged_count);

    [[nodiscard]] wxString Message() const;

private:
    wxStyledTextCtrl* editor_ = nullptr;
};

} // namespace repomancer::gui
