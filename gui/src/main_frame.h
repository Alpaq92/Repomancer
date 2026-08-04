// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#pragma once

#include "graph_renderer.h"
#include "log_model.h"

#include <wx/aui/aui.h>
#include <wx/dataview.h>
#include <wx/frame.h>
#include <wx/textctrl.h>

#include <atomic>
#include <thread>

class MainFrame : public wxFrame {
public:
    MainFrame();
    ~MainFrame() override;

    // Used by the CLI entry point and, later, by shell-menu invocations.
    void OpenRepository(const wxString& path);

private:
    void OnOpenRepository(wxCommandEvent& event);
    void OnThemeSelected(wxCommandEvent& event);
    void OnQuit(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void OnCommitSelected(wxDataViewEvent& event);

    void LoadRepository(const wxString& path);
    // Sizes every column to its content and lets Subject absorb the rest.
    void FitColumns();

    wxAuiManager aui_;
    wxDataViewCtrl* log_view_ = nullptr;
    wxTextCtrl* details_ = nullptr;
    wxObjectDataPtr<CommitLogModel> model_;
    repomancer::gui::GraphRenderer* graph_renderer_ = nullptr;
    wxDataViewColumn* graph_column_ = nullptr;
    wxDataViewColumn* subject_column_ = nullptr;
    wxDataViewColumn* author_column_ = nullptr;
    wxDataViewColumn* date_column_ = nullptr;
    wxDataViewColumn* hash_column_ = nullptr;
    std::thread worker_;
    std::atomic<bool> busy_{false};
};
