// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "main_frame.h"

#include "theme.h"

#include <repomancer/settings.h>
#include <repomancer/vcs/git/git_driver.h>

#include <wx/aboutdlg.h>
#include <wx/dirdlg.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

using repomancer::gui::ThemeMode;
using repomancer::vcs::LogOptions;
using repomancer::vcs::git::GitDriver;

namespace {
enum {
    ID_ThemeSystem = wxID_HIGHEST + 1,
    ID_ThemeLight,
    ID_ThemeDark,
};
} // namespace

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, _("Repomancer"), wxDefaultPosition, wxSize(1000, 650)) {
    auto* file_menu = new wxMenu;
    file_menu->Append(wxID_OPEN, _("&Open Repository…\tCtrl-O"),
                      _("Open a local git repository"));
    file_menu->AppendSeparator();
    file_menu->Append(wxID_EXIT);

    auto* theme_menu = new wxMenu;
    theme_menu->AppendRadioItem(ID_ThemeSystem, _("&System"));
    theme_menu->AppendRadioItem(ID_ThemeLight, _("&Light"));
    theme_menu->AppendRadioItem(ID_ThemeDark, _("&Dark"));
    auto* view_menu = new wxMenu;
    view_menu->AppendSubMenu(theme_menu, _("&Theme"));

    auto* help_menu = new wxMenu;
    help_menu->Append(wxID_ABOUT);

    auto* menu_bar = new wxMenuBar;
    menu_bar->Append(file_menu, _("&File"));
    menu_bar->Append(view_menu, _("&View"));
    menu_bar->Append(help_menu, _("&Help"));
    SetMenuBar(menu_bar);

    switch (repomancer::gui::theme_mode_from_string(repomancer::load_settings().theme)) {
    case ThemeMode::Light:
        theme_menu->Check(ID_ThemeLight, true);
        break;
    case ThemeMode::Dark:
        theme_menu->Check(ID_ThemeDark, true);
        break;
    case ThemeMode::System:
        theme_menu->Check(ID_ThemeSystem, true);
        break;
    }

    CreateStatusBar();
    SetStatusText(_("Open a repository to begin"));

    log_view_ = new wxDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                   wxDV_ROW_LINES);
    model_ = wxObjectDataPtr<CommitLogModel>(new CommitLogModel);
    log_view_->AssociateModel(model_.get());

    // Fixed pitch keeps the graph lanes continuous from row to row.
    log_view_->SetRowHeight(repomancer::gui::GraphRenderer::kRowHeight);

    graph_renderer_ = new repomancer::gui::GraphRenderer(&model_->graph_rows());
    graph_column_ = new wxDataViewColumn(_("Graph"), graph_renderer_, CommitLogModel::Col_Graph,
                                         60, wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    log_view_->AppendColumn(graph_column_);

    subject_column_ =
        log_view_->AppendTextColumn(_("Subject"), CommitLogModel::Col_Subject,
                                    wxDATAVIEW_CELL_INERT, 420, wxALIGN_LEFT,
                                    wxDATAVIEW_COL_RESIZABLE);
    author_column_ =
        log_view_->AppendTextColumn(_("Author"), CommitLogModel::Col_Author,
                                    wxDATAVIEW_CELL_INERT, 160, wxALIGN_LEFT,
                                    wxDATAVIEW_COL_RESIZABLE);
    date_column_ = log_view_->AppendTextColumn(_("Date"), CommitLogModel::Col_Date,
                                               wxDATAVIEW_CELL_INERT, 140, wxALIGN_LEFT,
                                               wxDATAVIEW_COL_RESIZABLE);
    hash_column_ = log_view_->AppendTextColumn(_("Hash"), CommitLogModel::Col_Hash,
                                               wxDATAVIEW_CELL_INERT, 100, wxALIGN_LEFT,
                                               wxDATAVIEW_COL_RESIZABLE);

    details_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                              wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    details_->SetFont(wxFont(wxFontInfo().Family(wxFONTFAMILY_TELETYPE)));

    aui_.SetManagedWindow(this);
    aui_.AddPane(log_view_, wxAuiPaneInfo().CenterPane().Name("log"));
    aui_.AddPane(details_, wxAuiPaneInfo()
                               .Bottom()
                               .Name("details")
                               .Caption(_("Commit details"))
                               .BestSize(-1, 220)
                               .CloseButton(false));
    aui_.Update();

    Bind(wxEVT_MENU, &MainFrame::OnOpenRepository, this, wxID_OPEN);
    Bind(wxEVT_MENU, &MainFrame::OnThemeSelected, this, ID_ThemeSystem, ID_ThemeDark);
    Bind(wxEVT_MENU, &MainFrame::OnQuit, this, wxID_EXIT);
    Bind(wxEVT_MENU, &MainFrame::OnAbout, this, wxID_ABOUT);
    log_view_->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &MainFrame::OnCommitSelected, this);
    log_view_->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        event.Skip();
        FitColumns();
    });
}

