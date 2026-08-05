// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "main_frame.h"

#include "blame_dialog.h"
#include "commit_dialog.h"
#include "file_history_dialog.h"
#include "gtk_header_bar.h"
#include "icons.h"
#include "pane_header.h"
#include "preferences_dialog.h"
#include "text_sanitize.h"
#include "theme.h"
#include "title_bar.h"

#include <repomancer/settings.h>
#include <repomancer/vcs/git/git_driver.h>
#include <repomancer/vcs/patch.h>

#include <wx/aboutdlg.h>
#include <wx/aui/dockart.h>
#include <wx/clipbrd.h>
#include <wx/dcclient.h>
#include <wx/dirdlg.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <algorithm>
#include <filesystem>
#include <functional>
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
    ID_StyleRounded,
    ID_StyleAngular,
    // Popup menus allocate their own wxID_ANY ids on their menu objects;
    // frame-level ids continue clear of that range.
    ID_Refresh = wxID_HIGHEST + 20,
    ID_Recent1, // … ID_Recent1 + kMaxRecentRepos - 1
    ID_Fetch = wxID_HIGHEST + 40,
    ID_Pull,
    ID_Push,
    ID_Stash,
    ID_StashPop,
    ID_TrustRepo,
};
} // namespace

MainFrame::MainFrame() {
    const auto startup_settings = repomancer::load_settings();
    recent_repos_ = startup_settings.recent_repos;
    // Frame chrome is decided once, at creation; nothing after the ctor
    // reads these (a settings change restarts the process instead).
    bool integrated_titlebar = false;
    bool topbar_native = false;
    repomancer::gui::TitleBar* title_bar = nullptr;
    (void)topbar_native;
    (void)title_bar;
#ifdef REPOMANCER_HAVE_WXBF
    integrated_titlebar = startup_settings.integrated_titlebar;
    if (integrated_titlebar) {
        // wxbf's Create swaps the WM title bar for an empty header, making
        // room for the integrated strip.
        MainFrameBase::Create(nullptr, wxID_ANY, _("Repomancer"), wxDefaultPosition,
                              wxSize(1000, 650));
    } else {
        // Plain frame creation on the same base: native decorations, and
        // none of wxbf's machinery engages.
        wxFrame::Create(nullptr, wxID_ANY, _("Repomancer"), wxDefaultPosition,
                        wxSize(1000, 650));
    }
#else
    wxFrame::Create(nullptr, wxID_ANY, _("Repomancer"), wxDefaultPosition,
                    wxSize(1000, 650));
#endif
    // Bitmaps go on before Append: wxGTK ignores a bitmap set afterwards.
    auto* file_menu = new wxMenu;
    auto* open_item = new wxMenuItem(file_menu, wxID_OPEN, _("&Open Repository…\tCtrl-O"),
                                     _("Open a local git repository"));
    open_item->SetBitmap(repomancer::gui::icons::menu_icon(repomancer::gui::icons::kFolderGit));
    file_menu->Append(open_item);
    auto* prefs_item = new wxMenuItem(file_menu, wxID_PREFERENCES,
                                      _("&Preferences…\tCtrl-,"),
                                      _("Configure the VCS tools Repomancer drives"));
    prefs_item->SetBitmap(repomancer::gui::icons::menu_icon(repomancer::gui::icons::kSettings));
    file_menu->Append(prefs_item);
    recent_menu_ = new wxMenu;
    auto* recent_item = new wxMenuItem(file_menu, wxID_ANY, _("Open &Recent"),
                                       wxEmptyString, wxITEM_NORMAL, recent_menu_);
    recent_item->SetBitmap(
        repomancer::gui::icons::menu_icon(repomancer::gui::icons::kFolderClock));
    file_menu->Append(recent_item);
    RebuildRecentMenu();
    auto* refresh_item = new wxMenuItem(file_menu, ID_Refresh, _("&Refresh\tF5"),
                                        _("Reload the repository from disk"));
    refresh_item->SetBitmap(
        repomancer::gui::icons::menu_icon(repomancer::gui::icons::kRotateCw));
    file_menu->Append(refresh_item);
    file_menu->AppendSeparator();
    auto* quit_item = new wxMenuItem(file_menu, wxID_EXIT);
    quit_item->SetBitmap(repomancer::gui::icons::menu_icon(repomancer::gui::icons::kExit));
    file_menu->Append(quit_item);

    auto* repo_menu = new wxMenu;
    const auto repo_item = [repo_menu](int id, const wxString& label,
                                       const wxString& help, const char* icon) {
        auto* item = new wxMenuItem(repo_menu, id, label, help);
        item->SetBitmap(repomancer::gui::icons::menu_icon(icon));
        repo_menu->Append(item);
    };
    repo_item(ID_Fetch, _("&Fetch\tCtrl-Shift-F"),
              _("Download new commits from every remote"),
              repomancer::gui::icons::kArrowDownToLine);
    repo_item(ID_Pull, _("&Pull\tCtrl-Shift-P"),
              _("Fast-forward the current branch from its upstream"),
              repomancer::gui::icons::kArrowDown);
    repo_item(ID_Push, _("Pus&h\tCtrl-P"),
              _("Publish the current branch to its upstream"),
              repomancer::gui::icons::kArrowUp);
    repo_menu->AppendSeparator();
    repo_item(ID_Stash, _("&Stash Changes…"), _("Set the tracked changes aside"),
              repomancer::gui::icons::kLayers2);
    repo_item(ID_StashPop, _("Pop S&tash"), _("Re-apply and drop the newest stash"),
              repomancer::gui::icons::kUndo2);
    repo_menu->AppendSeparator();
    auto* trust_item = new wxMenuItem(
        repo_menu, ID_TrustRepo, _("Trust This Repositor&y"),
        _("Allow this repository's configuration and enable changes"));
    trust_item->SetBitmap(
        repomancer::gui::icons::menu_icon(repomancer::gui::icons::kShieldCheck));
    repo_menu->Append(trust_item);

    auto* theme_menu = new wxMenu;
    theme_menu->AppendRadioItem(ID_ThemeSystem, _("&System"));
    theme_menu->AppendRadioItem(ID_ThemeLight, _("&Light"));
    theme_menu->AppendRadioItem(ID_ThemeDark, _("&Dark"));
    auto* style_menu = new wxMenu;
    style_menu->AppendRadioItem(ID_StyleAngular, _("&Angular"));
    style_menu->AppendRadioItem(ID_StyleRounded, _("&Rounded"));

    auto* view_menu = new wxMenu;
    auto* theme_item = new wxMenuItem(view_menu, wxID_ANY, _("&Theme"), wxEmptyString,
                                      wxITEM_NORMAL, theme_menu);
    theme_item->SetBitmap(repomancer::gui::icons::menu_icon(repomancer::gui::icons::kEclipse));
    view_menu->Append(theme_item);
    auto* style_item = new wxMenuItem(view_menu, wxID_ANY, _("&Graph style"), wxEmptyString,
                                      wxITEM_NORMAL, style_menu);
    style_item->SetBitmap(repomancer::gui::icons::menu_icon(repomancer::gui::icons::kGitPullRequestArrow));
    view_menu->Append(style_item);

    auto* help_menu = new wxMenu;
    auto* about_item = new wxMenuItem(help_menu, wxID_ABOUT);
    about_item->SetBitmap(repomancer::gui::icons::menu_icon(repomancer::gui::icons::kBookmark));
    help_menu->Append(about_item);

#ifdef REPOMANCER_HAVE_WXBF
    if (integrated_titlebar) {
        // No native menu bar: the integrated title bar shows the menus as
        // buttons and pops them on this frame, so the command events land on
        // the same handlers. The accelerators the menu bar would have
        // installed are registered explicitly.
        file_menu_owned_.reset(file_menu);
        repo_menu_owned_.reset(repo_menu);
        view_menu_owned_.reset(view_menu);
        help_menu_owned_.reset(help_menu);
        using Buttons = repomancer::gui::TitleBar::Buttons;
        const Buttons buttons =
            repomancer::gui::TitleBar::FromSetting(startup_settings.topbar_buttons);
        topbar_native = buttons == Buttons::Native;
        title_bar = new repomancer::gui::TitleBar(
            this,
            {{_("File"), file_menu},
             {_("Repository"), repo_menu},
             {_("View"), view_menu},
             {_("Help"), help_menu}},
            buttons);
        SetBorderColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
        const wxAcceleratorEntry accelerators[] = {
            {wxACCEL_CTRL, 'O', wxID_OPEN},
            {wxACCEL_CTRL, ',', wxID_PREFERENCES},
            {wxACCEL_NORMAL, WXK_F5, ID_Refresh},
            {wxACCEL_CTRL | wxACCEL_SHIFT, 'F', ID_Fetch},
            {wxACCEL_CTRL | wxACCEL_SHIFT, 'P', ID_Pull},
            {wxACCEL_CTRL, 'P', ID_Push},
        };
        SetAcceleratorTable(
            wxAcceleratorTable(WXSIZEOF(accelerators), accelerators));
    }
#endif
    if (!integrated_titlebar) {
        auto* menu_bar = new wxMenuBar;
        menu_bar->Append(file_menu, _("&File"));
        menu_bar->Append(repo_menu, _("&Repository"));
        menu_bar->Append(view_menu, _("&View"));
        menu_bar->Append(help_menu, _("&Help"));
        SetMenuBar(menu_bar);
    }

    const auto startup_style =
        repomancer::gui::graph_style_from_string(startup_settings.graph_style);
    git_config_.binary = std::filesystem::path(startup_settings.git_binary);
    style_menu->Check(startup_style == repomancer::gui::GraphStyle::Angular ? ID_StyleAngular
                                                                           : ID_StyleRounded,
                      true);

    switch (repomancer::gui::theme_mode_from_string(startup_settings.theme)) {
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

    model_ = std::make_unique<CommitLogModel>();
    // The history is an owner-drawn canvas under a native header strip, not a
    // tree view — see log_view.h for why. Like every other table it sits one
    // hairline inside its pane, which draws the outer line around it.
    log_panel_ = new wxPanel(this);
    log_panel_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
    log_ = new repomancer::gui::LogView(log_panel_, model_.get());
    {
        // The pane borders the window on the right, so like the Diff pane it
        // gets a right margin sized with the others once the layout is known.
        auto* sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(log_, 1, wxEXPAND | wxTOP | wxBOTTOM | wxLEFT, 1);
        log_margin_ = sizer->AddSpacer(0);
        log_panel_->SetSizer(sizer);
    }
    log_->SetStyle(startup_style);
    log_->SetOnSelect([this](int row) { ShowCommit(row); });
    log_->SetOnContextMenu([this](int row) { OnLogMenu(row); });

    // Wrapped, not clipped: a long subject or a wide body should reflow
    // rather than force horizontal scrolling, and the text needs room to
    // breathe away from the pane border.
    // Each pane is titled by a real header control sitting above its content,
    // the same kind the log uses for its columns, so both read as one sort of
    // header. wxAUI's own caption is switched off for these panes.
    const auto titled = [](wxPanel* panel, const wxString& title, wxWindow* content,
                           bool separated = false) {
        auto* header = new repomancer::gui::PaneHeader(panel, title);
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(header, 0, wxEXPAND);
        if (separated) {
            // A hairline between the pane title and the content's own column
            // header, so two stacked header bands read as two, not one blur.
            auto* line = new wxWindow(panel, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
            line->SetBackgroundColour(repomancer::gui::hairline_colour(
                wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT),
                wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE)));
            sizer->Add(line, 0, wxEXPAND);
        }
        sizer->Add(content, 1, wxEXPAND);
        panel->SetSizer(sizer);
    };

    // The detail panes are inset from the inside throughout: Scintilla's own
    // text margins at the sides, a blank leading line for the top — it has no
    // top-margin setting — and a blank spacer column in the file list. Nothing
    // is inset with an outer border, so every control still reaches its pane
    // edges and its scrollbars sit where they belong.
    // No wrapping: the pane is mostly hashes and identity lines, and breaking
    // a 40-character hash across two rows reads as damage rather than reflow.
    // The inset comes from a sizer border — wxTextCtrl::SetMargins is not
    // honoured for multiline text on every port — with SetMargins on top for
    // the ports that do take it.
    // A styled text control rather than wxTextCtrl: the latter ignores
    // SetMargins for multiline text on GTK, so its first character sits hard
    // against the frame. Read-only and unlexed, it is just a text pane with a
    // margin that works.
    // Like the sidebar, the pane holds one bordered table — header and text
    // as a unit — inset so the outline is visible against the pane.
    details_panel_ = new wxPanel(this);
    details_panel_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
    auto* details_table = new wxPanel(details_panel_, wxID_ANY, wxDefaultPosition,
                                      wxDefaultSize, wxBORDER_NONE);
    details_ = new wxStyledTextCtrl(details_table, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                    wxBORDER_NONE);
    details_->SetReadOnly(true);
    details_->SetLexer(wxSTC_LEX_NULL);
    details_->SetWrapMode(wxSTC_WRAP_NONE);
    for (int margin = 0; margin < 3; ++margin) {
        details_->SetMarginWidth(margin, 0);
    }
    details_->SetMarginLeft(10);
    details_->SetMarginRight(10);
    // Scintilla has no top margin and one uniform line height for the whole
    // document, so a short pad line is not possible: extra ascent is the only
    // space it will put above text, and it applies to every line. That reads
    // as an inset at the top and slightly airier lines below it.
    details_->SetExtraAscent(7);
    details_->SetExtraDescent(1);
    details_->StyleSetFont(wxSTC_STYLE_DEFAULT,
                           wxFont(wxFontInfo().Family(wxFONTFAMILY_TELETYPE)));
    titled(details_table, _("Commit details"), details_);
    {
        // Aligned with its neighbours: the table's header sits level with
        // "Changed files" and "Diff", one hairline inside the pane, and the
        // left margin matches the gap between panes (sized with the
        // sidebar's, once the dock layout is known).
        auto* outer = new wxBoxSizer(wxHORIZONTAL);
        details_margin_ = outer->AddSpacer(0);
        outer->Add(details_table, 1, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 1);
        details_panel_->SetSizer(outer);
    }

    // Both neighbours get the details pane's arrangement: an outlined table
    // one hairline inside its pane. Their left edges sit on sashes, which
    // already provide the gap; the Diff pane borders the window on the
    // right, so its right margin is sized with the others once the dock
    // layout is known.
    files_panel_ = new wxPanel(this);
    files_panel_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
    auto* files_table = new wxPanel(files_panel_, wxID_ANY, wxDefaultPosition,
                                    wxDefaultSize, wxBORDER_NONE);
    // Native, not wx's generic list: the generic control decorates the
    // focused row with a dotted rectangle, which reads as a broken highlight.
    files_ = new wxDataViewListCtrl(files_table, wxID_ANY, wxDefaultPosition,
                                    wxDefaultSize, wxDV_SINGLE | wxBORDER_NONE);
    // The native list pads its cells itself — no spacer column needed. The
    // trailing filler keeps the header band spanning the pane.
    files_->AppendTextColumn(_("Change"), wxDATAVIEW_CELL_INERT, 90);
    files_->AppendTextColumn(_("File"), wxDATAVIEW_CELL_INERT, 320);
    files_->AppendTextColumn(wxEmptyString, wxDATAVIEW_CELL_INERT, 0);
    files_->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        event.Skip();
        int used = 0;
        for (unsigned int i = 0; i < 2; ++i) {
            if (const wxDataViewColumn* column = files_->GetColumn(i)) {
                used += column->GetWidth();
            }
        }
        if (wxDataViewColumn* filler = files_->GetColumn(2)) {
            filler->SetWidth(std::max(0, files_->GetClientSize().GetWidth() - used));
        }
    });
    titled(files_table, _("Changed files"), files_, /*separated=*/true);
    {
        auto* outer = new wxBoxSizer(wxVERTICAL);
        outer->Add(files_table, 1, wxEXPAND | wxALL, 1);
        files_panel_->SetSizer(outer);
    }

    diff_panel_ = new wxPanel(this);
    diff_panel_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
    auto* diff_table = new wxPanel(diff_panel_, wxID_ANY, wxDefaultPosition,
                                   wxDefaultSize, wxBORDER_NONE);
    diff_ = new repomancer::gui::DiffView(diff_table);
    diff_->SetOnHunkMenu(
        [this](const repomancer::vcs::FileDiff& file, std::size_t hunk) {
            OnHunkMenu(file, hunk);
        });
    titled(diff_table, _("Diff"), diff_);
    {
        auto* outer = new wxBoxSizer(wxHORIZONTAL);
        outer->Add(diff_table, 1, wxEXPAND | wxTOP | wxBOTTOM | wxLEFT, 1);
        diff_margin_ = outer->AddSpacer(0);
        diff_panel_->SetSizer(outer);
    }

    // The sidebar is a one-column, one-row table: its column header is the
    // pane title, and the row's only cell carries the repository details. It
    // floats exactly like the history table: top and bottom at its dock edges,
    // the sash as its right gap, and a left margin of one sash width so the
    // window edge keeps the same distance every other pane gets.
    repo_panel_ = new wxPanel(this);
    repo_panel_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
    repo_view_ = new repomancer::gui::RepoView(repo_panel_);
    repo_view_->SetOnActivate(
        [this](const repomancer::gui::RepoView::Target& target) { JumpToRef(target); });
    repo_view_->SetOnBranchMenu(
        [this](const wxString& branch) { OnBranchMenu(branch); });
    {
        // Horizontal: a spacer, sized once the dock layout is known, then the
        // table filling the rest. One pixel on the other sides keeps room for
        // the outline the pane draws around the table.
        auto* sizer = new wxBoxSizer(wxHORIZONTAL);
        repo_margin_ = sizer->AddSpacer(0);
        sizer->Add(repo_view_, 1, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 1);
        repo_panel_->SetSizer(sizer);
    }

    // Each inset table is outlined by its pane, not by a native frame: GTK
    // frames a panel and a tree view in different greys, so the only way two
    // outlines match exactly — in either theme — is to draw both ourselves.
    const auto outlined = [](wxPanel* pane, wxWindow* table) {
        pane->Bind(wxEVT_PAINT, [pane, table](wxPaintEvent&) {
            wxPaintDC dc(pane);
            dc.SetPen(wxPen(repomancer::gui::hairline_colour(
                                wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT),
                                wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE)),
                            1));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            wxRect box = table->GetRect();
            box.Inflate(1, 1);
            dc.DrawRectangle(box);
        });
        pane->Bind(wxEVT_SIZE, [pane](wxSizeEvent& event) {
            event.Skip();
            pane->Refresh();
        });
    };
    outlined(repo_panel_, repo_view_);
    outlined(details_panel_, details_table);
    outlined(files_panel_, files_table);
    outlined(diff_panel_, diff_table);
    outlined(log_panel_, log_);

    // GitX-style master/detail: history on top, and below it the commit's
    // metadata, the files it touched, and the patch for whichever is selected.
    aui_.SetManagedWindow(this);
