// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "remote_progress_dialog.h"

#include "text_sanitize.h"

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <utility>

namespace repomancer::gui {

RemoteProgressDialog::RemoteProgressDialog(wxWindow* parent, const wxString& title,
                                           Operation op)
    : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxSize(560, 340),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(new wxStaticText(this, wxID_ANY, title), 0, wxLEFT | wxRIGHT | wxTOP, 10);

    log_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                          wxDefaultSize,
                          wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    log_->SetFont(wxFont(wxFontInfo().Family(wxFONTFAMILY_TELETYPE)));
    root->Add(log_, 1, wxEXPAND | wxALL, 10);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer();
    close_ = new wxButton(this, wxID_CANCEL, _("Cancel"));
    buttons->Add(close_, 0);
    root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    SetSizer(root);

    // Cancel requests a stop; the button stays until the worker actually
    // winds down, then Finish() closes the dialog.
    close_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (finished_) {
            EndModal(wxID_CANCEL);
            return;
        }
        cancel_.store(true);
        close_->Disable();
        Append(_("Cancelling…\n"));
    });
    // The window's close box behaves the same: request cancel, don't tear the
    // dialog out from under the worker.
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        if (finished_) {
            event.Skip();
            return;
        }
        cancel_.store(true);
        close_->Disable();
        event.Veto();
    });

    // The sink runs on the process drain thread; hop to the UI thread to
    // append. The final CallAfter delivers the result and closes.
    worker_ = std::thread([this, op = std::move(op)]() mutable {
        auto result = op(
            [this](std::string_view chunk, bool) {
                CallAfter([this, text = std::string(chunk)] {
                    Append(sanitized_utf8(text));
                });
            },
            &cancel_);
        CallAfter([this, result = std::move(result)]() mutable {
            Finish(std::move(result));
        });
    });
}

RemoteProgressDialog::~RemoteProgressDialog() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

repomancer::vcs::VcsResult<std::string> RemoteProgressDialog::Run() {
    ShowModal();
    if (worker_.joinable()) {
        worker_.join(); // no CallAfter can fire after this
    }
    if (result_) {
        return std::move(*result_);
    }
    return repomancer::vcs::VcsError{repomancer::vcs::VcsError::Kind::Cancelled,
                                     "operation cancelled"};
}

void RemoteProgressDialog::Append(const wxString& text) {
    // Emulate a terminal's line rewriting: characters extend the current
    // line; '\r' means the next characters overwrite it (a progress tick);
    // '\n' commits it as a permanent line. The control's last line always
    // mirrors current_line_.
    const auto render_live = [this] {
        log_->Replace(line_start_pos_, log_->GetLastPosition(), current_line_);
    };
    for (const wxUniChar ch : text) {
        if (ch == '\r') {
            carriage_ = true;
        } else if (ch == '\n') {
            render_live();
            log_->AppendText("\n");
            line_start_pos_ = log_->GetLastPosition();
            current_line_.clear();
            carriage_ = false;
        } else {
            if (carriage_) {
                current_line_.clear();
                carriage_ = false;
            }
            current_line_ += ch;
        }
    }
    render_live();
}

void RemoteProgressDialog::Finish(repomancer::vcs::VcsResult<std::string> result) {
    finished_ = true;
    result_ = std::move(result);
    if (result_->ok()) {
        EndModal(wxID_OK);
        return;
    }
    // On failure, keep the window up so the user can read git's message; the
    // button turns into a plain Close.
    Append("\n" + error_text(result_->error()));
    close_->SetLabel(_("Close"));
    close_->Enable();
}

} // namespace repomancer::gui
