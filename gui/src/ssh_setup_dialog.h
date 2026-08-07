// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// The rest of the M3 key wizard (implementation-plan.md §7): once a key
// exists, walk it through agent → ssh config → known_hosts → `ssh -T`. Each
// step drives the wx-free core; the work runs on a worker thread and reports
// into a live log, so a slow network never freezes the dialog.
//
// The public key is shown with a Copy button: uploading it to the forge is a
// manual paste until the OAuth device flow is configured.

#pragma once

#include <repomancer/secret/secure_buffer.h>
#include <repomancer/ssh/keys.h>

#include <wx/dialog.h>

#include <atomic>
#include <string>
#include <thread>

class wxButton;
class wxCheckBox;
class wxTextCtrl;

namespace repomancer::gui {

class SshSetupDialog : public wxDialog {
public:
    // `passphrase` is what the key was created with — needed to load it into
    // the agent. Held in guarded memory and revealed only at the point of use.
    SshSetupDialog(wxWindow* parent, repomancer::ssh::KeyInfo key,
                   const std::string& passphrase);
    ~SshSetupDialog() override;

private:
    void OnRun();
    void RunSteps();                  // worker thread body
    void Log(const wxString& line);   // UI thread only
    void PostLog(const wxString& line); // any thread → UI thread
    void Finish();
    void TrustPendingHostKey();

    repomancer::ssh::KeyInfo key_;
    repomancer::secret::SecretString passphrase_;

    wxTextCtrl* host_ = nullptr;
    wxCheckBox* do_agent_ = nullptr;
    wxCheckBox* do_config_ = nullptr;
    wxCheckBox* do_known_hosts_ = nullptr;
    wxCheckBox* do_test_ = nullptr;
    wxTextCtrl* log_ = nullptr;
    wxButton* run_ = nullptr;
    wxButton* trust_ = nullptr; // appears when a host key needs confirming

    // A scanned host key that did NOT match a published forge fingerprint: it
    // is never trusted silently — the user confirms it after reading it.
    std::string pending_host_line_;

    std::thread worker_;
    std::atomic<bool> running_{false};
};

} // namespace repomancer::gui
