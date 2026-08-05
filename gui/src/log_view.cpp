// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "log_view.h"

#include "pane_header.h"

#include <wx/dataview.h>
#include <wx/dcclient.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>

#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace repomancer::gui {

namespace {

constexpr int kCellPad = 4;   // text inset within a column
constexpr int kMinSubject = 200;

} // namespace

// The header strip: the shared PaneHeader widget carrying the log's five
// columns instead of a single title, so every header in the window is drawn
// (and self-sized) by the same class.
class LogHeaderCtrl : public PaneHeader {
public:
    explicit LogHeaderCtrl(wxWindow* parent) : PaneHeader(parent) {
        const wxString titles[] = {_("Graph"), _("Subject"), _("Author"), _("Date"),
                                   _("Hash")};
        const int widths[] = {60, 420, 160, 140, 100};
        for (int i = 0; i < CommitLogModel::Col_Count; ++i) {
            AppendTextColumn(titles[i], wxDATAVIEW_CELL_INERT, widths[i], wxALIGN_LEFT,
                             wxDATAVIEW_COL_RESIZABLE);
        }
        // A stretch filler after the last column: without it the header band
        // stops at "Hash" and the control's white body shows to the edge.
        AppendTextColumn(wxEmptyString, wxDATAVIEW_CELL_INERT, 0, wxALIGN_LEFT, 0);
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            event.Skip();
            StretchFiller();
        });
    }

    void StretchFiller() {
        int used = 0;
        for (unsigned int i = 0; i < CommitLogModel::Col_Count; ++i) {
            used += ColumnWidth(i);
        }
        if (wxDataViewColumn* filler = GetColumn(CommitLogModel::Col_Count)) {
            filler->SetWidth(std::max(0, GetClientSize().GetWidth() - used));
        }
    }

    [[nodiscard]] int ColumnWidth(unsigned int column) const {
        const wxDataViewColumn* col = GetColumn(column);
        return col != nullptr ? col->GetWidth() : 0;
    }

    void SetColumnWidth(unsigned int column, int width) {
        if (wxDataViewColumn* col = GetColumn(column)) {
            col->SetWidth(width);
        }
    }
};

// The rows. Layout, painting, hit-testing and the scroll range all derive
// from one constant pitch, so they cannot disagree — and scrolling is wx's
// own, immune to whatever the native tree view's scroll path does with this
// platform's input.
class LogCanvasCtrl : public wxScrolledWindow {
public:
    LogCanvasCtrl(wxWindow* parent, CommitLogModel* model, LogHeaderCtrl* header)
        : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxBORDER_NONE | wxWANTS_CHARS),
          model_(model),
          header_(header) {
        SetScrollRate(0, kRowHeight);
        SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX));
        Bind(wxEVT_PAINT, &LogCanvasCtrl::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &LogCanvasCtrl::OnLeftDown, this);
        Bind(wxEVT_RIGHT_DOWN, &LogCanvasCtrl::OnRightDown, this);
        Bind(wxEVT_KEY_DOWN, &LogCanvasCtrl::OnKeyDown, this);
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            event.Skip();
            Refresh();
        });
    }

    void ModelChanged() {
        strips_.clear();
        selected_ = -1;
        UpdateVirtualSize();
        Scroll(0, 0); // a new history starts at its newest commit
        Refresh();
    }

    void SetStyle(GraphStyle style) {
        if (style != style_) {
            style_ = style;
            InvalidateStrips();
        }
    }

    void InvalidateStrips() {
        strips_.clear();
        Refresh();
    }

    void Select(int row) {
        const int count = static_cast<int>(model_->GetCount());
        if (row < 0 || row >= count) {
            return;
        }
        if (selected_ != row) {
            // One selected strip variant is live at a time; drop the old one.
            strips_.erase(static_cast<long>(selected_) * 2 + 1);
        }
        selected_ = row;
        EnsureVisible(row);
        Refresh();
        if (on_select_) {
            on_select_(row);
        }
    }

    [[nodiscard]] int SelectedRow() const { return selected_; }

    [[nodiscard]] CommitLogModel* model() const { return model_; }

    void SetOnSelect(std::function<void(int)> handler) {
        on_select_ = std::move(handler);
    }

    void SetOnContextMenu(std::function<void(int)> handler) {
        on_context_menu_ = std::move(handler);
    }

    void UpdateVirtualSize() {
        int width = 0;
        for (unsigned int i = 0; i < CommitLogModel::Col_Count; ++i) {
            width += header_->ColumnWidth(i);
        }
        SetVirtualSize(width,
                       static_cast<int>(model_->GetCount()) * kRowHeight);
    }

