// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "main_frame.h"

#include "pane_header.h"
#include "theme.h"

#include <repomancer/settings.h>
#include <repomancer/vcs/git/git_driver.h>

#include <wx/aboutdlg.h>
#include <wx/dirdlg.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/aui/dockart.h>
#include <wx/settings.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <wx/sizer.h>

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
    ID_StyleRounded,
    ID_StyleAngular,
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
    auto* style_menu = new wxMenu;
    style_menu->AppendRadioItem(ID_StyleAngular, _("&Angular"));
    style_menu->AppendRadioItem(ID_StyleRounded, _("&Rounded"));

    auto* view_menu = new wxMenu;
    view_menu->AppendSubMenu(theme_menu, _("&Theme"));
    view_menu->AppendSubMenu(style_menu, _("&Graph style"));

    auto* help_menu = new wxMenu;
    help_menu->Append(wxID_ABOUT);

    auto* menu_bar = new wxMenuBar;
    menu_bar->Append(file_menu, _("&File"));
    menu_bar->Append(view_menu, _("&View"));
    menu_bar->Append(help_menu, _("&Help"));
    SetMenuBar(menu_bar);

    const auto startup_settings = repomancer::load_settings();
    const auto startup_style =
        repomancer::gui::graph_style_from_string(startup_settings.graph_style);
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

    log_view_ = new wxDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                   wxDV_ROW_LINES);
    model_ = wxObjectDataPtr<CommitLogModel>(new CommitLogModel);
    log_view_->AssociateModel(model_.get());

    // Fixed pitch keeps the graph lanes continuous from row to row.
    log_view_->SetRowHeight(repomancer::gui::GraphRenderer::kRowHeight);

    graph_renderer_ = new repomancer::gui::GraphRenderer(&model_->graph_rows());
    graph_renderer_->SetStyle(startup_style);
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

    // Wrapped, not clipped: a long subject or a wide body should reflow
    // rather than force horizontal scrolling, and the text needs room to
    // breathe away from the pane border.
    // Each pane is titled by a real header control sitting above its content,
    // the same kind the log uses for its columns, so both read as one sort of
    // header. wxAUI's own caption is switched off for these panes.
    const auto titled = [this](wxPanel* panel, const wxString& title, wxWindow* content) {
        auto* header = new repomancer::gui::PaneHeader(panel, title);
        pane_headers_.push_back(header);
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(header, 0, wxEXPAND);
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
    auto* details_panel = new wxPanel(this);
    details_ = new wxStyledTextCtrl(details_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
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
    titled(details_panel, _("Commit details"), details_);

    auto* files_panel = new wxPanel(this);
    files_ = new wxListCtrl(files_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                            wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
    // wxListCtrl has no internal margin, so a narrow blank column stands in
    // for one and keeps the first label off the border.
    files_->AppendColumn(wxEmptyString, wxLIST_FORMAT_LEFT, 8);
    files_->AppendColumn(_("Change"), wxLIST_FORMAT_LEFT, 90);
    files_->AppendColumn(_("File"), wxLIST_FORMAT_LEFT, 320);
    titled(files_panel, _("Changed files"), files_);

    auto* diff_panel = new wxPanel(this);
    diff_ = new repomancer::gui::DiffView(diff_panel);
    titled(diff_panel, _("Diff"), diff_);

    // The sidebar is itself a one-column, one-row table: its column header is
    // the pane title, and the row's only cell carries the repository details.
    repo_view_ = new repomancer::gui::RepoView(this);

    // GitX-style master/detail: history on top, and below it the commit's
    // metadata, the files it touched, and the patch for whichever is selected.
    aui_.SetManagedWindow(this);
    aui_.AddPane(log_view_, wxAuiPaneInfo().CenterPane().Name("log"));
    aui_.AddPane(repo_view_, wxAuiPaneInfo()
                                 .Left()
                                 .Name("refs")
                                 .Caption(_("Repository"))
                                 .BestSize(230, -1)
                                 .MinSize(160, -1)
                                 .CloseButton(false)
                                 .CaptionVisible(false));
    aui_.AddPane(details_panel, wxAuiPaneInfo()
                               .Bottom()
                               .Name("details")
                               .Caption(_("Commit details"))
                               .BestSize(360, 260)
                               .CloseButton(false)
                                 .CaptionVisible(false));
    aui_.AddPane(files_panel, wxAuiPaneInfo()
                             .Bottom()
                             .Name("files")
                             .Caption(_("Changed files"))
                             .BestSize(360, 260)
                             .CloseButton(false)
                                 .CaptionVisible(false)
                             .Position(1));
    aui_.AddPane(diff_panel, wxAuiPaneInfo()
                            .Bottom()
                            .Name("diff")
                            .Caption(_("Diff"))
                            .BestSize(520, 260)
                            .CloseButton(false)
                                 .CaptionVisible(false)
                            .Position(2));
    aui_.Update();
    ApplyThemeToWidgets();

    Bind(wxEVT_MENU, &MainFrame::OnOpenRepository, this, wxID_OPEN);
    Bind(wxEVT_MENU, &MainFrame::OnThemeSelected, this, ID_ThemeSystem, ID_ThemeDark);
    Bind(wxEVT_MENU, &MainFrame::OnGraphStyleSelected, this, ID_StyleRounded, ID_StyleAngular);
    Bind(wxEVT_MENU, &MainFrame::OnQuit, this, wxID_EXIT);
    Bind(wxEVT_MENU, &MainFrame::OnAbout, this, wxID_ABOUT);
    log_view_->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &MainFrame::OnCommitSelected, this);
    files_->Bind(wxEVT_LIST_ITEM_SELECTED, &MainFrame::OnFileSelected, this);
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
        SetDetailsText(wxEmptyString);
        files_->DeleteAllItems();
        changed_files_.clear();
        selected_commit_.clear();
        diff_->Clear();
        return;
    }
    selected_commit_ = commit->hash;

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
    SetDetailsText(text);

    // The file list is small enough to fetch inline; the patch for a selected
    // file is fetched on demand.
    files_->DeleteAllItems();
    changed_files_.clear();
    diff_->ShowMessage(_("Select a file to see its changes."));

    auto files = GitDriver().changed_files(repo_path_, selected_commit_);
    if (!files.ok()) {
        diff_->ShowMessage(wxString::Format(_("Could not list changed files: %s"),
                                            wxString::FromUTF8(files.error().message)));
        return;
    }
    changed_files_ = std::move(files).value();
    long row = 0;
    for (const auto& file : changed_files_) {
        const auto name = repomancer::vcs::file_change_name(file.change);
        files_->InsertItem(row, wxEmptyString);
        files_->SetItem(row, 1, wxString::FromUTF8(std::string(name)));
        wxString path = wxString::FromUTF8(file.path);
        if (!file.old_path.empty()) {
            path = wxString::FromUTF8(file.old_path) + " → " + path;
        }
        files_->SetItem(row, 2, path);
        ++row;
    }
    if (!changed_files_.empty()) {
        files_->SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    }
}

