// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// The first step of the M3 SSH-key wizard (implementation-plan.md §7): collect
// the parameters for a new key. The caller runs ssh::generate() with request()
// and reports the result; the dialog itself spawns nothing.

#pragma once

#include <repomancer/ssh/keys.h>

#include <wx/dialog.h>

#include <filesystem>

class wxCheckBox;
class wxChoice;
class wxTextCtrl;

namespace repomancer::gui {

class SshKeyDialog : public wxDialog {
public:
    // `default_dir` seeds the file location (typically ~/.ssh).
    SshKeyDialog(wxWindow* parent, std::filesystem::path default_dir);

    // The parameters the user chose, ready for ssh::generate().
    [[nodiscard]] repomancer::ssh::GenerateRequest request() const;

    // True when the user asked to stir the seed by hand (the key ceremony).
    // Only offered for Ed25519 — the embedded generator produces nothing else.
    [[nodiscard]] bool UseCeremony() const;

private:
    void SyncDefaultPath();  // keeps the filename in step with the key type
    void SyncCeremonyState(); // the ceremony is Ed25519-only

    wxChoice* type_ = nullptr;
    wxCheckBox* ceremony_ = nullptr;
    wxTextCtrl* comment_ = nullptr;
    wxTextCtrl* path_ = nullptr;
    wxTextCtrl* passphrase_ = nullptr;
    wxTextCtrl* confirm_ = nullptr;
    std::filesystem::path dir_;
    bool path_edited_ = false; // once the user edits the path, stop retitling it
};

} // namespace repomancer::gui
