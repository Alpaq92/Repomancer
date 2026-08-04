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
        return wxSize(width + 2 * kPadding, height + kPadding);
    }

    bool Render(wxRect cell, wxDC* dc, int state) override {
        const wxFont base = dc->GetFont();
        const bool selected = (state & wxDATAVIEW_CELL_SELECTED) != 0;
        const wxColour fg = wxSystemSettings::GetColour(
            selected ? wxSYS_COLOUR_HIGHLIGHTTEXT : wxSYS_COLOUR_WINDOWTEXT);

        dc->SetTextForeground(fg);
        int y = cell.GetTop() + kPadding;
        for (const auto& line : lines_) {
            // A heading is any line that is not indented under one.
            const bool heading = !line.empty() && line[0] != ' ';
            dc->SetFont(heading ? base.Bold() : base);
            dc->DrawText(line, cell.GetLeft() + kPadding, y);
            y += dc->GetTextExtent(line.empty() ? " " : line).GetHeight() + kLeading;
        }
        dc->SetFont(base);
        return true;
    }

private:
    static constexpr int kPadding = 8; // cell edge to text
    static constexpr int kLeading = 3;

    std::vector<wxString> lines_;
};

} // namespace

RepoView::RepoView(wxWindow* parent)
    : wxDataViewListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                         // Variable line height: the row is as tall as its
                         // block of lines, instead of clipping to one line.
                         // The border outlines the whole table, header and
                         // cell together; the margin around the widget is what
                         // makes it visible against the pane.
                         wxDV_VARIABLE_LINE_HEIGHT | wxBORDER_SIMPLE) {
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
