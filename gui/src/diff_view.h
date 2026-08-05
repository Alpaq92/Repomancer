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

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace repomancer::gui {

class DiffView : public wxStyledTextCtrl {
public:
    DiffView(wxWindow* parent, wxWindowID id = wxID_ANY);

    // `banner` (optional) is shown as a meta line above the patch — e.g.
    // "Staged changes" when the pane falls back to the index diff.
    void ShowDiff(std::vector<repomancer::vcs::FileDiff> files,
                  const wxString& banner = {});
    void ShowMessage(const wxString& message);
    void Clear();

    // Re-reads system colours; call when the theme changes.
    void ApplyTheme();

    // Right-click on a hunk's lines: the handler receives the displayed
    // file and the hunk index within it. Rows outside any hunk (headers,
    // messages) raise nothing.
    void SetOnHunkMenu(
        std::function<void(const repomancer::vcs::FileDiff&, std::size_t)> handler);

private:
    void SetReadOnlyText(const wxString& text);

    std::vector<repomancer::vcs::FileDiff> files_;
    // Per displayed text line: (file index, hunk index), or (-1, -1) for
    // chrome lines. Rebuilt by every ShowDiff.
    std::vector<std::pair<int, int>> line_hunks_;
    std::function<void(const repomancer::vcs::FileDiff&, std::size_t)> on_hunk_menu_;
};

} // namespace repomancer::gui
