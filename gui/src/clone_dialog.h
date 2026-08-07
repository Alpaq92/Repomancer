// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Collects a URL and a destination for File ▸ Clone. The clone itself runs
// through RemoteProgressDialog, like every other remote operation, so this
// dialog spawns nothing.

#pragma once

#include <wx/dialog.h>

#include <filesystem>
#include <string>

class wxTextCtrl;

namespace repomancer::gui {

class CloneDialog : public wxDialog {
public:
    // `default_parent` is the folder new clones land in — the parent of the
    // last repository opened, or the user's home.
    CloneDialog(wxWindow* parent, std::filesystem::path default_parent);

    [[nodiscard]] std::string Url() const;
    // The full path to create: the chosen folder plus the repository name.
    [[nodiscard]] std::filesystem::path Destination() const;

private:
    // Keeps the destination in step with the repository named by the URL,
    // until the user edits it themselves.
    void SyncDestination();

    wxTextCtrl* url_ = nullptr;
    wxTextCtrl* dest_ = nullptr;
    std::filesystem::path parent_;
    bool dest_edited_ = false;
};

} // namespace repomancer::gui