void MainFrame::PopulateRepoDetails() {
    // Attacker-influenced strings (branch names, identities) are stripped of
    // control characters before display, as everywhere else in the UI.
    const auto clean = [](const std::string& raw) {
        std::string text;
        text.reserve(raw.size());
        for (const char ch : raw) {
            text.push_back(static_cast<unsigned char>(ch) < 0x20 ? ' ' : ch);
        }
        return wxString::FromUTF8(text);
    };

    wxString details;
    const auto section = [&details](const wxString& title) {
        if (!details.empty()) {
            details += "\n";
        }
        details += title + "\n";
    };

    const auto refs = GitDriver().refs(repo_path_);
    if (refs.ok()) {
        struct Group {
            repomancer::vcs::RefKind kind;
            wxString title;
            wxString body;
        };
        Group groups[] = {
            {repomancer::vcs::RefKind::LocalBranch, _("Branches"), {}},
            {repomancer::vcs::RefKind::RemoteBranch, _("Remotes"), {}},
            {repomancer::vcs::RefKind::Tag, _("Tags"), {}},
            {repomancer::vcs::RefKind::Stash, _("Stashes"), {}},
        };
        for (const auto& ref : refs.value()) {
            auto* group = std::find_if(std::begin(groups), std::end(groups),
                                       [&](const Group& g) { return g.kind == ref.kind; });
            if (group == std::end(groups)) {
                continue;
            }
            group->body += "  " + clean(ref.short_name);
            if (ref.is_head) {
                group->body += " \u2713";
            }
            if (!ref.upstream.empty()) {
                group->body += " \u2192 " + clean(ref.upstream);
            }
            group->body += "\n";
        }
        for (const auto& group : groups) {
            if (!group.body.empty()) {
                section(group.title);
                details += group.body;
            }
        }
    }

    const auto people = GitDriver().contributors(repo_path_);
    if (people.ok() && !people.value().empty()) {
        section(_("Contributors"));
        for (const auto& person : people.value()) {
            details += "  " + clean(person.name) +
                       wxString::Format(wxPLURAL(" (%d commit)", " (%d commits)",
                                                 person.commits),
                                        person.commits) +
                       "\n";
        }
    }

    const auto langs = GitDriver().languages(repo_path_);
    if (langs.ok() && !langs.value().empty()) {
        section(_("Languages"));
        for (const auto& language : langs.value()) {
            const wxString share = language.percent < 0.05
                                       ? wxString("<0.1%")
                                       : wxString::Format("%.1f%%", language.percent);
            details += "  " + clean(language.name) + "  " + share + "\n";
        }
    }

    if (details.empty()) {
        details = _("No repository information");
    }
    repo_view_->SetDetails(details);
}