private:
    void EnsureVisible(int row) {
        int view_x = 0;
        int view_y = 0;
        GetViewStart(&view_x, &view_y); // in scroll steps == rows
        const int first = view_y;
        const int visible = GetClientSize().GetHeight() / kRowHeight;
        if (row < first) {
            Scroll(view_x, row);
        } else if (row >= first + visible) {
            Scroll(view_x, std::max(0, row - visible + 1));
        }
    }

    void OnPaint(wxPaintEvent&) {
        wxPaintDC dc(this);
        DoPrepareDC(dc);

        const int count = static_cast<int>(model_->GetCount());
        if (count == 0) {
            return;
        }
        int view_x = 0;
        int view_y = 0;
        GetViewStart(&view_x, &view_y);
        const int height = GetClientSize().GetHeight();
        const int first = std::max(0, view_y);
        const int last =
            std::min(count - 1, view_y + height / kRowHeight + 1);

        const wxColour fg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
        const wxColour sel_bg = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);
        const wxColour sel_fg = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT);
        const int char_height = GetCharHeight();

        // Column x origins from the header, so the two always line up.
        int origins[CommitLogModel::Col_Count + 1];
        origins[0] = 0;
        for (int i = 0; i < CommitLogModel::Col_Count; ++i) {
            origins[i + 1] = origins[i] + header_->ColumnWidth(static_cast<unsigned>(i));
        }

        for (int row = first; row <= last; ++row) {
            const int y = row * kRowHeight;
            const bool selected = row == selected_;
            if (selected) {
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(wxBrush(sel_bg));
                dc.DrawRectangle(0, y, std::max(origins[CommitLogModel::Col_Count], GetClientSize().GetWidth()),
                                 kRowHeight);
            }

            // Graph strip, cached by (row, selected).
            const auto& rows = model_->graph_rows();
            if (static_cast<std::size_t>(row) < rows.size()) {
                const int strip_width = std::max(1, origins[1] - origins[0]);
                if (strip_width != strip_width_) {
                    strips_.clear();
                    strip_width_ = strip_width;
                }
                // Enough for several viewports; scrolling a long history
                // must not accumulate strips without bound.
                if (strips_.size() > 256) {
                    strips_.clear();
                }
                const long key = row * 2 + (selected ? 1 : 0);
                auto hit = strips_.find(key);
                if (hit == strips_.end()) {
                    const wxBitmap strip = render_graph_strip(
                        rows[static_cast<std::size_t>(row)], row == 0, row == count - 1,
                        strip_width, kRowHeight, selected, style_);
                    if (strip.IsOk()) {
                        hit = strips_.emplace(key, strip).first;
                    }
                }
                if (hit != strips_.end()) {
                    // Clipped to the row band: the strip carries transparent
                    // overhang above and below, and an unclipped blit lets
                    // that band wipe the neighbouring row's curve tails.
                    wxDCClipper clip(dc, origins[0], y, strip_width,
                                     kRowHeight);
                    dc.DrawBitmap(hit->second, origins[0],
                                  y + kRowHeight / 2 -
                                      hit->second.GetHeight() / 2,
                                  /*useMask=*/true);
                }
            }

            dc.SetTextForeground(selected ? sel_fg : fg);
            const int text_y = y + (kRowHeight - char_height) / 2;
            for (int col = CommitLogModel::Col_Subject;
                 col < CommitLogModel::Col_Count; ++col) {
                const wxString& text = model_->text(
                    static_cast<unsigned>(row),
                    static_cast<CommitLogModel::Column>(col));
                if (text.empty()) {
                    continue;
                }
                const int left = origins[col] + kCellPad;
                const int cell_width = origins[col + 1] - origins[col] - 2 * kCellPad;
                if (cell_width <= 0) {
                    continue;
                }
                wxDCClipper clip(dc, left, y, cell_width, kRowHeight);
                dc.DrawText(text, left, text_y);
            }
        }
    }

    void OnLeftDown(wxMouseEvent& event) {
        SetFocus();
        const wxPoint point = CalcUnscrolledPosition(event.GetPosition());
        const int row = point.y / kRowHeight;
        if (row >= 0 && row < static_cast<int>(model_->GetCount())) {
            Select(row);
        }
    }

    void OnRightDown(wxMouseEvent& event) {
        SetFocus();
        const wxPoint point = CalcUnscrolledPosition(event.GetPosition());
        const int row = point.y / kRowHeight;
        if (row < 0 || row >= static_cast<int>(model_->GetCount())) {
            return;
        }
        Select(row); // the menu acts on what the user sees highlighted
        if (on_context_menu_) {
            on_context_menu_(row);
        }
    }

    void OnKeyDown(wxKeyEvent& event) {
        const int count = static_cast<int>(model_->GetCount());
        if (count == 0) {
            event.Skip();
            return;
        }
        const int visible = std::max(1, GetClientSize().GetHeight() / kRowHeight);
        int target = selected_ < 0 ? 0 : selected_;
        switch (event.GetKeyCode()) {
        case WXK_UP:
            target -= 1;
            break;
        case WXK_DOWN:
            target += 1;
            break;
        case WXK_PAGEUP:
            target -= visible;
            break;
        case WXK_PAGEDOWN:
            target += visible;
            break;
        case WXK_HOME:
            target = 0;
            break;
        case WXK_END:
            target = count - 1;
            break;
        default:
            event.Skip();
            return;
        }
        Select(std::clamp(target, 0, count - 1));
    }

    CommitLogModel* model_;
    LogHeaderCtrl* header_;
    GraphStyle style_ = GraphStyle::Angular;
    int selected_ = -1;
    std::function<void(int)> on_select_;
    std::function<void(int)> on_context_menu_;
    std::unordered_map<long, wxBitmap> strips_;
    int strip_width_ = -1;
};

