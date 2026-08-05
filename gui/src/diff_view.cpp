// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "diff_view.h"

#include <wx/settings.h>

namespace repomancer::gui {

namespace {

// Style ids beyond Scintilla's own reserved range.
enum DiffStyle {
    Style_Default = 0,
    Style_Added,
    Style_Removed,
    Style_HunkHeader,
    Style_FileHeader,
    Style_Meta,
};

// Open Color, matched to the lane palette (see graph_renderer.cpp). The tinted
// backgrounds use the lightest steps so text stays readable on them.
const wxColour kAddedFg(0x2B, 0x8A, 0x3E);  // green-9
const wxColour kAddedBg(0xEB, 0xFB, 0xEE);  // green-0
const wxColour kRemovedFg(0xC9, 0x2A, 0x2A); // red-9
const wxColour kRemovedBg(0xFF, 0xF5, 0xF5); // red-0
const wxColour kHunkFg(0x18, 0x64, 0xAB);    // blue-9
const wxColour kHunkBg(0xE7, 0xF5, 0xFF);    // blue-0
const wxColour kMetaFg(0x86, 0x8E, 0x96);    // gray-6

// Dark equivalents: the light tints above would glare, so these are the
// accent hues dropped to roughly the background's luminance — enough to read
// a block of additions at a glance without lighting up the pane.
const wxColour kAddedFgDark(0x8C, 0xE9, 0x9A);   // green-3
const wxColour kAddedBgDark(0x1A, 0x2E, 0x20);
const wxColour kRemovedFgDark(0xFF, 0xA8, 0xA8); // red-3
const wxColour kRemovedBgDark(0x38, 0x20, 0x20);
const wxColour kHunkFgDark(0x74, 0xC0, 0xFC);    // blue-3
const wxColour kHunkBgDark(0x1E, 0x29, 0x3B);
const wxColour kMetaFgDark(0xAD, 0xB5, 0xBD);    // gray-5

bool is_dark(const wxColour& background) {
    // Rec. 601 luma; good enough to pick a legible pairing.
    return (background.Red() * 299 + background.Green() * 587 + background.Blue() * 114) / 1000 <
           128;
}

} // namespace

DiffView::DiffView(wxWindow* parent, wxWindowID id)
    // No frame of its own: the pane already has a header above it, and the
    // control's border reads as a stray line under that header.
    : wxStyledTextCtrl(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE) {
    SetReadOnly(true);
    SetWrapMode(wxSTC_WRAP_NONE);
    SetViewEOL(false);
    SetIndentationGuides(wxSTC_IV_NONE);
    // A diff is not source: Scintilla must not reflow or re-indent anything.
    SetLexer(wxSTC_LEX_NULL);
    SetTabWidth(4);
    SetUseTabs(false);

    SetMarginType(0, wxSTC_MARGIN_NUMBER);
    SetMarginWidth(0, 0); // patch lines carry their own numbers
    SetMarginWidth(1, 0);
    SetMarginWidth(2, 0);
    // Blank space Scintilla keeps between its border and the text itself, so
    // the first character is not pressed against the frame. It has no top
    // margin, and one uniform line height for the document, so the space above
    // the first line can only come from extra ascent — which applies to every
    // line and doubles as leading.
    SetMarginLeft(10);
    SetMarginRight(10);
    SetExtraAscent(7);
    SetExtraDescent(1);

    ApplyTheme();

    // Scintilla eats plain mouse events; the context-menu event is the one
    // reliable right-click signal on all three platforms.
    Bind(wxEVT_CONTEXT_MENU, [this](wxContextMenuEvent& event) {
        if (!on_hunk_menu_ || line_hunks_.empty()) {
            event.Skip();
            return;
        }
        const wxPoint screen = event.GetPosition();
        long position = 0;
        if (screen == wxDefaultPosition) {
            position = GetCurrentPos(); // keyboard menu key
        } else {
            const wxPoint client = ScreenToClient(screen);
            position = PositionFromPoint(wxPoint(client.x, client.y));
        }
        const long line = LineFromPosition(position);
        if (line < 0 || static_cast<std::size_t>(line) >= line_hunks_.size()) {
            event.Skip();
            return;
        }
        const auto [file, hunk] = line_hunks_[static_cast<std::size_t>(line)];
        if (file < 0 || hunk < 0) {
            event.Skip();
            return;
        }
        on_hunk_menu_(files_[static_cast<std::size_t>(file)],
                      static_cast<std::size_t>(hunk));
    });
}

void DiffView::SetOnHunkMenu(
    std::function<void(const repomancer::vcs::FileDiff&, std::size_t)> handler) {
    on_hunk_menu_ = std::move(handler);
}

void DiffView::ApplyTheme() {
    const wxColour window_bg = wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX);
    const wxColour window_fg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    const bool dark = is_dark(window_bg);

    wxFont mono(wxFontInfo().Family(wxFONTFAMILY_TELETYPE));
    StyleSetFont(wxSTC_STYLE_DEFAULT, mono);
    StyleSetForeground(wxSTC_STYLE_DEFAULT, window_fg);
    StyleSetBackground(wxSTC_STYLE_DEFAULT, window_bg);
    StyleClearAll(); // propagate the default before setting the specific ones