#ifdef REPOMANCER_HAVE_WXBF
#ifdef __WXGTK__
    if (integrated_titlebar && topbar_native) {
        // The panel moves into the window's header bar; GTK draws the
        // theme's own window buttons and handles dragging.
        title_bar->Layout();
        repomancer::gui::host_in_header_bar(
            GetHandle(), title_bar->GetHandle(), title_bar->GetBestSize().x,
            FromDIP(32), startup_settings.topbar_sharp_corners);
    }
#endif
    if (integrated_titlebar && !topbar_native)
        aui_.AddPane(title_bar, wxAuiPaneInfo()
                                 .Name("titlebar")
                                 .Top()
                                 .Layer(2)
                                 .CaptionVisible(false)
                                 .PaneBorder(false)
                                 .Gripper(false)
                                 .Floatable(false)
                                 .Movable(false)
                                 .Dockable(false)
                                 .DockFixed()
                                 .Resizable(false)
                                 .MinSize(-1, repomancer::gui::TitleBar::kHeight)
                                 .MaxSize(-1, repomancer::gui::TitleBar::kHeight));
#endif
    aui_.AddPane(log_panel_, wxAuiPaneInfo().CenterPane().Name("log"));
    aui_.AddPane(repo_panel_, wxAuiPaneInfo()
                                 .Left()
                                 .Name("refs")
                                 .Caption(_("Repository"))
                                 .BestSize(230, -1)
                                 .MinSize(160, -1)
                                 .CloseButton(false)
                                 .CaptionVisible(false));
    aui_.AddPane(details_panel_, wxAuiPaneInfo()
                               .Bottom()
                               .Name("details")
                               .Caption(_("Commit details"))
                               .BestSize(360, 260)
                               .CloseButton(false)
                                 .CaptionVisible(false));
    aui_.AddPane(files_panel_, wxAuiPaneInfo()
                             .Bottom()
                             .Name("files")
                             .Caption(_("Changed files"))
                             .BestSize(360, 260)
                             .CloseButton(false)
                                 .CaptionVisible(false)
                             .Position(1));
    aui_.AddPane(diff_panel_, wxAuiPaneInfo()
                            .Bottom()
                            .Name("diff")
                            .Caption(_("Diff"))
                            .BestSize(520, 260)
                            .CloseButton(false)
                                 .CaptionVisible(false)
                            .Position(2));
    aui_.Update();

    // The window edge must keep the same distance from the sidebar table that
    // the dock layout leaves between the sidebar and the history table. That
    // gap is more than the sash metric alone, so it is measured off the
    // laid-out windows rather than derived from art settings.
    CallAfter([this] {
        // Both windows are the frame's own children, so their positions are
        // directly comparable — and unlike screen coordinates, they are valid
        // before the frame is shown.
        const int gap = log_panel_->GetPosition().x -
                        (repo_panel_->GetPosition().x +
                         repo_panel_->GetSize().GetWidth());
        if (gap > 0 && repo_margin_ != nullptr) {
            repo_margin_->AssignSpacer(gap, 0);
            repo_panel_->Layout();
            repo_panel_->Refresh();
        }
        if (gap > 0 && details_margin_ != nullptr) {
            details_margin_->AssignSpacer(gap, 0);
            details_panel_->Layout();
            details_panel_->Refresh();
        }
        if (gap > 0 && diff_margin_ != nullptr) {
            diff_margin_->AssignSpacer(gap, 0);
            diff_panel_->Layout();
            diff_panel_->Refresh();
        }
        if (gap > 0 && log_margin_ != nullptr) {
            log_margin_->AssignSpacer(gap, 0);
            log_panel_->Layout();
            log_panel_->Refresh();
        }
    });
    ApplyThemeToWidgets();

    Bind(wxEVT_MENU, &MainFrame::OnOpenRepository, this, wxID_OPEN);
    Bind(wxEVT_MENU, &MainFrame::OnThemeSelected, this, ID_ThemeSystem, ID_ThemeDark);
    Bind(wxEVT_MENU, &MainFrame::OnGraphStyleSelected, this, ID_StyleRounded, ID_StyleAngular);
    Bind(wxEVT_MENU, &MainFrame::OnPreferences, this, wxID_PREFERENCES);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (!repo_path_.empty() && !busy_.load()) {
                ReloadRepository();
            }
        },
        ID_Refresh);
    // The remote/stash operations all need a repository first; one guard
    // routes each to its worker.
    const auto repo_op = [this](void (MainFrame::*op)()) {
        return [this, op](wxCommandEvent&) {
            if (repo_path_.empty() || busy_.load()) {
                wxBell();
                return;
            }
            (this->*op)();
        };
    };
    Bind(wxEVT_MENU, repo_op(&MainFrame::FetchRemotes), ID_Fetch);
    Bind(wxEVT_MENU, repo_op(&MainFrame::PullCurrentBranch), ID_Pull);
    Bind(wxEVT_MENU, repo_op(&MainFrame::PushCurrentBranch), ID_Push);
    Bind(wxEVT_MENU, repo_op(&MainFrame::StashChanges), ID_Stash);
    Bind(wxEVT_MENU, repo_op(&MainFrame::PopStash), ID_StashPop);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (repo_path_.empty() ||
                git_config_.trust != repomancer::vcs::git::RepoTrust::ReadOnly) {
                wxBell();
                return;
            }
            std::error_code ec;
            const auto canonical =
                std::filesystem::weakly_canonical(repo_path_, ec);
            const std::string key = ec ? repo_path_.string() : canonical.string();
            wxMessageDialog ask(
                this,
                wxString::Format(_("Trust the authors of %s?\n\nIts configuration "
                                   "will be honoured from now on."),
                                 repomancer::gui::sanitized_utf8(key)),
                _("Trust Repository"), wxYES_NO | wxNO_DEFAULT | wxICON_AUTH_NEEDED);
            if (ask.ShowModal() != wxID_YES) {
                return;
            }
            auto settings = repomancer::load_settings();
            remember_trusted_repo(settings, key);
            repomancer::save_settings(settings);
            session_read_only_.erase(key);
            git_config_.trust = repomancer::vcs::git::RepoTrust::Trusted;
            git_config_.extra_neutralize.clear();
            repo_read_only_ = false;
            ReloadRepository();
        },
        ID_TrustRepo);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent& event) {
            const std::size_t index = static_cast<std::size_t>(event.GetId() - ID_Recent1);
            if (index < recent_repos_.size() && !busy_.load()) {
                LoadRepository(wxString::FromUTF8(recent_repos_[index]));
            }
        },
        ID_Recent1, ID_Recent1 + repomancer::kMaxRecentRepos - 1);
    Bind(wxEVT_MENU, &MainFrame::OnQuit, this, wxID_EXIT);
    Bind(wxEVT_MENU, &MainFrame::OnAbout, this, wxID_ABOUT);
    files_->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &MainFrame::OnFileSelected, this);
    files_->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, [this](wxDataViewEvent&) {
        const int row = files_->GetSelectedRow();
        if (row == wxNOT_FOUND) {
            return;
        }
        wxMenu menu;
        using repomancer::gui::icons::menu_icon;
        // Append + bind in one place; wx allots the ids, so this menu can
        // never collide with frame-level ones.
        const auto add = [&menu](const wxString& label, const char* icon,
                                 std::function<void()> action) {
            auto* item = new wxMenuItem(&menu, wxID_ANY, label);
            if (icon != nullptr) {
                item->SetBitmap(menu_icon(icon));
            }
            menu.Append(item);
            menu.Bind(
                wxEVT_MENU,
                [action = std::move(action)](wxCommandEvent&) { action(); },
                item->GetId());
        };
        const auto at = static_cast<std::size_t>(row);
        if (at >= changed_files_.size()) {
            return;
        }
        if (worktree_mode_ && at < worktree_entries_.size()) {
            // The path is captured by VALUE: the popup runs a nested event
            // loop in which a pending refresh may rebuild the list, so a row
            // index would be stale by action time.
            const auto& entry = worktree_entries_[at];
            const std::string wt_path = entry.path;
            const bool staged =
                entry.kind == repomancer::vcs::EntryKind::Ordinary && entry.x != '.';
            const bool stageable = entry.kind == repomancer::vcs::EntryKind::Untracked ||
                                   entry.y != '.';
            if (stageable) {
                add(_("&Stage"), nullptr,
                    [this, wt_path] { StageWorktreeFile(wt_path, true); });
            }
            if (staged) {
                add(_("&Unstage"), nullptr,
                    [this, wt_path] { StageWorktreeFile(wt_path, false); });
            }
            const bool any_staged = std::any_of(
                worktree_entries_.begin(), worktree_entries_.end(), [](const auto& e) {
                    return e.kind == repomancer::vcs::EntryKind::Ordinary && e.x != '.';
                });
            if (any_staged) {
                add(_("&Commit Staged Changes…"), nullptr, [this] { CommitStaged(); });
            }
            // Whole-file discard is defined for plain edits and untracked
            // files. On a staged rename `git restore` would delete the new
            // name and leave a half-staged deletion — worse than no button.
            if (entry.kind == repomancer::vcs::EntryKind::Ordinary ||
                entry.kind == repomancer::vcs::EntryKind::Untracked) {
                add(_("&Discard Changes…"), nullptr,
                    [this, wt_path] { DiscardFile(wt_path); });
            }
            menu.AppendSeparator();
        }
        const std::string file_path = changed_files_[at].path;
        add(_("&Open file"), repomancer::gui::icons::kFile,
            [this, file_path] { OpenChangedFile(file_path); });
        add(_("Open containing &folder"), repomancer::gui::icons::kFolder,
            [this, file_path] { OpenContainingFolder(file_path); });
        menu.AppendSeparator();
        add(_("File &History…"), repomancer::gui::icons::kClock4,
            [this, file_path] { ShowFileHistory(file_path); });
        add(_("&Blame…"), repomancer::gui::icons::kSearch,
            [this, file_path] { ShowFileBlame(file_path); });
        files_->PopupMenu(&menu);
    });
}


