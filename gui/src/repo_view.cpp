// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "repo_view.h"

#include <wx/dc.h>
#include <wx/settings.h>
#include <wx/utils.h>

#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

#include <vector>

namespace repomancer::gui {

namespace {

// Shared by the renderer and the click hit-test, which must agree on where
// each line sits.
constexpr int kPadding = 8; // cell edge to text
constexpr int kLeading = 3;

std::vector<wxString> split_lines(const wxString& text) {
    std::vector<wxString> lines;
    wxString rest = text;
    while (!rest.empty()) {
        lines.push_back(rest.BeforeFirst('\n'));
        const int eol = rest.Find('\n');
        if (eol == wxNOT_FOUND) {
            break;
        }
        rest = rest.Mid(eol + 1);
    }
    return lines;
}

// Draws the one cell: a block of lines, headings bold. A stock text renderer
// shows a single line only, which is no use for a cell that carries the whole
// summary.
class DetailsRenderer : public wxDataViewCustomRenderer {
public:
    // `selected_line` belongs to the owning view; the renderer only reads it.
    explicit DetailsRenderer(const int* selected_line)
        : wxDataViewCustomRenderer("string", wxDATAVIEW_CELL_INERT, wxALIGN_LEFT),
          selected_line_(selected_line) {}

    bool SetValue(const wxVariant& value) override {
        lines_ = split_lines(value.GetString());
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
        for (std::size_t i = 0; i < lines_.size(); ++i) {
            const wxString& line = lines_[i];
            // A heading is any line that is not indented under one.
            const bool heading = !line.empty() && line[0] != ' ';
            dc->SetFont(heading ? base.Bold() : base);
            const int height = dc->GetTextExtent(line.empty() ? " " : line).GetHeight();
            const bool highlit =
                selected_line_ != nullptr && static_cast<int>(i) == *selected_line_;
            if (highlit) {
                dc->SetPen(*wxTRANSPARENT_PEN);
                dc->SetBrush(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)));
                dc->DrawRectangle(cell.GetLeft() + 2, y - 1, cell.GetWidth() - 4,
                                  height + kLeading);
                dc->SetTextForeground(
                    wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));
            }
            dc->DrawText(line, cell.GetLeft() + kPadding, y);
            if (highlit) {
                dc->SetTextForeground(fg);
            }
            y += height + kLeading;
        }
        dc->SetFont(base);
        return true;
    }

private:
    std::vector<wxString> lines_;
    const int* selected_line_;
};

#ifdef __WXGTK__
extern "C" {
static gboolean repo_view_button_press(GtkWidget* widget, GdkEventButton* event,
                                       gpointer data) {
    // Only presses on the rows themselves; header clicks arrive on another
    // GdkWindow and their coordinates would convert into nonsense.
    if (event->type == GDK_BUTTON_PRESS && event->button == 1 &&
        event->window == gtk_tree_view_get_bin_window(GTK_TREE_VIEW(widget))) {
        int x = 0;
        int y = 0;
        gtk_tree_view_convert_bin_window_to_widget_coords(
            GTK_TREE_VIEW(widget), static_cast<int>(event->x),
            static_cast<int>(event->y), &x, &y);
        static_cast<repomancer::gui::RepoView*>(data)->OnItemPress(wxPoint(x, y));
    }
    return FALSE;
}
}
#endif

} // namespace

RepoView::RepoView(wxWindow* parent)
    : wxDataViewListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                         // Variable line height: the row is as tall as its
                         // block of lines, instead of clipping to one line.
                         // No native frame: the pane draws the outline itself,
                         // the same hairline every table gets.
                         wxDV_VARIABLE_LINE_HEIGHT | wxBORDER_NONE) {
    column_ = new wxDataViewColumn(_("Repository"), new DetailsRenderer(&selected_line_), 0,
                                   wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, 0);
    AppendColumn(column_, "string");
    // One column spanning the pane, exactly like the other pane headers.
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        event.Skip();
        if (column_ != nullptr) {
            column_->SetWidth(GetClientSize().GetWidth());
        }
    });

    // The table has exactly one row, so selecting it means nothing — the
    // click's position is what carries the intent. A click on a line that
    // names a ref activates that ref, and the selection is dropped again so
    // the block does not light up as one big selected cell.
    Bind(wxEVT_LEFT_DOWN, [](wxMouseEvent& event) {
        event.Skip();
        fprintf(stderr, "[down] wx mouse event at (%d,%d)\n", event.GetX(), event.GetY());
    });
    Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [this](wxDataViewEvent& event) {
        event.Skip();
        if (event.GetItem().IsOk()) {
            const int line = LineAt(ScreenToClient(wxGetMousePosition()));
            if (line >= 0 && line < static_cast<int>(targets_.size()) &&
                !targets_[line].empty()) {
                // The click landed on an item: mark it like a list selection
                // and act on it.
                selected_line_ = line;
                if (on_activate_) {
                    on_activate_(targets_[line]);
                }
            }
        }
        CallAfter([this] {
            UnselectAll();
            Refresh();
        });
    });
}

void RepoView::OnItemPress(const wxPoint& point) {
    const int line = LineAt(point);
    if (line >= 0 && line < static_cast<int>(targets_.size()) &&
        !targets_[line].empty()) {
        selected_line_ = line;
        Refresh();
        if (on_activate_) {
            on_activate_(targets_[line]);
        }
    }
}

int RepoView::LineAt(const wxPoint& point) {
    if (GetItemCount() == 0) {
        return -1;
    }
    const wxRect cell = GetItemRect(RowToItem(0), column_);
    if (!cell.Contains(point)) {
        return -1;
    }
    // The same walk the renderer paints by: kPadding of headroom, then each
    // line's own height plus its leading.
    int y = cell.GetTop() + kPadding;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        if (point.y < y) {
            return -1; // in the headroom above the first line
        }
        const wxString& line = lines_[i];
        const int height =
            GetTextExtent(line.empty() ? wxString(" ") : line).GetHeight() + kLeading;
        if (point.y < y + height) {
            return static_cast<int>(i);
        }
        y += height;
    }
    return -1;
}

void RepoView::SetDetails(const wxString& details, std::vector<Target> targets) {
    lines_ = split_lines(details);
    targets_ = std::move(targets);
    selected_line_ = -1;
    DeleteAllItems();
    wxVector<wxVariant> row;
    row.push_back(wxVariant(details));
    AppendItem(row);
}

} // namespace repomancer::gui
