// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// A modal progress view for the remote operations (fetch / pull / push): it
// streams git's --progress output live and offers a Cancel that stops the
// transfer. The operation runs on a worker thread; chunks marshal back to the
// UI thread. Replaces the silent status-bar freeze those ops used to be.

#pragma once

#include <repomancer/process/process_runner.h>
#include <repomancer/vcs/model.h>

#include <wx/dialog.h>

#include <atomic>
#include <functional>
#include <optional>
#include <thread>

class wxTextCtrl;
class wxButton;

namespace repomancer::gui {

class RemoteProgressDialog : public wxDialog {
public:
    // `op` runs on a worker thread; it must stream to the given sink and
    // honour the cancel flag (both are forwarded straight to the driver).
    using Operation = std::function<repomancer::vcs::VcsResult<std::string>(
        const repomancer::proc::ChunkSink&, const std::atomic<bool>*)>;

    RemoteProgressDialog(wxWindow* parent, const wxString& title, Operation op);
    ~RemoteProgressDialog() override;

    // Shows the dialog modally and returns the operation's result.
    [[nodiscard]] repomancer::vcs::VcsResult<std::string> Run();

private:
    void Append(const wxString& text);
    void Finish(repomancer::vcs::VcsResult<std::string> result);

    // git's --progress redraws a line with '\r'; render that in place —
    // permanent lines commit on '\n', the current line overwrites on '\r'.
    wxString current_line_;
    long line_start_pos_ = 0;
    bool carriage_ = false;

    wxTextCtrl* log_ = nullptr;
    wxButton* close_ = nullptr;
    std::atomic<bool> cancel_{false};
    bool finished_ = false;
    std::thread worker_;
    std::optional<repomancer::vcs::VcsResult<std::string>> result_;
};

} // namespace repomancer::gui
