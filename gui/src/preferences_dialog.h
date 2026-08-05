// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Preferences (implementation-plan.md §11.1): VCS Providers (which git
// binary the app runs — "git" from PATH by default) and Interface (the
// integrated title bar and its window-button decoration).

#pragma once

#include <repomancer/settings.h>

#include <wx/dialog.h>

class wxCheckBox;
class wxChoice;
class wxStaticText;
class wxTextCtrl;

namespace repomancer::gui {

class PreferencesDialog : public wxDialog {
public:
    PreferencesDialog(wxWindow* parent, const repomancer::Settings& settings);

    // The edited settings; valid after ShowModal() returns wxID_OK.
    [[nodiscard]] repomancer::Settings Result() const;

private:
    void OnBrowse();
    void OnTest();

    repomancer::Settings settings_;
    wxTextCtrl* git_binary_ = nullptr;
    wxStaticText* test_result_ = nullptr;
    wxCheckBox* integrated_titlebar_ = nullptr;
    wxChoice* topbar_buttons_ = nullptr;
    wxCheckBox* sharp_corners_ = nullptr;
};

} // namespace repomancer::gui