namespace {

// A porcelain XY pair as a human word for the Change column.
wxString worktree_change_label(const repomancer::vcs::StatusEntry& entry) {
    using repomancer::vcs::EntryKind;
    switch (entry.kind) {
    case EntryKind::Untracked:
        return _("untracked");
    case EntryKind::Ignored:
        return _("ignored");
    case EntryKind::Unmerged:
        return _("unmerged");
    case EntryKind::RenamedCopied:
        return _("renamed");
    case EntryKind::Ordinary:
        break;
    }
    const char state = entry.y != '.' ? entry.y : entry.x;
    switch (state) {
    case 'M':
        return _("modified");
    case 'A':
        return _("added");
    case 'D':
        return _("deleted");
    case 'T':
        return _("type changed");
    default:
        return _("changed");
    }
}

void CopyToClipboard(const wxString& text) {
    if (wxTheClipboard->Open()) {
        wxTheClipboard->SetData(new wxTextDataObject(text));
        wxTheClipboard->Close();
    }
}

} // namespace

void MainFrame::ReloadRepository() {
    // Completion callbacks reach for this while another load may be in
    // flight (e.g. a fetch finishing during the trust-gate dialog); a reload
    // must never stack on a running one.
    if (busy_.load()) {
        return;
    }
    LoadRepository(wxString::FromUTF8(repo_path_.string()));
}

