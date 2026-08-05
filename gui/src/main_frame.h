// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#pragma once

#include "diff_view.h"
#include "graph_renderer.h"
#include "log_view.h"
#include "pane_header.h"
#include "repo_view.h"
#include "log_model.h"

#include <wx/aui/aui.h>
#include <wx/frame.h>
#include <wx/listctrl.h>
#include <wx/stc/stc.h>

#include <repomancer/vcs/refs.h>
#include <repomancer/vcs/stats.h>

#include <atomic>
#include <filesystem>
#include <memory>
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
    void ShowCommit(int row);
    void OnFileSelected(wxListEvent& event);

    void LoadRepository(const wxString& path);
    void ShowFileDiff(long index);
    void SetDetailsText(const wxString& text);
    // Re-reads system colours into everything that caches them.
    void ApplyThemeToWidgets();
    // Relaunches the application so a new theme actually takes effect.
    void RestartForTheme();
    void PopulateRepoDetails(
        const repomancer::vcs::VcsResult<std::vector<repomancer::vcs::Ref>>& refs,
        const repomancer::vcs::VcsResult<std::vector<repomancer::vcs::Contributor>>&
            people,
        const repomancer::vcs::VcsResult<std::vector<repomancer::vcs::LanguageStat>>&
            langs);
    // Selects the log row the target names — by commit id, or failing that by
    // the ref name in the row's decorations.
    void JumpToRef(const repomancer::gui::RepoView::Target& target);

    wxAuiManager aui_;
    wxPanel* log_panel_ = nullptr;
    repomancer::gui::LogView* log_ = nullptr;
    wxStyledTextCtrl* details_ = nullptr;
    wxPanel* repo_panel_ = nullptr;
    wxSizerItem* repo_margin_ = nullptr;
    wxSizerItem* details_margin_ = nullptr;
    wxPanel* files_panel_ = nullptr;
    wxPanel* diff_panel_ = nullptr;
    wxSizerItem* diff_margin_ = nullptr;
    wxSizerItem* log_margin_ = nullptr;
    wxPanel* details_panel_ = nullptr;
    repomancer::gui::RepoView* repo_view_ = nullptr;
    wxListCtrl* files_ = nullptr;
    repomancer::gui::DiffView* diff_ = nullptr;
    std::unique_ptr<CommitLogModel> model_;
    std::filesystem::path repo_path_;
    std::string selected_commit_;
    std::vector<repomancer::vcs::ChangedFile> changed_files_;
    std::thread worker_;
    std::atomic<bool> busy_{false};
    // Selection details are fetched off the UI thread; the generation numbers
    // let a newer selection silently drop results that arrive for an older one.
    std::thread files_worker_;
    std::thread diff_worker_;
    std::atomic<unsigned> files_generation_{0};
    std::atomic<unsigned> diff_generation_{0};
};
