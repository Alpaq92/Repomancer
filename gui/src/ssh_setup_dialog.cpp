// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "ssh_setup_dialog.h"

#include <repomancer/ssh/agent.h>
#include <repomancer/ssh/config.h>
#include <repomancer/ssh/connection.h>
#include <repomancer/ssh/known_hosts.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/clipbrd.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <utility>

namespace repomancer::gui {

namespace ssh = repomancer::ssh;

SshSetupDialog::SshSetupDialog(wxWindow* parent, ssh::KeyInfo key,
                               const std::string& passphrase)
    : wxDialog(parent, wxID_ANY, _("Set Up SSH Key"), wxDefaultPosition,
               wxSize(600, 560), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      key_(std::move(key)),
      passphrase_(passphrase) {
    auto* root = new wxBoxSizer(wxVERTICAL);

    root->Add(new wxStaticText(
                  this, wxID_ANY,
                  wxString::Format(_("Key created: %s"),
                                   wxString::FromUTF8(key_.fingerprint_sha256))),
              0, wxLEFT | wxRIGHT | wxTOP, 12);

    // The public key, ready to paste into the forge's "add SSH key" page.
    auto* pub = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(key_.public_key),
                               wxDefaultPosition, wxSize(-1, 56),
                               wxTE_MULTILINE | wxTE_READONLY | wxTE_BESTWRAP);
    root->Add(pub, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);

    auto* copy_row = new wxBoxSizer(wxHORIZONTAL);
    auto* copy = new wxButton(this, wxID_ANY, _("&Copy public key"));
    copy_row->Add(copy, 0, wxRIGHT, 8);
    copy_row->Add(new wxStaticText(this, wxID_ANY,
                                   _("Paste it into your forge's SSH keys page.")),
                  0, wxALIGN_CENTER_VERTICAL);
    root->Add(copy_row, 0, wxLEFT | wxRIGHT | wxTOP, 12);
    copy->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (wxTheClipboard->Open()) {
            wxTheClipboard->SetData(
                new wxTextDataObject(wxString::FromUTF8(key_.public_key)));
            wxTheClipboard->Close();
            Log(_("Public key copied to the clipboard."));
        }
    });

    auto* host_row = new wxBoxSizer(wxHORIZONTAL);
    host_row->Add(new wxStaticText(this, wxID_ANY, _("&Host:")), 0,
                  wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    host_ = new wxTextCtrl(this, wxID_ANY, "github.com");
    host_row->Add(host_, 1);
    root->Add(host_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);

    do_agent_ = new wxCheckBox(this, wxID_ANY, _("Add the key to &ssh-agent"));
    do_config_ = new wxCheckBox(this, wxID_ANY,
                                _("Use this key for the host in ~/.ssh/&config"));
    do_known_hosts_ =
        new wxCheckBox(this, wxID_ANY, _("&Verify and remember the host key"));
    do_test_ = new wxCheckBox(this, wxID_ANY, _("&Test the connection"));
    for (auto* box : {do_agent_, do_config_, do_known_hosts_, do_test_}) {
        box->SetValue(true);
        root->Add(box, 0, wxLEFT | wxRIGHT | wxTOP, 12);
    }

    log_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                          wxDefaultSize,
                          wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    root->Add(log_, 1, wxEXPAND | wxALL, 12);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    trust_ = new wxButton(this, wxID_ANY, _("Trust this host &key"));
    trust_->Hide(); // only shown when an unrecognized host key needs confirming
    buttons->Add(trust_, 0, wxRIGHT, 6);
    buttons->AddStretchSpacer();
    run_ = new wxButton(this, wxID_ANY, _("&Run"));
    buttons->Add(run_, 0, wxRIGHT, 6);
    buttons->Add(new wxButton(this, wxID_CLOSE, _("Close")), 0);
    root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    SetSizer(root);

    run_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnRun(); });
    trust_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { TrustPendingHostKey(); });
    Bind(wxEVT_BUTTON, [this](wxCommandEvent& event) {
        if (event.GetId() == wxID_CLOSE) {
            Close();
            return;
        }
        event.Skip();
    });
    // Never tear the dialog down while the worker is still touching it.
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        if (running_.load()) {
            wxBell();
            event.Veto();
            return;
        }
        event.Skip();
    });
}

SshSetupDialog::~SshSetupDialog() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

void SshSetupDialog::Log(const wxString& line) {
    log_->AppendText(line + "\n");
}

void SshSetupDialog::PostLog(const wxString& line) {
    CallAfter([this, line] { Log(line); });
}

void SshSetupDialog::OnRun() {
    if (running_.load()) {
        wxBell();
        return;
    }
    if (worker_.joinable()) {
        worker_.join(); // the previous run has already finished
    }
    running_.store(true);
    run_->Disable();
    trust_->Hide();
    pending_host_line_.clear();
    Layout();
    worker_ = std::thread([this] { RunSteps(); });
}