    // Both themes tint the line background: on dark the tint is dim rather
    // than absent, so a block of additions still reads as a block instead of
    // relying on the text colour alone.
    StyleSetForeground(Style_Added, dark ? kAddedFgDark : kAddedFg);
    StyleSetBackground(Style_Added, dark ? kAddedBgDark : kAddedBg);
    StyleSetForeground(Style_Removed, dark ? kRemovedFgDark : kRemovedFg);
    StyleSetBackground(Style_Removed, dark ? kRemovedBgDark : kRemovedBg);
    StyleSetForeground(Style_HunkHeader, dark ? kHunkFgDark : kHunkFg);
    StyleSetBackground(Style_HunkHeader, dark ? kHunkBgDark : kHunkBg);
    StyleSetForeground(Style_FileHeader, window_fg);
    StyleSetBold(Style_FileHeader, true);
    StyleSetForeground(Style_Meta, dark ? kMetaFgDark : kMetaFg);
    Refresh();
}

void DiffView::SetReadOnlyText(const wxString& text) {
    SetReadOnly(false);
    ClearAll();
    SetText(text);
    SetReadOnly(true);
}

void DiffView::Clear() {
    files_.clear();
    line_hunks_.clear();
    SetReadOnlyText(wxEmptyString);
}

void DiffView::ShowMessage(const wxString& message) {
    files_.clear();
    line_hunks_.clear();
    SetReadOnlyText(message);
    StartStyling(0);
    SetStyling(GetTextLength(), Style_Meta);
}

void DiffView::ShowDiff(std::vector<repomancer::vcs::FileDiff> files,
                        const wxString& banner) {
    using repomancer::vcs::DiffLineKind;

    files_ = std::move(files);
    line_hunks_.clear();

    // Build the text and the style run for each line in one pass; styling by
    // byte length keeps multi-byte UTF-8 aligned, which styling per character
    // would not.
    wxString text;
    std::vector<std::pair<std::size_t, int>> runs; // (byte length, style)

    int current_file = -1;
    int current_hunk = -1;
    const auto append = [&](const wxString& line, int style) {
        const wxString with_eol = line + "\n";
        text += with_eol;
        runs.emplace_back(with_eol.utf8_str().length(), style);
        line_hunks_.emplace_back(current_file, current_hunk);
    };
    if (!banner.empty()) {
        append(banner, Style_Meta);
    }

    for (std::size_t file_index = 0; file_index < files_.size(); ++file_index) {
        const auto& file = files_[file_index];
        current_file = static_cast<int>(file_index);
        current_hunk = -1;
        const wxString name = file.new_path.empty() ? wxString::FromUTF8(file.old_path)
                                                    : wxString::FromUTF8(file.new_path);
        append(name, Style_FileHeader);
        if (!file.old_path.empty() && !file.new_path.empty() &&
            file.old_path != file.new_path) {
            append(wxString::FromUTF8(file.old_path) + " → " + wxString::FromUTF8(file.new_path),
                   Style_Meta);
        }
        if (file.is_binary) {
            append(_("Binary file — no textual diff"), Style_Meta);
            append(wxEmptyString, Style_Default);
            continue;
        }

        for (std::size_t hunk_index = 0; hunk_index < file.hunks.size(); ++hunk_index) {
            const auto& hunk = file.hunks[hunk_index];
            current_hunk = static_cast<int>(hunk_index);
            wxString header =
                wxString::Format("@@ -%d,%d +%d,%d @@", hunk.old_start, hunk.old_count,
                                 hunk.new_start, hunk.new_count);
            if (!hunk.heading.empty()) {
                header += " " + wxString::FromUTF8(hunk.heading);
            }
            append(header, Style_HunkHeader);

            for (const auto& line : hunk.lines) {
                // A trailing CR is real content (CRLF blobs) kept for the
                // hunk-staging round-trip; the display drops it.
                std::string shown = line.text;
                if (!shown.empty() && shown.back() == '\r') {
                    shown.pop_back();
                }
                const wxString body = wxString::FromUTF8(shown);
                switch (line.kind) {
                case DiffLineKind::Added:
                    append("+" + body, Style_Added);
                    break;
                case DiffLineKind::Removed:
                    append("-" + body, Style_Removed);
                    break;
                case DiffLineKind::NoNewline:
                    // The parsed text keeps its leading space.
                    append("\\" + body, Style_Meta);
                    break;
                case DiffLineKind::Context:
                    append(" " + body, Style_Default);
                    break;
                }
            }
        }
        current_hunk = -1;
        append(wxEmptyString, Style_Default);
    }

    SetReadOnlyText(text);
    std::size_t position = 0;
    for (const auto& [length, style] : runs) {
        StartStyling(static_cast<int>(position));
        SetStyling(static_cast<int>(length), style);
        position += length;
    }
}

} // namespace repomancer::gui