LogView::LogView(wxWindow* parent, CommitLogModel* model) : wxPanel(parent) {
    header_ = new LogHeaderCtrl(this);
    canvas_ = new LogCanvasCtrl(this, model, header_);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(header_, 0, wxEXPAND);
    sizer->Add(canvas_, 1, wxEXPAND);
    SetSizer(sizer);

    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        event.Skip();
        FitColumns();
    });
}

void LogView::ModelChanged() {
    header_->ClampToNativeHeader();
    MeasureContentWidths();
    FitColumns();
    canvas_->ModelChanged();
}

void LogView::MeasureContentWidths() {
    // Content widths depend on the rows and the font only — never on the
    // client width — so they are measured once per load, not per size event.
    wxClientDC dc(this);
    const auto text_width = [&dc](const wxString& text) {
        return dc.GetTextExtent(text).GetWidth();
    };
    CommitLogModel* m = canvas_->model();
    author_width_ = text_width(_("Author")) + 16;
    date_width_ = text_width(_("Date")) + 16;
    hash_width_ = text_width(_("Hash")) + 16;
    for (unsigned row = 0; row < m->GetCount(); ++row) {
        author_width_ = std::max(
            author_width_,
            text_width(m->text(row, CommitLogModel::Col_Author)) + 2 * kCellPad + 4);
        date_width_ = std::max(
            date_width_,
            text_width(m->text(row, CommitLogModel::Col_Date)) + 2 * kCellPad + 4);
        hash_width_ = std::max(
            hash_width_,
            text_width(m->text(row, CommitLogModel::Col_Hash)) + 2 * kCellPad + 4);
    }
    graph_width_ = std::max(graph_strip_width(m->max_lanes()),
                            wxClientDC(this).GetTextExtent(_("Graph")).GetWidth() + 16);
}

void LogView::SetStyle(GraphStyle style) { canvas_->SetStyle(style); }

void LogView::InvalidateStrips() { canvas_->InvalidateStrips(); }

void LogView::FitColumns() {
    const int available =
        GetClientSize().GetWidth() - graph_width_ - author_width_ - date_width_ -
        hash_width_ - 8;
    header_->SetColumnWidth(0, graph_width_);
    header_->SetColumnWidth(1, std::max(kMinSubject, available));
    header_->SetColumnWidth(2, author_width_);
    header_->SetColumnWidth(3, date_width_);
    header_->SetColumnWidth(4, hash_width_);
    header_->StretchFiller();
    canvas_->UpdateVirtualSize();
    canvas_->Refresh();
}

void LogView::Select(int row) { canvas_->Select(row); }

int LogView::SelectedRow() const { return canvas_->SelectedRow(); }

void LogView::SetOnSelect(std::function<void(int)> handler) {
    canvas_->SetOnSelect(std::move(handler));
}

void LogView::SetOnContextMenu(std::function<void(int)> handler) {
    canvas_->SetOnContextMenu(std::move(handler));
}

wxWindow* LogView::canvas() const { return canvas_; }

} // namespace repomancer::gui
