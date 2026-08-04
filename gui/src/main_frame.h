// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#pragma once

#include "log_model.h"

#include <wx/aui/aui.h>
#include <wx/dataview.h>
#include <wx/frame.h>

#include <atomic>
#include <thread>

class MainFrame : public wxFrame {
public:
    MainFrame();
    ~MainFrame() override;

private:
    void OnOpenRepository(wxCommandEvent& event);
    void OnQuit(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);

    void LoadRepository(const wxString& path);

    wxAuiManager aui_;
    wxDataViewCtrl* log_view_ = nullptr;
    wxObjectDataPtr<CommitLogModel> model_;
    std::thread worker_;
    std::atomic<bool> busy_{false};
};