bool MainFrame::RepoWritable() {
    if (!repo_read_only_) {
        return true;
    }
    SetStatusText(_("Repository is open read-only — Repository ▸ Trust This "
                    "Repository to enable changes"));
    return false;
}

void MainFrame::ShowWorktree() {
    SetStatusText(_("Reading the working tree…"));
    RunGitOp(
        _("Could not read the working tree: "),
        [](const std::filesystem::path& repo,
           const repomancer::vcs::git::GitConfig& config) {
            return GitDriver(config).status(repo);
        },
        [this](repomancer::vcs::StatusSnapshot snapshot) {
            worktree_mode_ = true;
            selected_commit_.clear();
            worktree_entries_ = snapshot.entries;

            wxString text;
            text << _("Working tree") << "\n";
            text << _("Branch:  ")
                 << repomancer::gui::sanitized_utf8(snapshot.branch.head) << "\n";
            if (!snapshot.branch.upstream.empty()) {
                text << _("Upstream: ")
                     << repomancer::gui::sanitized_utf8(snapshot.branch.upstream)
                     << wxString::Format("  (+%d / -%d)", snapshot.branch.ahead,
                                         snapshot.branch.behind)
                     << "\n";
            }
            text << "\n"
                 << wxString::Format(wxPLURAL("%zu changed file", "%zu changed files",
                                              snapshot.entries.size()),
                                     snapshot.entries.size());
            SetDetailsText(text);

            // Keep the user's place across stage/unstage refreshes.
            std::string keep;
            if (const int row = files_->GetSelectedRow();
                row != wxNOT_FOUND &&
                static_cast<std::size_t>(row) < changed_files_.size()) {
                keep = changed_files_[static_cast<std::size_t>(row)].path;
            }

            files_->DeleteAllItems();
            changed_files_.clear();
            diff_->ShowMessage(_("Select a file to see its changes."));
            for (const auto& entry : snapshot.entries) {
                repomancer::vcs::ChangedFile file;
                file.path = entry.path;
                file.old_path = entry.orig_path;
                changed_files_.push_back(file);
                AppendFileRow(worktree_change_label(entry), entry.path,
                              entry.orig_path);
            }
            if (!changed_files_.empty()) {
                int select = 0;
                for (std::size_t i = 0; i < changed_files_.size(); ++i) {
                    if (changed_files_[i].path == keep) {
                        select = static_cast<int>(i);
                        break;
                    }
                }
                files_->SelectRow(select);
                ShowFileDiff(select);
            }
            SetStatusText(wxString::Format(
                wxPLURAL("Working tree — %zu changed file",
                         "Working tree — %zu changed files",
                         snapshot.entries.size()),
                snapshot.entries.size()));
        });
}

void MainFrame::StageWorktreeFile(const std::string& path, bool stage) {
    if (!RepoWritable()) {
        return;
    }
    RunGitOp(
        _("Staging failed: "),
        [path, stage](const std::filesystem::path& repo,
                      const repomancer::vcs::git::GitConfig& config) {
            return stage ? GitDriver(config).stage(repo, path)
                         : GitDriver(config).unstage(repo, path);
        },
        [this](std::string) { ShowWorktree(); /* fresh status, fresh lists */ });
}

void MainFrame::CommitStaged() {
    if (!RepoWritable()) {
        return;
    }
    const int staged = static_cast<int>(std::count_if(
        worktree_entries_.begin(), worktree_entries_.end(), [](const auto& e) {
            return e.kind == repomancer::vcs::EntryKind::Ordinary && e.x != '.';
        }));
    repomancer::gui::CommitDialog dialog(this, staged);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    const std::string message(dialog.Message().utf8_str());
    SetStatusText(_("Committing…"));
    RunGitOp(
        _("Commit failed: "),
        [message](const std::filesystem::path& repo,
                  const repomancer::vcs::git::GitConfig& config) {
            return GitDriver(config).commit(repo, message);
        },
        [this](std::string) { ReloadRepository(); /* the history changed */ });
}

void MainFrame::OnBranchMenu(const wxString& branch) {
    const std::string name(branch.utf8_str());
    wxMenu menu;
    using repomancer::gui::icons::menu_icon;
    auto* switch_item = new wxMenuItem(
        &menu, wxID_ANY, wxString::Format(_("&Switch to \"%s\""), branch));
    switch_item->SetBitmap(menu_icon(repomancer::gui::icons::kSquareArrowRightEnter));
    menu.Append(switch_item);
    switch_item->Enable(name != current_branch_);
    auto* new_branch_item = new wxMenuItem(&menu, wxID_ANY, _("&New Branch Here…"));
    new_branch_item->SetBitmap(menu_icon(repomancer::gui::icons::kGitBranchPlus));
    menu.Append(new_branch_item);
    menu.Bind(
        wxEVT_MENU, [this, name](wxCommandEvent&) { SwitchBranch(name); },
        switch_item->GetId());
    menu.Bind(
        wxEVT_MENU,
        [this, name](wxCommandEvent&) {
            // The new branch starts at HEAD of the clicked branch only when
            // that branch is checked out; keep it simple and explicit: the
            // dialog names it, creation happens at the current HEAD.
            wxTextEntryDialog dialog(this, _("Name for the new branch:"),
                                     _("New Branch"));
            if (dialog.ShowModal() == wxID_OK &&
                !dialog.GetValue().Trim(true).Trim(false).empty()) {
                CreateBranch(std::string(
                    dialog.GetValue().Trim(true).Trim(false).utf8_str()));
            }
        },
        new_branch_item->GetId());
    repo_view_->PopupMenu(&menu);
}

void MainFrame::SwitchBranch(const std::string& branch) {
    if (!RepoWritable()) {
        return;
    }
    SetStatusText(wxString::Format(_("Switching to %s…"), wxString::FromUTF8(branch)));
    RunGitOp(
        _("Switch failed: "),
        [branch](const std::filesystem::path& repo,
                 const repomancer::vcs::git::GitConfig& config) {
            return GitDriver(config).switch_branch(repo, branch);
        },
        [this](std::string) { ReloadRepository(); });
}