void MainFrame::RestartForTheme() {
    // Where the toolkit cannot restyle windows that already exist, the way to
    // honour the choice is to start again with it: the theme is resolved when
    // a window is created, so a fresh process gets it right. The setting is
    // already on disk, and the open repository rides across on the command
    // line so the new window comes up where this one left off.
    //
    // Nothing here is unsaved yet. Once editing lands this has to run the
    // save prompts first, because Close(true) cannot be vetoed.
    Hide();

    wxString command = "\"" + wxStandardPaths::Get().GetExecutablePath() + "\"";
    if (!repo_path_.empty()) {
        command += " \"" + wxString::FromUTF8(repo_path_.string()) + "\"";
    }
    wxExecute(command, wxEXEC_ASYNC);
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

    // wxListCtrl and wxTreeCtrl are wx's own generic controls on GTK, not
    // native ones, so they keep whatever colours they were given and do not
    // follow a theme change by themselves.
    const wxColour list_bg = wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX);
    const wxColour list_fg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    for (wxWindow* control : {static_cast<wxWindow*>(files_),
                              static_cast<wxWindow*>(repo_view_),
                              static_cast<wxWindow*>(log_view_)}) {
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

void MainFrame::RefreshLogRows() {
    // wxGTK backs wxDataViewCtrl with a native GtkTreeView, which repaints a
    // cell only when the model says the row changed — Refresh() alone leaves
    // the old rendering on screen. Resetting re-queries every row; the
    // selection is restored by index because a virtual list model addresses
    // items that way.
    const wxDataViewItem selected = log_view_->GetSelection();
    const unsigned int selected_row = selected.IsOk() ? model_->GetRow(selected) : 0;
    const bool had_selection = selected.IsOk();

    model_->Reset(model_->GetCount());

    if (had_selection && selected_row < model_->GetCount()) {
        const wxDataViewItem item = model_->GetItem(selected_row);
        log_view_->Select(item);
        log_view_->EnsureVisible(item);
    }
}

void MainFrame::OnFileSelected(wxListEvent& event) { ShowFileDiff(event.GetIndex()); }

void MainFrame::ShowFileDiff(long index) {
    if (index < 0 || static_cast<std::size_t>(index) >= changed_files_.size() ||
        selected_commit_.empty()) {
        return;
    }
    const auto& file = changed_files_[static_cast<std::size_t>(index)];
    const auto diff = GitDriver().file_diff(repo_path_, selected_commit_, file.path);
    if (!diff.ok()) {
        diff_->ShowMessage(wxString::Format(_("Could not read the diff: %s"),
                                            wxString::FromUTF8(diff.error().message)));
        return;
    }
    if (diff.value().empty()) {
        diff_->ShowMessage(_("No textual changes."));
        return;
    }
    diff_->ShowDiff(diff.value());
}

void MainFrame::OnGraphStyleSelected(wxCommandEvent& event) {
    const auto style = event.GetId() == ID_StyleAngular ? repomancer::gui::GraphStyle::Angular
                                                        : repomancer::gui::GraphStyle::Rounded;
    graph_renderer_->SetStyle(style);
    RefreshLogRows();

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
            RefreshLogRows();
        });
    }

    auto settings = repomancer::load_settings();
    settings.theme = repomancer::gui::theme_mode_to_string(mode);
    repomancer::save_settings(settings);

    if (!applied_live) {
        RestartForTheme();
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
    repo_path_ = std::filesystem::path(path_utf8);
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
            SetDetailsText(wxEmptyString);
            files_->DeleteAllItems();
            changed_files_.clear();
            diff_->Clear();

            // Size the graph column to the widest row of this history, then
            // fit every other column to what it actually holds.
            graph_renderer_->SetMaxLanes(model_->max_lanes());
            FitColumns();

            SetStatusText(wxString::Format(_("%zu commits — %s"), count,
                                           wxString::FromUTF8(path_utf8)));

            PopulateRepoDetails();

            // The log's column header is the reference the pane headers line
            // up with. Its height is not exposed, but the first row's position
            // in client coordinates is exactly where that header ends.
            if (model_->GetCount() > 0) {
                const wxRect row = log_view_->GetItemRect(model_->GetItem(0));
                for (auto* header : pane_headers_) {
                    header->SetHeight(row.GetTop());
                }
            }

            // Land on the newest commit so the detail panes have content.
            if (count > 0) {
                const wxDataViewItem first = model_->GetItem(0);
                log_view_->Select(first);
                log_view_->EnsureVisible(first);
                wxDataViewEvent selected(wxEVT_DATAVIEW_SELECTION_CHANGED, log_view_,
                                         graph_column_, first);
                OnCommitSelected(selected);
            }
        });
    });
}
