// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#pragma once

#include "diff_view.h"
#include "graph_renderer.h"
#include "pane_header.h"
#include "repo_view.h"
#include "log_model.h"

#include <wx/aui/aui.h>
#include <wx/dataview.h>
#include <wx/frame.h>
#include <wx/listctrl.h>
#include <wx/stc/stc.h>

#include <atomic>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

class MainFrame : public wxFrame {
public:
    MainFrame();
    ~MainFrame() override;

    // Used by the CLI entry point and, later, by shell-menu invocations.
    void OpenRepository(const wxString& path);

private:
    void OnOpenRepository(wxCommandEvent& event);
    void OnThemeSelected(wxCommandEvent& event);
    void OnGraphStyleSelected(wxCommandEvent& event);
    void OnQuit(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void OnCommitSelected(wxDataViewEvent& event);
    void OnFileSelected(wxListEvent& event);

    void LoadRepository(const wxString& path);
    void ShowFileDiff(long index);
    // Forces every visible row to be re-rendered, preserving the selection.
    void RefreshLogRows();
    void SetDetailsText(const wxString& text);
    // Re-reads system colours into everything that caches them.
    void ApplyThemeToWidgets();
    // Relaunches the application so a new theme actually takes effect.
    void RestartForTheme();
    void PopulateRepoDetails();
    // Sizes every column to its content and lets Subject absorb the rest.
    void FitColumns();

    wxAuiManager aui_;
    wxDataViewCtrl* log_view_ = nullptr;
    wxStyledTextCtrl* details_ = nullptr;
    repomancer::gui::RepoView* repo_view_ = nullptr;
    wxListCtrl* files_ = nullptr;
    repomancer::gui::DiffView* diff_ = nullptr;
    wxObjectDataPtr<CommitLogModel> model_;
    repomancer::gui::GraphRenderer* graph_renderer_ = nullptr;
    std::vector<repomancer::gui::PaneHeader*> pane_headers_;
    wxDataViewColumn* graph_column_ = nullptr;
    wxDataViewColumn* subject_column_ = nullptr;
    wxDataViewColumn* author_column_ = nullptr;
    wxDataViewColumn* date_column_ = nullptr;
    wxDataViewColumn* hash_column_ = nullptr;
    std::filesystem::path repo_path_;
    std::string selected_commit_;
    std::vector<repomancer::vcs::ChangedFile> changed_files_;
    std::thread worker_;
    std::atomic<bool> busy_{false};
};