void MainFrame::CreateBranch(const std::string& branch) {
    if (!RepoWritable()) {
        return;
    }
    RunGitOp(
        _("Branch creation failed: "),
        [branch](const std::filesystem::path& repo,
                 const repomancer::vcs::git::GitConfig& config) {
            return GitDriver(config).create_branch(repo, branch, /*checkout=*/true);
        },
        [this](std::string) { ReloadRepository(); });
}

void MainFrame::FetchRemotes() {
    if (!RepoWritable()) {
        return;
    }
    SetStatusText(_("Fetching from every remote…"));
    RunGitOp(
        _("Fetch failed: "),
        [](const std::filesystem::path& repo,
           const repomancer::vcs::git::GitConfig& config) {
            return GitDriver(config).fetch(repo);
        },
        [this](std::string) { ReloadRepository(); });
}

void MainFrame::PullCurrentBranch() {
    if (!RepoWritable()) {
        return;
    }
    SetStatusText(_("Pulling…"));
    RunGitOp(
        _("Pull failed: "),
        [](const std::filesystem::path& repo,
           const repomancer::vcs::git::GitConfig& config) {
            return GitDriver(config).pull(repo);
        },
        [this](std::string) { ReloadRepository(); });
}

void MainFrame::PushCurrentBranch() {
    if (!RepoWritable()) {
        return;
    }
    SetStatusText(_("Pushing…"));
    RunGitOp(
        _("Push failed: "),
        [](const std::filesystem::path& repo,
           const repomancer::vcs::git::GitConfig& config) {
            return GitDriver(config).push(repo);
        },
        [this](std::string) { ReloadRepository(); /* ahead/behind moved */ });
}

void MainFrame::StashChanges() {
    if (!RepoWritable()) {
        return;
    }
    wxTextEntryDialog dialog(this, _("Message for the stash (optional):"),
                             _("Stash Changes"));
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    const std::string message(dialog.GetValue().Trim(true).Trim(false).utf8_str());
    SetStatusText(_("Stashing…"));
    RunGitOp(
        _("Stash failed: "),
        [message](const std::filesystem::path& repo,
                  const repomancer::vcs::git::GitConfig& config) {
            return GitDriver(config).stash_save(repo, message);
        },
        [this](std::string) { ReloadRepository(); });
}

void MainFrame::PopStash() {
    if (!RepoWritable()) {
        return;
    }
    SetStatusText(_("Popping the newest stash…"));
    RunGitOp(
        _("Stash pop failed: "),
        [](const std::filesystem::path& repo,
           const repomancer::vcs::git::GitConfig& config) {
            return GitDriver(config).stash_pop(repo);
        },
        [this](std::string) { ReloadRepository(); });
}

void MainFrame::OnLogMenu(int row) {
    const auto* commit = model_->commit_at(static_cast<unsigned int>(row));
    if (commit == nullptr) {
        return;
    }
    const std::string hash = commit->hash;
    const wxString subject = repomancer::gui::sanitized_utf8(commit->subject);
    wxMenu menu;
    using repomancer::gui::icons::menu_icon;
    const auto add = [&menu](const wxString& label, const char* icon,
                             std::function<void()> action) {
        auto* item = new wxMenuItem(&menu, wxID_ANY, label);
        if (icon != nullptr) {
            item->SetBitmap(menu_icon(icon));
        }
        menu.Append(item);
        menu.Bind(
            wxEVT_MENU,
            [action = std::move(action)](wxCommandEvent&) { action(); },
            item->GetId());
    };
    add(_("&Copy Hash"), repomancer::gui::icons::kHash,
        [hash] { CopyToClipboard(wxString::FromUTF8(hash)); });
    add(_("Copy &Subject"), repomancer::gui::icons::kFileTypeCorner,
        [subject] { CopyToClipboard(subject); });
    menu.AppendSeparator();
    add(_("&Tag Here…"), repomancer::gui::icons::kTag, [this, hash] {
        if (!RepoWritable()) {
            return;
        }
        wxTextEntryDialog dialog(this, _("Name for the new tag:"), _("New Tag"));
        if (dialog.ShowModal() != wxID_OK) {
            return;
        }
        const std::string name(dialog.GetValue().Trim(true).Trim(false).utf8_str());
        if (name.empty()) {
            return;
        }
        RunGitOp(
            _("Tag creation failed: "),
            [name, hash](const std::filesystem::path& repo,
                         const repomancer::vcs::git::GitConfig& config) {
                return GitDriver(config).create_tag(repo, name, hash);
            },
            [this](std::string) { ReloadRepository(); });
    });
    log_->canvas()->PopupMenu(&menu);
}

