// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "preferences_dialog.h"

#include <repomancer/vcs/git/git_driver.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <chrono>

namespace repomancer::gui {

PreferencesDialog::PreferencesDialog(wxWindow* parent, const repomancer::Settings& settings)
    : wxDialog(parent, wxID_ANY, _("Preferences"), wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE),
      settings_(settings) {
    auto* root = new wxBoxSizer(wxVERTICAL);

    const auto heading = [this, root](const wxString& title) {
        auto* text = new wxStaticText(this, wxID_ANY, title);
        text->SetFont(text->GetFont().Bold());
        root->Add(text, 0, wxLEFT | wxRIGHT | wxTOP, 12);
    };
    heading(_("VCS Providers"));

    auto* hint = new wxStaticText(
        this, wxID_ANY,
        _("Repomancer drives the VCS tools already installed on this machine.\n"
          "\"git\" uses the first git found on PATH."));
    root->Add(hint, 0, wxLEFT | wxRIGHT | wxTOP, 12);

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(this, wxID_ANY, _("Git binary:")), 0,
             wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    git_binary_ = new wxTextCtrl(this, wxID_ANY,
                                 wxString::FromUTF8(settings_.git_binary),
                                 wxDefaultPosition, wxSize(280, -1));
    row->Add(git_binary_, 1, wxALIGN_CENTER_VERTICAL);
    auto* browse = new wxButton(this, wxID_ANY, _("Browse…"));
    row->Add(browse, 0, wxLEFT, 6);
    auto* test = new wxButton(this, wxID_ANY, _("Test"));
    row->Add(test, 0, wxLEFT, 6);
    root->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);

    test_result_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    root->Add(test_result_, 0, wxLEFT | wxRIGHT | wxTOP, 12);

    auto* merge_row = new wxBoxSizer(wxHORIZONTAL);
    merge_row->Add(new wxStaticText(this, wxID_ANY, _("Merge tool:")), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    merge_tool_ = new wxTextCtrl(this, wxID_ANY,
                                 wxString::FromUTF8(settings_.merge_tool),
                                 wxDefaultPosition, wxSize(280, -1));
    merge_tool_->SetHint(_("empty = git's configured merge.tool"));
    merge_row->Add(merge_tool_, 1, wxALIGN_CENTER_VERTICAL);
    root->Add(merge_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    root->Add(new wxStaticText(
                  this, wxID_ANY,
                  _("A git mergetool name — e.g. meld, kdiff3, vimdiff, or one you "
                    "configured. Used to resolve conflicts from the working tree.")),
              0, wxLEFT | wxRIGHT | wxTOP, 12);

    heading(_("Interface"));
    integrated_titlebar_ = new wxCheckBox(
        this, wxID_ANY, _("Integrated title bar (takes effect after restart)"));
    integrated_titlebar_->SetValue(settings_.integrated_titlebar);
    root->Add(integrated_titlebar_, 0, wxLEFT | wxRIGHT | wxTOP, 12);

    // Window-button style for the integrated bar, as wx-notepad-plus-plus
    // offers it: the theme's own buttons, flat, or rounded pills.
    auto* buttons_row = new wxBoxSizer(wxHORIZONTAL);
    buttons_row->Add(new wxStaticText(this, wxID_ANY, _("Window buttons:")), 0,
                     wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    topbar_buttons_ = new wxChoice(this, wxID_ANY);
    topbar_buttons_->Append(_("System-native"));
    topbar_buttons_->Append(_("Flat"));
    topbar_buttons_->Append(_("Rounded"));
    topbar_buttons_->SetSelection(settings_.topbar_buttons);
    buttons_row->Add(topbar_buttons_, 0);
    root->Add(buttons_row, 0, wxLEFT | wxRIGHT | wxTOP, 12);

    sharp_corners_ = new wxCheckBox(
        this, wxID_ANY, _("Ignore platform decoration (sharp corners)"));
    sharp_corners_->SetValue(settings_.topbar_sharp_corners);
    root->Add(sharp_corners_, 0, wxLEFT | wxRIGHT | wxTOP, 12);

    // What this platform cannot honour is greyed in one place.
#if !defined(REPOMANCER_HAVE_WXBF)
    integrated_titlebar_->Disable(); // no wxbf on this platform
    topbar_buttons_->Disable();
    sharp_corners_->Disable();
#elif !defined(__WXGTK__)
    sharp_corners_->Disable(); // companion to the GTK header bar only
#endif

    root->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 12);
    SetSizerAndFit(root);

    browse->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnBrowse(); });
    test->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnTest(); });
}

repomancer::Settings PreferencesDialog::Result() const {
    repomancer::Settings result = settings_;
    const std::string value(git_binary_->GetValue().Trim(true).Trim(false).utf8_str());
    result.git_binary = value.empty() ? "git" : value;
    result.merge_tool =
        std::string(merge_tool_->GetValue().Trim(true).Trim(false).utf8_str());
    result.integrated_titlebar = integrated_titlebar_->GetValue();
    result.topbar_buttons = topbar_buttons_->GetSelection();
    result.topbar_sharp_corners = sharp_corners_->GetValue();
    return result;
}

void PreferencesDialog::OnBrowse() {
    wxFileDialog dialog(this, _("Select the git executable"), wxEmptyString,
                        wxEmptyString, wxFileSelectorDefaultWildcardStr,
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) {
        git_binary_->SetValue(dialog.GetPath());
    }
}

void PreferencesDialog::OnTest() {
    // The candidate is exec'd argv-style with --version — never a shell
    // (§13.1) — and the driver's parse limits apply to what comes back.
    repomancer::vcs::git::GitConfig config;
    config.binary =
        std::filesystem::path(std::string(git_binary_->GetValue().utf8_str()));
    // This runs modal on the UI thread; a hung candidate must fail in a
    // beat, not the driver's full operation timeout.
    config.timeout = std::chrono::seconds(2);
    const auto version = repomancer::vcs::git::GitDriver(config).version();
    if (version.ok()) {
        test_result_->SetLabel(wxString::Format(
            _("OK — git %d.%d.%d"), version.value().major, version.value().minor,
            version.value().patch));
    } else {
        test_result_->SetLabel(wxString::Format(
            _("Failed: %s"), wxString::FromUTF8(version.error().message)));
    }
    Layout();
}

} // namespace repomancer::gui