void SshSetupDialog::RunSteps() {
    // Snapshot every UI value up front: the worker must not touch widgets.
    const std::string host(host_->GetValue().utf8_string());
    const bool want_agent = do_agent_->GetValue();
    const bool want_config = do_config_->GetValue();
    const bool want_known = do_known_hosts_->GetValue();
    const bool want_test = do_test_->GetValue();

    if (want_agent) {
        PostLog(_("Adding the key to ssh-agent..."));
        auto r = ssh::agent_add(key_.private_path, passphrase_.reveal());
        PostLog(r.ok() ? _("  OK  key loaded into the agent")
                       : wxString::Format(_("  FAILED  %s"),
                                          wxString::FromUTF8(r.error().message)));
    }

    if (want_config && !host.empty()) {
        PostLog(wxString::Format(_("Routing %s to this key in ~/.ssh/config..."),
                                 wxString::FromUTF8(host)));
        const auto cfg_path = ssh::default_key_dir() / "config";
        auto r = ssh::config_set_identity(cfg_path, host, key_.private_path);
        PostLog(r.ok()
                    ? wxString::Format(_("  OK  %s now uses this key"),
                                       wxString::FromUTF8(host))
                    : wxString::Format(_("  FAILED  %s"),
                                       wxString::FromUTF8(r.error().message)));
    }

    if (want_known && !host.empty()) {
        PostLog(wxString::Format(_("Fetching the host key for %s..."),
                                 wxString::FromUTF8(host)));
        auto scan = ssh::scan_host(host);
        if (!scan.ok()) {
            PostLog(wxString::Format(_("  FAILED  %s"),
                                     wxString::FromUTF8(scan.error().message)));
        } else if (scan.value().empty()) {
            PostLog(_("  FAILED  no host key came back - is the host reachable?"));
        } else {
            const auto known = ssh::default_key_dir() / "known_hosts";
            bool handled = false;
            for (const auto& hk : scan.value()) {
                if (ssh::is_trusted_forge_key(host, hk.fingerprint_sha256)) {
                    // Matches the fingerprint the forge publishes — safe to add
                    // without asking.
                    auto add = ssh::known_hosts_add(known, {hk.line});
                    PostLog(add.ok()
                                ? wxString::Format(
                                      _("  OK  %s matches the published fingerprint "
                                        "- remembered"),
                                      wxString::FromUTF8(hk.fingerprint_sha256))
                                : wxString::Format(
                                      _("  FAILED  %s"),
                                      wxString::FromUTF8(add.error().message)));
                    handled = true;
                    break;
                }
            }
            if (!handled) {
                // Unrecognized: never trusted silently. Show it and let the
                // user confirm against the fingerprint their forge publishes.
                const auto& hk = scan.value().front();
                pending_host_line_ = hk.line;
                PostLog(wxString::Format(
                    _("  NOTE  host key is %s"),
                    wxString::FromUTF8(hk.fingerprint_sha256)));
                PostLog(_("    It does not match a fingerprint Repomancer knows. "
                          "Check it against the one your host publishes, then "
                          "choose \"Trust this host key\"."));
                CallAfter([this] {
                    trust_->Show();
                    Layout();
                });
            }
        }
    }

    if (want_test && !host.empty()) {
        PostLog(wxString::Format(_("Testing the connection to %s..."),
                                 wxString::FromUTF8(host)));
        ssh::ConnectionConfig cfg;
        cfg.identity_file = key_.private_path;
        auto r = ssh::test_connection("git@" + host, cfg);
        if (!r.ok()) {
            PostLog(wxString::Format(_("  FAILED  %s"),
                                     wxString::FromUTF8(r.error().message)));
        } else if (r.value().authenticated) {
            PostLog(r.value().username.empty()
                        ? _("  OK  authenticated")
                        : wxString::Format(_("  OK  authenticated as %s"),
                                           wxString::FromUTF8(r.value().username)));
        } else {
            PostLog(_("  FAILED  not authenticated - the forge may not have this key yet"));
            PostLog("    " + wxString::FromUTF8(r.value().message));
        }
    }

    CallAfter([this] { Finish(); });
}

void SshSetupDialog::Finish() {
    running_.store(false);
    run_->Enable();
    Log(_("Done."));
}

void SshSetupDialog::TrustPendingHostKey() {
    if (pending_host_line_.empty()) {
        return;
    }
    const auto known = ssh::default_key_dir() / "known_hosts";
    auto r = ssh::known_hosts_add(known, {pending_host_line_});
    Log(r.ok() ? _("Host key remembered.")
               : wxString::Format(_("Could not remember the host key: %s"),
                                  wxString::FromUTF8(r.error().message)));
    pending_host_line_.clear();
    trust_->Hide();
    Layout();
}

} // namespace repomancer::gui