void MainFrame::FitColumns() {
    if (log_view_ == nullptr || subject_column_ == nullptr) {
        return;
    }
    // The graph needs room for its lanes, but never less than its own header.
    const int graph_width =
        std::max(graph_renderer_->GetSize().GetWidth(),
                 GetTextExtent(graph_column_->GetTitle()).GetWidth() + 16);
    graph_column_->SetWidth(graph_width);

    for (auto* column : {author_column_, date_column_, hash_column_}) {
        column->SetWidth(wxCOL_WIDTH_AUTOSIZE);
    }

    const int used = graph_width + author_column_->GetWidth() + date_column_->GetWidth() +
                     hash_column_->GetWidth();
    const int available = log_view_->GetClientSize().GetWidth() - used - 8;
    subject_column_->SetWidth(std::max(200, available));
}

void MainFrame::OnCommitSelected(wxDataViewEvent&) {
    const wxDataViewItem item = log_view_->GetSelection();
    const auto* commit = item.IsOk() ? model_->commit_at(model_->GetRow(item)) : nullptr;
    if (commit == nullptr) {
        details_->ChangeValue(wxEmptyString);
        return;
    }

    const auto utf8 = [](const std::string& text) { return wxString::FromUTF8(text); };
    wxString text;
    text << _("Commit:  ") << utf8(commit->hash) << "\n";
    for (const auto& parent : commit->parents) {
        text << _("Parent:  ") << utf8(parent) << "\n";
    }
    text << _("Author:  ") << utf8(commit->author_name) << " <" << utf8(commit->author_email)
         << ">  " << wxDateTime(static_cast<time_t>(commit->author_time)).Format() << "\n";
    text << _("Commit:  ") << utf8(commit->committer_name) << " <"
         << utf8(commit->committer_email) << ">  "
         << wxDateTime(static_cast<time_t>(commit->commit_time)).Format() << "\n";
    if (!commit->refs.empty()) {
        text << _("Refs:    ") << utf8(commit->refs) << "\n";
    }
    text << "\n" << utf8(commit->subject) << "\n";
    if (!commit->body.empty()) {
        text << "\n" << utf8(commit->body);
    }
    details_->ChangeValue(text);
}

void MainFrame::OnThemeSelected(wxCommandEvent& event) {
    ThemeMode mode = ThemeMode::System;
    if (event.GetId() == ID_ThemeLight) {
        mode = ThemeMode::Light;
    } else if (event.GetId() == ID_ThemeDark) {
        mode = ThemeMode::Dark;
    }

    const bool applied_live = repomancer::gui::apply_theme(mode);

    auto settings = repomancer::load_settings();
    settings.theme = repomancer::gui::theme_mode_to_string(mode);
    repomancer::save_settings(settings);

    if (!applied_live) {
        wxMessageBox(_("The theme will be applied after restarting Repomancer."),
                     _("Repomancer"), wxOK | wxICON_INFORMATION, this);
    }
}

MainFrame::~MainFrame() {
    if (worker_.joinable()) {
        worker_.join();
    }
    aui_.UnInit();
}

void MainFrame::OnQuit(wxCommandEvent&) { Close(true); }

void MainFrame::OnAbout(wxCommandEvent&) {
    wxAboutDialogInfo info;
    info.SetName(_("Repomancer"));
    info.SetDescription(_("A visual client for version control"));
    info.SetCopyright("Copyright 2026 Repomancer contributors — Apache-2.0");
    wxAboutBox(info, this);
}

void MainFrame::OnOpenRepository(wxCommandEvent&) {
    if (busy_.load()) {
        wxBell();
        return;
    }
    wxDirDialog dialog(this, _("Open repository"), wxEmptyString,
                       wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    LoadRepository(dialog.GetPath());
}

void MainFrame::OpenRepository(const wxString& path) { LoadRepository(path); }

void MainFrame::LoadRepository(const wxString& path) {
    busy_.store(true);
    SetStatusText(wxString::Format(_("Loading %s…"), path));
    if (worker_.joinable()) {
        worker_.join();
    }

    const std::string path_utf8(path.utf8_str());
    worker_ = std::thread([this, path_utf8] {
        GitDriver driver;
        LogOptions options;
        options.max_count = 2000;
        auto result = driver.log(std::filesystem::path(path_utf8), options);

        CallAfter([this, path_utf8, result = std::move(result)]() mutable {
            busy_.store(false);
            if (!result.ok()) {
                SetStatusText(wxString::Format(
                    _("Failed to read repository: %s"),
                    wxString::FromUTF8(result.error().message)));
                return;
            }
            const auto count = result.value().size();
            model_->ReplaceAll(std::move(result).value());
            details_->ChangeValue(wxEmptyString);

            // Size the graph column to the widest row of this history, then
            // fit every other column to what it actually holds.
            graph_renderer_->SetMaxLanes(model_->max_lanes());
            FitColumns();

            SetStatusText(wxString::Format(_("%zu commits — %s"), count,
                                           wxString::FromUTF8(path_utf8)));
        });
    });
}
