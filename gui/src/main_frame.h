// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#pragma once

#include "diff_view.h"
#include "text_sanitize.h"
#include "title_bar.h"
#include "graph_renderer.h"
#include "log_view.h"
#include "pane_header.h"
#include "repo_view.h"
#include "log_model.h"

#include <wx/aui/aui.h>
#include <wx/dataview.h>

#ifdef REPOMANCER_HAVE_WXBF
#include <wxbf/borderless_frame.h>
// The integrated top bar rides on wxbf's borderless frame (Windows/Linux);
// macOS keeps the native frame and menu bar.
using MainFrameBase = wxBorderlessFrame;
#else
using MainFrameBase = wxFrame;
#endif
#include <wx/frame.h>
#include <wx/stc/stc.h>

#include <repomancer/process/process_runner.h>
#include <repomancer/vcs/git/git_driver.h>
#include <repomancer/vcs/refs.h>
#include <repomancer/vcs/stats.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <functional>
#include <set>
#include <thread>
#include <vector>

class MainFrame : public MainFrameBase {
public:
    MainFrame();
    ~MainFrame() override;

    // Tier-0 shell menus launch us with an action against a folder. None is
    // the plain "open and show the log" default.
    enum class StartupAction { None, Commit, Sync, Settings };
    [[nodiscard]] static StartupAction StartupActionFromString(const std::string& name);

    // Used by the CLI entry point and by shell-menu invocations.
    void OpenRepository(const wxString& path);
    void SetStartupAction(StartupAction action) { startup_action_ = action; }
    void OpenPreferences(); // settings action, also usable without a repo

private:
    void RunStartupAction(); // dispatched once the repository has loaded

