// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "main_frame.h"

#include "icons.h"

#include <wx/dcclient.h>


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
    // Bitmaps go on before Append: wxGTK ignores a bitmap set afterwards.
    auto* file_menu = new wxMenu;
    auto* open_item = new wxMenuItem(file_menu, wxID_OPEN, _("&Open Repository…\tCtrl-O"),
                                     _("Open a local git repository"));
    open_item->SetBitmap(repomancer::gui::icons::menu_icon(repomancer::gui::icons::kFolderGit));
    file_menu->Append(open_item);
    file_menu->AppendSeparator();
    auto* quit_item = new wxMenuItem(file_menu, wxID_EXIT);
    quit_item->SetBitmap(repomancer::gui::icons::menu_icon(repomancer::gui::icons::kExit));
    file_menu->Append(quit_item);

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

    // Wrapped, not clipped: a long subject or a wide body should reflow
    // rather than force horizontal scrolling, and the text needs room to
    // breathe away from the pane border.
    // Each pane is titled by a real header control sitting above its content,
    // the same kind the log uses for its columns, so both read as one sort of
    // header. wxAUI's own caption is switched off for these panes.
    const auto titled = [](wxPanel* panel, const wxString& title, wxWindow* content) {
        auto* header = new repomancer::gui::PaneHeader(panel, title);
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
    files_ = new wxListCtrl(files_table, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                            wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
    // wxListCtrl has no internal margin, so a narrow blank column stands in
    // for one and keeps the first label off the border.
    files_->AppendColumn(wxEmptyString, wxLIST_FORMAT_LEFT, 8);
    files_->AppendColumn(_("Change"), wxLIST_FORMAT_LEFT, 90);
    files_->AppendColumn(_("File"), wxLIST_FORMAT_LEFT, 320);
    // A stretch filler after the last column, so the header band spans the
    // pane instead of exposing the list's white body beyond "File".
    files_->AppendColumn(wxEmptyString, wxLIST_FORMAT_LEFT, 0);
    files_->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        event.Skip();
        const int used = files_->GetColumnWidth(0) + files_->GetColumnWidth(1) +
                         files_->GetColumnWidth(2);
        files_->SetColumnWidth(3, std::max(0, files_->GetClientSize().GetWidth() - used));
    });
    titled(files_table, _("Changed files"), files_);
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
            const wxColour fg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
            const wxColour bg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
            const auto blend = [](int a, int b) { return (a * 30 + b * 70) / 100; };
            dc.SetPen(wxPen(wxColour(blend(fg.Red(), bg.Red()), blend(fg.Green(), bg.Green()),
                                     blend(fg.Blue(), bg.Blue())),
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
    Bind(wxEVT_MENU, &MainFrame::OnQuit, this, wxID_EXIT);
    Bind(wxEVT_MENU, &MainFrame::OnAbout, this, wxID_ABOUT);
    files_->Bind(wxEVT_LIST_ITEM_SELECTED, &MainFrame::OnFileSelected, this);
}


void MainFrame::ShowCommit(int row) {
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
                                 commit = selected_commit_] {
        auto files = GitDriver().changed_files(repo, commit);
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
            long list_row = 0;
            for (const auto& file : changed_files_) {
                const auto name = repomancer::vcs::file_change_name(file.change);
                files_->InsertItem(list_row, wxEmptyString);
                files_->SetItem(list_row, 1, wxString::FromUTF8(std::string(name)));
                wxString path = wxString::FromUTF8(file.path);
                if (!file.old_path.empty()) {
                    path = wxString::FromUTF8(file.old_path) + " → " + path;
                }
                files_->SetItem(list_row, 2, path);
                ++list_row;
            }
            if (!changed_files_.empty()) {
                files_->SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
            }
        });
    });
}

void MainFrame::PopulateRepoDetails(
    const repomancer::vcs::VcsResult<std::vector<repomancer::vcs::Ref>>& refs,
    const repomancer::vcs::VcsResult<std::vector<repomancer::vcs::Contributor>>& people,
    const repomancer::vcs::VcsResult<std::vector<repomancer::vcs::LanguageStat>>& langs) {
    using Row = repomancer::gui::RepoView::Row;
    using Target = repomancer::gui::RepoView::Target;
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

    std::vector<Row> rows;
    const auto section = [&rows](const wxString& title) {
        if (!rows.empty()) {
            rows.push_back({});
        }
        Row heading;
        heading.text = title;
        heading.heading = true;
        rows.push_back(std::move(heading));
    };

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
            item.target = Target{wxString::FromUTF8(ref.target),
                                 wxString::FromUTF8(ref.short_name)};
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
    log_->InvalidateStrips();

    // wxListCtrl and wxTreeCtrl are wx's own generic controls on GTK, not
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


void MainFrame::OnFileSelected(wxListEvent& event) { ShowFileDiff(event.GetIndex()); }

void MainFrame::ShowFileDiff(long index) {
    if (index < 0 || static_cast<std::size_t>(index) >= changed_files_.size() ||
        selected_commit_.empty()) {
        return;
    }
    const auto& file = changed_files_[static_cast<std::size_t>(index)];

    const unsigned generation = ++diff_generation_;
    if (diff_worker_.joinable()) {
        diff_worker_.join();
    }
    diff_worker_ = std::thread([this, generation, repo = repo_path_,
                                commit = selected_commit_, path = file.path] {
        auto diff = GitDriver().file_diff(repo, commit, path);
        CallAfter([this, generation, diff = std::move(diff)] {
            if (generation != diff_generation_.load()) {
                return;
            }
            if (!diff.ok()) {
                diff_->ShowMessage(
                    wxString::Format(_("Could not read the diff: %s"),
                                     wxString::FromUTF8(diff.error().message)));
                return;
            }
            if (diff.value().empty()) {
                diff_->ShowMessage(_("No textual changes."));
                return;
            }
            diff_->ShowDiff(diff.value());
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
        RestartForTheme();
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
        const std::filesystem::path repo(path_utf8);
        auto result = driver.log(repo, options);
        // The sidebar's data rides in the same worker: three more git
        // subprocesses that must not run on the UI thread.
        auto refs = driver.refs(repo);
        auto people = driver.contributors(repo);
        auto langs = driver.languages(repo);

        CallAfter([this, path_utf8, result = std::move(result), refs = std::move(refs),
                   people = std::move(people), langs = std::move(langs)]() mutable {
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

            SetStatusText(wxString::Format(_("%zu commits — %s"), count,
                                           wxString::FromUTF8(path_utf8)));

            PopulateRepoDetails(refs, people, langs);

            // Land on the newest commit so the detail panes have content.
            if (count > 0) {
                log_->Select(0);
            }
        });
    });
}