void MainFrame::OnHunkMenu(const repomancer::vcs::FileDiff& file, std::size_t hunk) {
    // Hunk operations exist for working-tree changes only, and only on the
    // plain-modification diffs the patch builder covers.
    if (!worktree_mode_ || repo_read_only_ ||
        !repomancer::vcs::supports_hunk_ops(file) || hunk >= file.hunks.size()) {
        return;
    }
    const std::string patch = repomancer::vcs::single_hunk_patch(file, hunk);
    if (patch.empty()) {
        return;
    }
    const wxString where = wxString::Format(
        _("%s @@ -%d,%d"), repomancer::gui::sanitized_utf8(file.new_path),
        file.hunks[hunk].old_start, file.hunks[hunk].old_count);

    wxMenu menu;
    const auto add = [&menu](const wxString& label, std::function<void()> action) {
        auto* item = new wxMenuItem(&menu, wxID_ANY, label);
        menu.Append(item);
        menu.Bind(
            wxEVT_MENU,
            [action = std::move(action)](wxCommandEvent&) { action(); },
            item->GetId());
    };
    if (diff_is_staged_) {
        // The pane shows HEAD-vs-index: the one defined operation is taking
        // the hunk back out of the index.
        add(_("&Unstage Hunk"), [this, patch] {
            SetStatusText(_("Unstaging the hunk…"));
            RunGitOp(
                _("Hunk unstaging failed: "),
                [patch](const std::filesystem::path& repo,
                        const repomancer::vcs::git::GitConfig& config) {
                    return GitDriver(config).apply_patch(repo, patch, /*cached=*/true,
                                                         /*reverse=*/true);
                },
                [this](std::string) { ShowWorktree(); });
        });
        diff_->PopupMenu(&menu);
        return;
    }
    add(_("&Stage Hunk"), [this, patch] {
        SetStatusText(_("Staging the hunk…"));
        RunGitOp(
            _("Hunk staging failed: "),
            [patch](const std::filesystem::path& repo,
                    const repomancer::vcs::git::GitConfig& config) {
                return GitDriver(config).apply_patch(repo, patch, /*cached=*/true,
                                                     /*reverse=*/false);
            },
            [this](std::string) { ShowWorktree(); });
    });
    add(_("&Discard Hunk…"), [this, patch, where] {
        wxMessageDialog confirm(
            this,
            wxString::Format(_("Discard this hunk from the working tree?\n\n%s\n\n"
                               "The change is lost — there is no undo."),
                             where),
            _("Discard Hunk"), wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
        if (confirm.ShowModal() != wxID_YES) {
            return;
        }
        SetStatusText(_("Discarding the hunk…"));
        RunGitOp(
            _("Hunk discard failed: "),
            [patch](const std::filesystem::path& repo,
                    const repomancer::vcs::git::GitConfig& config) {
                return GitDriver(config).apply_patch(repo, patch, /*cached=*/false,
                                                     /*reverse=*/true);
            },
            [this](std::string) { ShowWorktree(); });
    });
    diff_->PopupMenu(&menu);
}

void MainFrame::DiscardFile(const std::string& path) {
    // The untracked branch deletes via the filesystem, not the driver, so
    // this GUI guard is the ONLY read-only barrier on that path.
    if (!RepoWritable()) {
        return;
    }
    // Everything is read by VALUE up front: the modal below runs a nested
    // event loop in which a pending refresh may reassign worktree_entries_,
    // so a reference held across ShowModal could dangle or re-point.
    const auto find = [this](const std::string& p)
        -> const repomancer::vcs::StatusEntry* {
        for (const auto& e : worktree_entries_) {
            if (e.path == p) {
                return &e;
            }
        }
        return nullptr;
    };
    const auto* entry = find(path);
    if (entry == nullptr) {
        return;
    }
    const bool untracked = entry->kind == repomancer::vcs::EntryKind::Untracked;
    const wxString shown = repomancer::gui::sanitized_utf8(path);
    wxMessageDialog confirm(
        this,
        untracked ? wxString::Format(_("Delete the untracked file %s?\n\n"
                                       "The file is lost — there is no undo."),
                                     shown)
                  : wxString::Format(_("Discard every change to %s (staged and "
                                       "unstaged)?\n\nThe changes are lost — there "
                                       "is no undo."),
                                     shown),
        _("Discard Changes"), wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
    if (confirm.ShowModal() != wxID_YES) {
        return;
    }
    // Re-validate after the modal: the list may have refreshed while the
    // dialog was up, and the confirmed file may be gone or changed kind.
    entry = find(path);
    if (entry == nullptr ||
        untracked != (entry->kind == repomancer::vcs::EntryKind::Untracked)) {
        SetStatusText(
            wxString::Format(_("%s changed while the dialog was open — nothing done"),
                             shown));
        return;
    }
    if (untracked) {
        std::error_code ec;
        std::filesystem::remove(repo_path_ / path, ec);
        if (ec) {
            SetStatusText(wxString::Format(_("Could not delete %s"), shown));
            return;
        }
        ShowWorktree();
        return;
    }
    SetStatusText(_("Discarding changes…"));
    RunGitOp(
        _("Discard failed: "),
        [path](const std::filesystem::path& repo,
               const repomancer::vcs::git::GitConfig& config) {
            return GitDriver(config).discard_file(repo, path);
        },
        [this](std::string) { ShowWorktree(); });
}

void MainFrame::AppendFileRow(const wxString& change, const std::string& path,
                              const std::string& old_path) {
    wxString shown = repomancer::gui::sanitized_utf8(path);
    if (!old_path.empty()) {
        shown = repomancer::gui::sanitized_utf8(old_path) + " \u2192 " + shown;
    }
    wxVector<wxVariant> row;
    row.push_back(wxVariant(change));
    row.push_back(wxVariant(shown));
    row.push_back(wxVariant(wxEmptyString));
    files_->AppendItem(row);
}

void MainFrame::ShowCommit(int row) {
    worktree_mode_ = false;
    const auto* commit =
        row >= 0 ? model_->commit_at(static_cast<unsigned int>(row)) : nullptr;
    if (commit == nullptr) {
        SetDetailsText(wxEmptyString);
        files_->DeleteAllItems();
        changed_files_.clear();
        selected_commit_.clear();
        diff_->Clear();
        return;
    }
    selected_commit_ = commit->hash;

    const auto& utf8 = repomancer::gui::sanitized_utf8;
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
    SetDetailsText(text);

    files_->DeleteAllItems();
    changed_files_.clear();
    diff_->ShowMessage(_("Select a file to see its changes."));

    // Off the UI thread: a git subprocess per selection would freeze the
    // whole window for its duration, which reads as a stutter on every
    // click. A newer selection bumps the generations, so a fetch that comes
    // back late finds itself stale and is dropped.
    const unsigned generation = ++files_generation_;
    ++diff_generation_;
    if (files_worker_.joinable()) {
        files_worker_.join();
    }
    files_worker_ = std::thread([this, generation, repo = repo_path_,
                                 commit = selected_commit_, config = git_config_] {
        auto files = GitDriver(config).changed_files(repo, commit);
        CallAfter([this, generation, files = std::move(files)]() mutable {
            if (generation != files_generation_.load()) {
                return;
            }
            if (!files.ok()) {
                diff_->ShowMessage(
                    wxString::Format(_("Could not list changed files: %s"),
                                     wxString::FromUTF8(files.error().message)));
                return;
            }
            changed_files_ = std::move(files).value();
            for (const auto& file : changed_files_) {
                const auto name = repomancer::vcs::file_change_name(file.change);
                AppendFileRow(wxString::FromUTF8(std::string(name)), file.path,
                              file.old_path);
            }
            if (!changed_files_.empty()) {
                files_->SelectRow(0);
                // Programmatic selection sends no event; show the diff
                // ourselves, as a click would.
                ShowFileDiff(0);
            }
        });
    });
}

void MainFrame::PopulateRepoDetails(
    const repomancer::vcs::VcsResult<repomancer::vcs::StatusSnapshot>& status,
    const repomancer::vcs::VcsResult<std::vector<repomancer::vcs::Ref>>& refs,
    const repomancer::vcs::VcsResult<std::vector<repomancer::vcs::Contributor>>& people,
    const repomancer::vcs::VcsResult<std::vector<repomancer::vcs::LanguageStat>>& langs) {
    using Row = repomancer::gui::RepoView::Row;
    using Target = repomancer::gui::RepoView::Target;
    // Attacker-influenced strings (branch names, identities) are stripped of
    // control characters before display, as everywhere else in the UI.
    const auto& clean = repomancer::gui::sanitized_utf8;

    std::vector<Row> rows;
    if (repo_read_only_) {
        // The one indicator that survives every status-bar update.
        Row badge;
        badge.text = _("Read-only — untrusted");
        badge.heading = true;
        rows.push_back(std::move(badge));
    }
    const auto section = [&rows](const wxString& title) {
        if (!rows.empty()) {
            rows.push_back({});
        }
        Row heading;
        heading.text = title;
        heading.heading = true;
        rows.push_back(std::move(heading));
    };

    if (status.ok() && !status.value().entries.empty()) {
        section(_("Working tree"));
        Row dirty;
        dirty.text = "  " + wxString::Format(
                               wxPLURAL("%zu changed file", "%zu changed files",
                                        status.value().entries.size()),
                               status.value().entries.size());
        dirty.target.kind = Target::Kind::Worktree;
        rows.push_back(std::move(dirty));
    }

    if (refs.ok()) {
        struct Group {
            repomancer::vcs::RefKind kind;
            wxString title;
            std::vector<Row> items;
        };
        Group groups[] = {
            {repomancer::vcs::RefKind::LocalBranch, _("Branches"), {}},
            {repomancer::vcs::RefKind::RemoteBranch, _("Remotes"), {}},
            {repomancer::vcs::RefKind::Stash, _("Stashes"), {}},
        };
        for (const auto& ref : refs.value()) {
            auto* group = std::find_if(std::begin(groups), std::end(groups),
                                       [&](const Group& g) { return g.kind == ref.kind; });
            if (group == std::end(groups)) {
                continue; // tags and the rest have no place in the sidebar
            }
            Row item;
            item.text = "  " + clean(ref.short_name);
            if (ref.is_head) {
                item.text += " \u2713";
            }
            if (!ref.upstream.empty()) {
                item.text += " \u2192 " + clean(ref.upstream);
            }
            item.target.hash = wxString::FromUTF8(ref.target);
            item.target.name = wxString::FromUTF8(ref.short_name);
            item.branch = ref.kind == repomancer::vcs::RefKind::LocalBranch;
            if (ref.is_head) {
                current_branch_ = ref.short_name;
            }
            group->items.push_back(std::move(item));
        }
        for (auto& group : groups) {
            if (!group.items.empty()) {
                section(group.title);
                for (auto& item : group.items) {
                    rows.push_back(std::move(item));
                }
            }
        }
    }

    if (people.ok() && !people.value().empty()) {
        section(_("Contributors"));
        for (const auto& person : people.value()) {
            Row row;
            row.text = "  " + clean(person.name) +
                       wxString::Format(wxPLURAL(" (%d commit)", " (%d commits)",
                                                 person.commits),
                                        person.commits);
            rows.push_back(std::move(row));
        }
    }

    if (langs.ok() && !langs.value().empty()) {
        section(_("Languages"));
        // The GitHub arrangement: one stacked bar of the shares, then a
        // legend of colour dots. The colours are the graph's own palette.
        Row bar;
        for (std::size_t i = 0; i < langs.value().size(); ++i) {
            bar.bar.emplace_back(repomancer::gui::lane_colour(static_cast<int>(i)),
                                 langs.value()[i].percent);
        }
        rows.push_back(std::move(bar));
        for (std::size_t i = 0; i < langs.value().size(); ++i) {
            const auto& language = langs.value()[i];
            const wxString share = language.percent < 0.05
                                       ? wxString("<0.1%")
                                       : wxString::Format("%.1f%%", language.percent);
            Row row;
            row.text = clean(language.name) + "  " + share;
            row.dot = true;
            row.colour = repomancer::gui::lane_colour(static_cast<int>(i));
            rows.push_back(std::move(row));
        }
    }

    if (rows.empty()) {
        Row row;
        row.text = _("No repository information");
        rows.push_back(std::move(row));
    }
    repo_view_->SetRows(std::move(rows));
}

void MainFrame::JumpToRef(const repomancer::gui::RepoView::Target& target) {
    if (target.kind == repomancer::gui::RepoView::Target::Kind::Worktree) {
        ShowWorktree();
        return;
    }
    const std::string hash(target.hash.utf8_str());
    const std::string name(target.name.utf8_str());
    const unsigned int count = model_->GetCount();
    for (unsigned int row = 0; row < count; ++row) {
        const auto* commit = model_->commit_at(row);
        if (commit == nullptr) {
            continue;
        }
        // A branch names its tip commit outright. An annotated tag names a
        // tag object the log has no row for, so its name is matched against
        // the row decorations instead.
        bool match = !hash.empty() && commit->hash == hash;
        std::string_view rest = commit->refs;
        while (!match && !name.empty() && !rest.empty()) {
            const std::size_t comma = rest.find(", ");
            std::string_view ref = rest.substr(0, comma);
            rest = comma == std::string_view::npos ? std::string_view{}
                                                   : rest.substr(comma + 2);
            constexpr std::string_view kHeadArrow = "HEAD -> ";
            constexpr std::string_view kTag = "tag: ";
            if (ref.substr(0, kHeadArrow.size()) == kHeadArrow) {
                ref = ref.substr(kHeadArrow.size());
            } else if (ref.substr(0, kTag.size()) == kTag) {
                ref = ref.substr(kTag.size());
            }
            match = ref == name;
        }
        if (match) {
            log_->Select(static_cast<int>(row));
            return;
        }
    }
}

void MainFrame::RestartToApplySettings() {
    // Where the toolkit cannot restyle windows that already exist, the way to
    // honour the choice is to start again with it: the theme is resolved when
    // a window is created, so a fresh process gets it right. The setting is
    // already on disk, and the open repository rides across on the command
    // line so the new window comes up where this one left off.
    //
    // Nothing here is unsaved yet. Once editing lands this has to run the
    // save prompts first, because Close(true) cannot be vetoed.
    Hide();

    // Each argument is a separate array element: wxExecute(char**) exec()s
    // them verbatim, so a repository path containing a quote, space or
    // backslash cannot re-tokenize into a different (already-trusted) path.
    const wxString exe = wxStandardPaths::Get().GetExecutablePath();
    const wxString repo = wxString::FromUTF8(repo_path_.string());
    std::vector<const wxChar*> args;
    args.push_back(exe.c_str());
    if (!repo_path_.empty()) {
        args.push_back(repo.c_str());
    }
    args.push_back(nullptr);
    wxExecute(const_cast<wxChar**>(args.data()), wxEXEC_ASYNC);
    Close(true);
}

void MainFrame::ApplyThemeToWidgets() {
    // Anything that reads a system colour once and keeps it has to be told to
    // read again: Scintilla styles are fixed at the time they are set, and
    // wxAUI's art provider computes its caption and border colours when it is
    // created. Everything else here follows the platform theme on its own.
    details_->StyleSetForeground(wxSTC_STYLE_DEFAULT,
                                 wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    details_->StyleSetBackground(wxSTC_STYLE_DEFAULT,
                                 wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX));
    details_->StyleClearAll();

    diff_->ApplyTheme();
    log_->InvalidateStrips();

    // Some controls are wx's own generic implementations on GTK, not
    // native ones, so they keep whatever colours they were given and do not
    // follow a theme change by themselves.
    const wxColour list_bg = wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX);
    const wxColour list_fg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    repo_panel_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
    details_panel_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
    log_panel_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
    files_panel_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
    diff_panel_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
    for (wxWindow* control : {static_cast<wxWindow*>(files_),
                              repo_view_->canvas(),
                              log_->canvas()}) {
        control->SetBackgroundColour(list_bg);
        control->SetForegroundColour(list_fg);
        control->Refresh();
    }

    // Panes are titled by their own header controls, so wxAUI's caption is
    // hidden; what remains to match is the surface it draws between and
    // behind them.
    auto* art = new wxAuiDefaultDockArt;
    aui_.SetArtProvider(art);

    const wxColour header_bg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
    art->SetMetric(wxAUI_DOCKART_GRADIENT_TYPE, wxAUI_GRADIENT_NONE);
    art->SetColour(wxAUI_DOCKART_BORDER_COLOUR, header_bg);
    art->SetColour(wxAUI_DOCKART_SASH_COLOUR, header_bg);
    art->SetColour(wxAUI_DOCKART_BACKGROUND_COLOUR, header_bg);
    // No pane frame: wxAUI draws it as a dotted rectangle around every docked
    // pane, which reads as stray dashes now that the panes are titled by their
    // own headers. The sash between panes is separation enough.
    art->SetMetric(wxAUI_DOCKART_PANE_BORDER_SIZE, 0);
    aui_.Update();

    Refresh();
}

void MainFrame::SetDetailsText(const wxString& text) {
    details_->SetReadOnly(false);
    details_->SetText(text);
    details_->SetReadOnly(true);
}


void MainFrame::OnFileSelected(wxDataViewEvent&) {
    const int row = files_->GetSelectedRow();
    if (row != wxNOT_FOUND) {
        ShowFileDiff(row);
    }
}

void MainFrame::ShowFileDiff(long index) {
    if (index < 0 || static_cast<std::size_t>(index) >= changed_files_.size() ||
        (selected_commit_.empty() && !worktree_mode_)) {
        return;
    }
    const auto& file = changed_files_[static_cast<std::size_t>(index)];

    const unsigned generation = ++diff_generation_;
    if (diff_worker_.joinable()) {
        diff_worker_.join();
    }
    diff_worker_ = std::thread([this, generation, repo = repo_path_,
                                commit = selected_commit_, path = file.path,
                                worktree = worktree_mode_, config = git_config_] {
        // Worktree files show their unstaged patch; when a file's changes
        // are fully staged that patch is empty, so the staged patch is shown
        // instead (and the hunk menu switches to Unstage).
        bool staged_view = false;
        auto diff = worktree ? GitDriver(config).worktree_diff(repo, path)
                             : GitDriver(config).file_diff(repo, commit, path);
        if (worktree && diff.ok() && diff.value().empty()) {
            auto staged = GitDriver(config).staged_diff(repo, path);
            if (staged.ok() && !staged.value().empty()) {
                diff = std::move(staged);
                staged_view = true;
            }
        }
        CallAfter([this, generation, staged_view, diff = std::move(diff)] {
            if (generation != diff_generation_.load()) {
                return;
            }
            diff_is_staged_ = staged_view;
            if (!diff.ok()) {
                diff_->ShowMessage(
                    wxString::Format(_("Could not read the diff: %s"),
                                     wxString::FromUTF8(diff.error().message)));
                return;
            }
            if (diff.value().empty()) {
                diff_->ShowMessage(worktree_mode_
                                       ? _("No diff — the file is untracked or binary.")
                                       : _("No textual changes."));
                return;
            }
            diff_->ShowDiff(std::move(diff).value(),
                            staged_view ? _("Staged changes (already in the index)")
                                        : wxString());
        });
    });
}

void MainFrame::OnGraphStyleSelected(wxCommandEvent& event) {
    const auto style = event.GetId() == ID_StyleAngular ? repomancer::gui::GraphStyle::Angular
                                                        : repomancer::gui::GraphStyle::Rounded;
    log_->SetStyle(style);

    auto settings = repomancer::load_settings();
    settings.graph_style = repomancer::gui::graph_style_to_string(style);
    repomancer::save_settings(settings);
}

void MainFrame::OnThemeSelected(wxCommandEvent& event) {
    ThemeMode mode = ThemeMode::System;
    if (event.GetId() == ID_ThemeLight) {
        mode = ThemeMode::Light;
    } else if (event.GetId() == ID_ThemeDark) {
        mode = ThemeMode::Dark;
    }

    const bool applied_live = repomancer::gui::apply_theme(mode);
    if (applied_live) {
        // Deferred: the toolkit applies the new palette on its next pass, so
        // reading system colours right here would hand back the old ones and
        // leave everything that caches them a theme behind.
        CallAfter([this] {
            ApplyThemeToWidgets();
            // The graph reads its casing colour per render, so the rows have
            // to be re-rendered for the new highlight to take effect.
        });
    }

    auto settings = repomancer::load_settings();
    settings.theme = repomancer::gui::theme_mode_to_string(mode);
    repomancer::save_settings(settings);

    if (!applied_live) {
        RestartToApplySettings();
    }
}

MainFrame::~MainFrame() {
    if (worker_.joinable()) {
        worker_.join();
    }
    if (files_worker_.joinable()) {
        files_worker_.join();
    }
    if (diff_worker_.joinable()) {
        diff_worker_.join();
    }
    if (op_worker_.joinable()) {
        op_worker_.join();
    }
    aui_.UnInit();
}

void MainFrame::OnPreferences(wxCommandEvent&) {
    const auto before = repomancer::load_settings();
    repomancer::gui::PreferencesDialog dialog(this, before);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    const auto settings = dialog.Result();
    const bool titlebar_changed =
        settings.integrated_titlebar != before.integrated_titlebar ||
        settings.topbar_buttons != before.topbar_buttons ||
        settings.topbar_sharp_corners != before.topbar_sharp_corners;
    repomancer::save_settings(settings);
    git_config_.binary = std::filesystem::path(settings.git_binary);
    if (titlebar_changed) {
        // The frame's chrome is chosen at creation; a fresh process applies
        // it, the same way the theme restart works.
        RestartToApplySettings();
        return;
    }
    SetStatusText(wxString::Format(_("Git binary: %s — takes effect on the next operation"),
                                   wxString::FromUTF8(settings.git_binary)));
}

void MainFrame::ShowFileHistory(const std::string& path) {
    SetStatusText(wxString::Format(_("Reading history of %s…"),
                                   wxString::FromUTF8(path)));
    RunGitOp(
        _("Could not read the file's history: "),
        [path](const std::filesystem::path& repo,
               const repomancer::vcs::git::GitConfig& config) {
            LogOptions options;
            options.all_refs = false;
            options.path = path;
            return GitDriver(config).log(repo, options);
        },
        [this, path](std::vector<repomancer::vcs::Commit> commits) {
            SetStatusText(wxString::Format(
                wxPLURAL("%zu commit touch %s", "%zu commits touch %s",
                         commits.size()),
                commits.size(), wxString::FromUTF8(path)));
            repomancer::gui::FileHistoryDialog dialog(
                this, wxString::FromUTF8(path), std::move(commits));
            if (dialog.ShowModal() == wxID_OK && !dialog.SelectedHash().empty()) {
                // The main log shows every ref, so the commit is normally
                // there; jumping reuses the sidebar's hash lookup.
                repomancer::gui::RepoView::Target target;
                target.hash = wxString::FromUTF8(dialog.SelectedHash());
                JumpToRef(target);
            }
        });
}

void MainFrame::ShowFileBlame(const std::string& path) {
    SetStatusText(wxString::Format(_("Reading blame of %s…"), wxString::FromUTF8(path)));
    RunGitOp(
        _("Could not read blame: "),
        [path](const std::filesystem::path& repo,
               const repomancer::vcs::git::GitConfig& config) {
            return GitDriver(config).blame(repo, path);
        },
        [this, path](std::vector<repomancer::vcs::BlameLine> lines) {
            SetStatusText(wxString::Format(
                wxPLURAL("%zu line attributed — %s", "%zu lines attributed — %s",
                         lines.size()),
                lines.size(), wxString::FromUTF8(path)));
            repomancer::gui::BlameDialog dialog(this, wxString::FromUTF8(path),
                                                std::move(lines));
            if (dialog.ShowModal() == wxID_OK && !dialog.SelectedHash().empty()) {
                repomancer::gui::RepoView::Target target;
                target.hash = wxString::FromUTF8(dialog.SelectedHash());
                JumpToRef(target);
            }
        });
}

void MainFrame::OpenChangedFile(const std::string& path) {
    const auto file = repo_path_ / path;
    std::error_code ec;
    if (!std::filesystem::exists(file, ec) || ec) {
        // Deleted in this commit, or never in the working tree.
        SetStatusText(wxString::Format(_("%s does not exist in the working tree"),
                                       wxString::FromUTF8(file.string())));
        return;
    }
    if (!wxLaunchDefaultApplication(wxString::FromUTF8(file.string()))) {
        SetStatusText(wxString::Format(_("No application could open %s"),
                                       wxString::FromUTF8(file.string())));
    }
}

void MainFrame::OpenContainingFolder(const std::string& path) {
    auto folder = (repo_path_ / path).parent_path();
    std::error_code ec;
    if (!std::filesystem::exists(folder, ec) || ec) {
        // The directory may be gone with its contents; the repository root
        // always exists.
        folder = repo_path_;
    }
    if (!wxLaunchDefaultApplication(wxString::FromUTF8(folder.string()))) {
        SetStatusText(wxString::Format(_("No application could open %s"),
                                       wxString::FromUTF8(folder.string())));
    }
}

void MainFrame::RebuildRecentMenu() {
    if (recent_menu_ == nullptr) {
        return;
    }
    while (recent_menu_->GetMenuItemCount() > 0) {
        recent_menu_->Destroy(recent_menu_->FindItemByPosition(0));
    }
    if (recent_repos_.empty()) {
        recent_menu_->Append(ID_Recent1, _("(empty)"))->Enable(false);
        return;
    }
    int id = ID_Recent1;
    for (const auto& path : recent_repos_) {
        recent_menu_->Append(id++, wxString::FromUTF8(path));
    }
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

void MainFrame::ComputeReadOnlyOverrides(const std::filesystem::path& repo) {
    // Read the untrusted repo's own filter/diff drivers so make_spec can
    // neutralize them by name. A quick config read on the UI thread, like
    // the trust decision itself; failure just leaves the fixed set. Uses the
    // repo being opened — repo_path_ is not assigned until later.
    auto cfg = git_config_;
    cfg.extra_neutralize.clear();
    auto overrides = GitDriver(cfg).read_only_overrides(repo);
    git_config_.extra_neutralize =
        overrides.ok() ? std::move(overrides).value() : std::vector<std::string>{};
}

void MainFrame::LoadRepository(const wxString& path) {
    // One load at a time — and busy_ goes up BEFORE the trust dialog, whose
    // nested event loop would otherwise let queued completions start a
    // second load underneath it.
    if (busy_.exchange(true)) {
        wxBell();
        return;
    }
    // §13.1 trust gate — decided BEFORE the first git subprocess touches
    // the repository, because a hostile repo's config can name programs to
    // execute (fsmonitor, hooks, filters) for even read commands.
    {
        std::error_code ec;
        const auto canonical = std::filesystem::weakly_canonical(
            std::filesystem::path(std::string(path.utf8_str())), ec);
        const std::string key =
            ec ? std::string(path.utf8_str()) : canonical.string();
        auto settings = repomancer::load_settings();
        if (repomancer::is_repo_trusted(settings, key)) {
            git_config_.trust = repomancer::vcs::git::RepoTrust::Trusted;
            git_config_.extra_neutralize.clear();
        } else if (session_read_only_.count(key) != 0) {
            // Already answered "Open Read-Only" this session; re-prompting
            // on every refresh would only train the user to click through.
            git_config_.trust = repomancer::vcs::git::RepoTrust::ReadOnly;
            ComputeReadOnlyOverrides(
                std::filesystem::path(std::string(path.utf8_str())));
        } else {
            wxMessageDialog ask(
                this,
                wxString::Format(
                    _("Do you trust the authors of this repository?\n\n%s\n\n"
                      "A repository's own configuration can instruct git to run "
                      "programs. Until you trust it, Repomancer opens it "
                      "read-only with that configuration neutralized."),
                    repomancer::gui::sanitized_utf8(key)),
                _("Open Repository"),
                wxYES_NO | wxCANCEL | wxNO_DEFAULT | wxICON_AUTH_NEEDED);
            ask.SetYesNoCancelLabels(_("&Trust and Open"), _("Open &Read-Only"),
                                     _("Cancel"));
            switch (ask.ShowModal()) {
            case wxID_YES:
                remember_trusted_repo(settings, key);
                repomancer::save_settings(settings);
                git_config_.trust = repomancer::vcs::git::RepoTrust::Trusted;
                break;
            case wxID_NO:
                session_read_only_.insert(key);
                git_config_.trust = repomancer::vcs::git::RepoTrust::ReadOnly;
                ComputeReadOnlyOverrides(
                    std::filesystem::path(std::string(path.utf8_str())));
                break;
            default:
                busy_.store(false);
                return; // neither trust nor curiosity today
            }
        }
    }
    repo_read_only_ =
        git_config_.trust == repomancer::vcs::git::RepoTrust::ReadOnly;
    SetStatusText(wxString::Format(_("Loading %s…"), path));
    if (worker_.joinable()) {
        worker_.join();
    }

    const std::string path_utf8(path.utf8_str());
    repo_path_ = std::filesystem::path(path_utf8);
    worker_ = std::thread([this, path_utf8, config = git_config_] {
        GitDriver driver(config);
        LogOptions options;
        options.max_count = 2000;
        const std::filesystem::path repo(path_utf8);
        auto result = driver.log(repo, options);
        // The sidebar's data rides in the same worker: more git subprocesses
        // that must not run on the UI thread.
        auto status = driver.status(repo);
        auto refs = driver.refs(repo);
        auto people = driver.contributors(repo);
        auto langs = driver.languages(repo);

        CallAfter([this, path_utf8, result = std::move(result), status = std::move(status),
                   refs = std::move(refs), people = std::move(people),
                   langs = std::move(langs)]() mutable {
            busy_.store(false);
            if (!result.ok()) {
                SetStatusText(wxString::Format(
                    _("Failed to read repository: %s"),
                    wxString::FromUTF8(result.error().message)));
                return;
            }
            const auto count = result.value().size();
            model_->ReplaceAll(std::move(result).value());
            log_->ModelChanged();
            SetDetailsText(wxEmptyString);
            files_->DeleteAllItems();
            changed_files_.clear();
            diff_->Clear();

            wxString loaded = wxString::Format(_("%zu commits — %s"), count,
                                               wxString::FromUTF8(path_utf8));
            if (repo_read_only_) {
                loaded << _("  —  READ-ONLY (untrusted; Repository ▸ Trust This "
                            "Repository to enable changes)");
            }
            SetStatusText(loaded);

            // Land on the newest commit so the detail panes have content.
            if (count > 0) {
                log_->Select(0);
            }

            // The log is the page's centre; let it paint before the sidebar
            // and the recents bookkeeping run.
            CallAfter([this, path_utf8, status = std::move(status),
                       refs = std::move(refs), people = std::move(people),
                       langs = std::move(langs)] {
                PopulateRepoDetails(status, refs, people, langs);
                if (recent_repos_.empty() || recent_repos_.front() != path_utf8) {
                    auto remembered = repomancer::load_settings();
                    repomancer::remember_recent_repo(remembered, path_utf8);
                    repomancer::save_settings(remembered);
                    recent_repos_ = remembered.recent_repos;
                    RebuildRecentMenu();
                }
            });
        });
    });
}