    void OnOpenRepository(wxCommandEvent& event);
    void OnThemeSelected(wxCommandEvent& event);
    void OnGraphStyleSelected(wxCommandEvent& event);
    void OnQuit(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void ShowCommit(int row);
    void ReloadRepository();
    void AppendFileRow(const wxString& change, const std::string& path,
                       const std::string& old_path);
    void OnPreferences(wxCommandEvent& event);
    void RebuildRecentMenu();
    // Fetches and shows the history of the changed file at `index`.
    void ShowFileHistory(const std::string& path);
    void ShowFileBlame(const std::string& path);
    // Opens the working-tree copy of the changed file with the system's
    // default application.
    void OpenChangedFile(const std::string& path);
    // Opens the file's directory in the system file manager.
    void OpenContainingFolder(const std::string& path);
    void OnFileSelected(wxDataViewEvent& event);

    void LoadRepository(const wxString& path);
    void ShowFileDiff(long index);
    void SetDetailsText(const wxString& text);
    // Re-reads system colours into everything that caches them.
    void ApplyThemeToWidgets();
    // Relaunches the application so a new theme actually takes effect.
    void RestartToApplySettings();
    // One-shot git operation off the UI thread: runs `op` against the
    // repository with the configured binary, then `done` with the value on
    // the UI thread; failures land in the status bar with git's own words.
    // Serialized on op_worker_ — the join/queue policy lives here alone.
    // The default failure handler: the prefix plus git's own words in the
    // status bar. `on_error` overrides it where a non-zero exit is expected
    // and meaningful (a merge that stops on conflicts is not a hard error).
    template <typename Op, typename Done>
    void RunGitOp(wxString failure_prefix, Op op, Done done,
                  std::function<void(const repomancer::vcs::VcsError&)> on_error = {}) {
        // One op at a time — and never a UI-thread join on a LIVE worker: a
        // slow fetch would freeze the whole window for its duration. The
        // flag clears before `done` runs, so completions may chain ops.
        if (op_running_.load()) {
            wxBell();
            SetStatusText(_("Another git operation is still running…"));
            return;
        }
        op_running_.store(true);
        if (op_worker_.joinable()) {
            op_worker_.join(); // the previous thread has already finished
        }
        op_worker_ = std::thread([this, failure_prefix = std::move(failure_prefix),
                                  op = std::move(op), done = std::move(done),
                                  on_error = std::move(on_error), repo = repo_path_,
                                  config = git_config_]() mutable {
            auto result = op(repo, config);
            CallAfter([this, failure_prefix = std::move(failure_prefix),
                       result = std::move(result), done = std::move(done),
                       on_error = std::move(on_error)]() mutable {
                op_running_.store(false);
                if (!result.ok()) {
                    if (on_error) {
                        on_error(result.error());
                    } else {
                        SetStatusText(failure_prefix +
                                      repomancer::gui::error_text(result.error()));
                    }
                    return;
                }
                done(std::move(result).value());
            });
        });
    }

    // False (with an actionable status message) when the repository is
    // open read-only — every GUI mutation entry point checks this BEFORE
    // collecting input, on top of the driver's own refusal.
    bool RepoWritable();
    void ComputeReadOnlyOverrides(const std::filesystem::path& repo);

    void ShowWorktree();
    // Menu actions are keyed by PATH, not row index: popup menus and modal
    // dialogs run nested event loops in which a pending refresh can rebuild
    // the lists, so an index captured at menu-build time may name a
    // different file by action time.
    void RunRemoteOp(
        const wxString& title,
        std::function<repomancer::vcs::VcsResult<std::string>(
            repomancer::vcs::git::GitDriver&, const std::filesystem::path&,
            const repomancer::proc::ChunkSink&, const std::atomic<bool>*)>
            op);
    void FetchRemotes();
    void PullCurrentBranch();
    void PushCurrentBranch();
    void StashChanges();
    void PopStash();
    void OnLogMenu(int row);
    void MergeBranch(const std::string& branch);
    void AbortMerge();
    void CommitMerge();
    void ResolveConflict(const std::string& path, bool ours);
    void ResolveWithTool(const std::string& path);
    void UpdateMergeState(bool merging);
    void OnHunkMenu(const repomancer::vcs::FileDiff& file, std::size_t hunk);
    void DiscardFile(const std::string& path);
    void StageWorktreeFile(const std::string& path, bool stage);
    void CommitStaged();
    void OnBranchMenu(const wxString& branch);
    void SwitchBranch(const std::string& branch);
    void CreateBranch(const std::string& branch);
    void PopulateRepoDetails(
        const repomancer::vcs::VcsResult<repomancer::vcs::StatusSnapshot>& status,
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
    wxDataViewListCtrl* files_ = nullptr;
    repomancer::gui::DiffView* diff_ = nullptr;
    std::unique_ptr<CommitLogModel> model_;
    // Owned menus when there is no wxMenuBar (integrated title bar mode).
    std::unique_ptr<wxMenu> file_menu_owned_;
    std::unique_ptr<wxMenu> repo_menu_owned_;
    std::unique_ptr<wxMenu> view_menu_owned_;
    std::unique_ptr<wxMenu> help_menu_owned_;
    wxMenu* recent_menu_ = nullptr; // owned by the File menu
    wxMenuItem* merge_commit_item_ = nullptr; // enabled only while merging
    wxMenuItem* merge_abort_item_ = nullptr;
    std::vector<std::string> recent_repos_; // mirrors settings; UI reads this
    // Which git to run, resolved from Preferences → VCS Providers.
    repomancer::vcs::git::GitConfig git_config_;
    std::filesystem::path repo_path_;
    std::string selected_commit_;
    std::vector<repomancer::vcs::ChangedFile> changed_files_;
    // The files pane shows the working tree instead of a commit's files.
    bool worktree_mode_ = false;
    bool diff_is_staged_ = false; // the diff pane shows HEAD-vs-index
    bool repo_read_only_ = false; // §13.1: the open repo is untrusted
    bool merging_ = false;        // a merge is in progress (MERGE_HEAD exists)
    StartupAction startup_action_ = StartupAction::None;
    // Repos the user chose "Open Read-Only" for THIS session: the gate
    // auto-answers instead of re-prompting on every reload.
    std::set<std::string> session_read_only_;
    std::vector<repomancer::vcs::StatusEntry> worktree_entries_;
    std::string current_branch_;
    std::thread worker_;
    std::atomic<bool> busy_{false};
    // Selection details are fetched off the UI thread; the generation numbers
    // let a newer selection silently drop results that arrive for an older one.
    std::thread files_worker_;
    std::thread diff_worker_;
    std::thread op_worker_;
    std::atomic<bool> op_running_{false};
    std::atomic<unsigned> files_generation_{0};
    std::atomic<unsigned> diff_generation_{0};
};
