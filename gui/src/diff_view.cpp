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

DiffView::DiffView(wxWindow* parent, wxWindowID id) : wxStyledTextCtrl(parent, id) {
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
    // the first character is not pressed against the frame. A little extra
    // leading opens the lines up at the same time.
    SetMarginLeft(10);
    SetMarginRight(10);
    SetExtraAscent(2);
    SetExtraDescent(1);

    ApplyTheme();
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

void DiffView::Clear() { SetReadOnlyText(wxEmptyString); }

void DiffView::ShowMessage(const wxString& message) {
    SetReadOnlyText(message);
    StartStyling(0);
    SetStyling(GetTextLength(), Style_Meta);
}

void DiffView::ShowDiff(const std::vector<repomancer::vcs::FileDiff>& files) {
    using repomancer::vcs::DiffLineKind;

    // Build the text and the style run for each line in one pass; styling by
    // byte length keeps multi-byte UTF-8 aligned, which styling per character
    // would not.
    wxString text;
    std::vector<std::pair<std::size_t, int>> runs; // (byte length, style)

    const auto append = [&](const wxString& line, int style) {
        const wxString with_eol = line + "\n";
        text += with_eol;
        runs.emplace_back(with_eol.utf8_str().length(), style);
    };

    for (const auto& file : files) {
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

        for (const auto& hunk : file.hunks) {
            wxString header =
                wxString::Format("@@ -%d,%d +%d,%d @@", hunk.old_start, hunk.old_count,
                                 hunk.new_start, hunk.new_count);
            if (!hunk.heading.empty()) {
                header += " " + wxString::FromUTF8(hunk.heading);
            }
            append(header, Style_HunkHeader);

            for (const auto& line : hunk.lines) {
                const wxString body = wxString::FromUTF8(line.text);
                switch (line.kind) {
                case DiffLineKind::Added:
                    append("+" + body, Style_Added);
                    break;
                case DiffLineKind::Removed:
                    append("-" + body, Style_Removed);
                    break;
                case DiffLineKind::NoNewline:
                    append("\\ " + body, Style_Meta);
                    break;
                case DiffLineKind::Context:
                    append(" " + body, Style_Default);
                    break;
                }
            }
        }
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
