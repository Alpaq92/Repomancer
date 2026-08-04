// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "repo_view.h"

#include <wx/dc.h>
#include <wx/settings.h>

#include <vector>

namespace repomancer::gui {

namespace {

// Draws the one cell: a block of lines, headings bold. A stock text renderer
// shows a single line only, which is no use for a cell that carries the whole
// summary.
class DetailsRenderer : public wxDataViewCustomRenderer {
public:
    DetailsRenderer()
        : wxDataViewCustomRenderer("string", wxDATAVIEW_CELL_INERT, wxALIGN_LEFT) {}

    bool SetValue(const wxVariant& value) override {
        lines_.clear();
        wxString rest = value.GetString();
        while (!rest.empty()) {
            lines_.push_back(rest.BeforeFirst('\n'));
            const int eol = rest.Find('\n');
            if (eol == wxNOT_FOUND) {
                break;
            }
            rest = rest.Mid(eol + 1);
        }
        return true;
    }

    bool GetValue(wxVariant&) const override { return false; }

    wxSize GetSize() const override {
        int width = 0;
        int height = kPadding;
        for (const auto& line : lines_) {
            const wxSize extent = GetTextExtent(line.empty() ? " " : line);
            width = wxMax(width, extent.GetWidth());
            height += extent.GetHeight() + kLeading;
        }
        return wxSize(width + 2 * (kMargin + kPadding),
                      height + kPadding + 2 * kMargin);
    }

    bool Render(wxRect cell, wxDC* dc, int state) override {
        const wxFont base = dc->GetFont();
        const bool selected = (state & wxDATAVIEW_CELL_SELECTED) != 0;
        const wxColour fg = wxSystemSettings::GetColour(
            selected ? wxSYS_COLOUR_HIGHLIGHTTEXT : wxSYS_COLOUR_WINDOWTEXT);
        const wxColour bg = wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX);

        // The cell's outline, inset by a margin so it reads as a table cell
        // rather than as text floating under the header. The line colour is a
        // blend of the theme's own text and background, so it stays a subtle
        // hairline in both light and dark.
        const auto blend = [](int a, int b) { return (a * 30 + b * 70) / 100; };
        const wxColour border(blend(fg.Red(), bg.Red()), blend(fg.Green(), bg.Green()),
                              blend(fg.Blue(), bg.Blue()));
        wxRect box = cell;
        box.Deflate(kMargin, kMargin);
        dc->SetPen(wxPen(border, 1));
        dc->SetBrush(*wxTRANSPARENT_BRUSH);
        dc->DrawRectangle(box);

        dc->SetTextForeground(fg);
        int y = box.GetTop() + kPadding;
        for (const auto& line : lines_) {
            // A heading is any line that is not indented under one.
            const bool heading = !line.empty() && line[0] != ' ';
            dc->SetFont(heading ? base.Bold() : base);
            dc->DrawText(line, box.GetLeft() + kPadding, y);
            y += dc->GetTextExtent(line.empty() ? " " : line).GetHeight() + kLeading;
        }
        dc->SetFont(base);
        return true;
    }

private:
    static constexpr int kMargin = 6;  // cell edge to outline
    static constexpr int kPadding = 8; // outline to text
    static constexpr int kLeading = 3;

    std::vector<wxString> lines_;
};

} // namespace

RepoView::RepoView(wxWindow* parent)
    : wxDataViewListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                         // Variable line height: the row is as tall as its
                         // block of lines, instead of clipping to one line.
                         wxDV_VARIABLE_LINE_HEIGHT | wxBORDER_NONE) {
    column_ = new wxDataViewColumn(_("Repository"), new DetailsRenderer(), 0,
                                   wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, 0);
    AppendColumn(column_, "string");
    // One column spanning the pane, exactly like the other pane headers.
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        event.Skip();
        if (column_ != nullptr) {
            column_->SetWidth(GetClientSize().GetWidth());
        }
    });
}

void RepoView::SetDetails(const wxString& details) {
    DeleteAllItems();
    wxVector<wxVariant> row;
    row.push_back(wxVariant(details));
    AppendItem(row);
}

} // namespace repomancer::gui
