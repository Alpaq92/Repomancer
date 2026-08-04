// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Unified diff pane built on wxStyledTextCtrl — Scintilla under a permissive
// licence, which is what made wxWidgets the stack (see stack-analysis.md).
// Side-by-side and hunk staging come with the write milestone; this is the
// read-only half.

#pragma once

#include <repomancer/vcs/diff.h>

#include <wx/stc/stc.h>

#include <vector>

namespace repomancer::gui {

class DiffView : public wxStyledTextCtrl {
public:
    DiffView(wxWindow* parent, wxWindowID id = wxID_ANY);

    void ShowDiff(const std::vector<repomancer::vcs::FileDiff>& files);
    void ShowMessage(const wxString& message);
    void Clear();

    // Re-reads system colours; call when the theme changes.
    void ApplyTheme();

private:
    void SetReadOnlyText(const wxString& text);
};

} // namespace repomancer::gui
