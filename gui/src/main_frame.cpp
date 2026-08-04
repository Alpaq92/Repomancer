// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "main_frame.h"

#include <repomancer/vcs/git/git_driver.h>

#include <wx/aboutdlg.h>
#include <wx/dirdlg.h>
#include <wx/menu.h>

#include <filesystem>
#include <string>
#include <utility>

using repomancer::vcs::LogOptions;
using repomancer::vcs::git::GitDriver;

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, _("Repomancer"), wxDefaultPosition, wxSize(1000, 650)) {
    auto* file_menu = new wxMenu;
    file_menu->Append(wxID_OPEN, _("&Open Repository…\tCtrl-O"),
                      _("Open a local git repository"));
    file_menu->AppendSeparator();
    file_menu->Append(wxID_EXIT);

    auto* help_menu = new wxMenu;
    help_menu->Append(wxID_ABOUT);

    auto* menu_bar = new wxMenuBar;
    menu_bar->Append(file_menu, _("&File"));
    menu_bar->Append(help_menu, _("&Help"));
    SetMenuBar(menu_bar);

    CreateStatusBar();
    SetStatusText(_("Open a repository to begin"));

    log_view_ = new wxDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                   wxDV_ROW_LINES | wxDV_VERT_RULES);
    model_ = wxObjectDataPtr<CommitLogModel>(new CommitLogModel);
    log_view_->AssociateModel(model_.get());
    log_view_->AppendTextColumn(_("Subject"), CommitLogModel::Col_Subject,
                                wxDATAVIEW_CELL_INERT, 460, wxALIGN_LEFT,
                                wxDATAVIEW_COL_RESIZABLE);
    log_view_->AppendTextColumn(_("Author"), CommitLogModel::Col_Author, wxDATAVIEW_CELL_INERT,
                                160, wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    log_view_->AppendTextColumn(_("Date"), CommitLogModel::Col_Date, wxDATAVIEW_CELL_INERT, 140,
                                wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    log_view_->AppendTextColumn(_("Hash"), CommitLogModel::Col_Hash, wxDATAVIEW_CELL_INERT, 100,
                                wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);

    aui_.SetManagedWindow(this);
    aui_.AddPane(log_view_, wxAuiPaneInfo().CenterPane().Name("log"));
    aui_.Update();

    Bind(wxEVT_MENU, &MainFrame::OnOpenRepository, this, wxID_OPEN);
    Bind(wxEVT_MENU, &MainFrame::OnQuit, this, wxID_EXIT);
    Bind(wxEVT_MENU, &MainFrame::OnAbout, this, wxID_ABOUT);
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
            SetStatusText(wxString::Format(_("%zu commits — %s"), count,
                                           wxString::FromUTF8(path_utf8)));
        });
    });
}
