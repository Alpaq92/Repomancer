// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "repo_view.h"

#include "pane_header.h"

#include <wx/dcclient.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>

#include <algorithm>
#include <vector>

namespace repomancer::gui {

namespace {

constexpr int kPadding = 8; // canvas edge to content
constexpr int kLeading = 3;
constexpr int kDotSize = 8;  // legend dot diameter
constexpr int kDotGap = 6;   // dot to its label
constexpr int kBarHeight = 8;
constexpr int kBarRow = kBarHeight + 6; // the bar row's total height
constexpr int kBarGap = 2;              // between bar segments
constexpr int kBarMinWidth = 120;
constexpr int kScrollStep = 20; // one wheel line, about one text row

// Where a row's label starts, relative to the content's left edge.
int label_offset(const RepoView::Row& row) {
    return row.dot ? kDotSize + kDotGap : 0;
}

} // namespace

// The scrolled block of rows. Everything — layout, painting, hit-testing,
// scroll range — comes from the same height walk, so none of them can drift
// apart.
class DetailsCanvas : public wxScrolledWindow {
public:
    explicit DetailsCanvas(wxWindow* parent)
        : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxBORDER_NONE) {
        SetScrollRate(0, kScrollStep);
        SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX));
        Bind(wxEVT_PAINT, &DetailsCanvas::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &DetailsCanvas::OnLeftDown, this);
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            event.Skip();
            Refresh(); // the bar spans the client width
        });
    }

    void SetRows(std::vector<RepoView::Row> rows) {
        rows_ = std::move(rows);
        selected_row_ = -1;
        Relayout();
        Refresh();
    }

    void SetOnActivate(std::function<void(const RepoView::Target&)> handler) {
        on_activate_ = std::move(handler);
    }

private:
    // A row's height under the given font context. Leaves the row's font set
    // on `dc`, so a following text measurement matches what is painted.
    int RowHeight(wxDC& dc, const RepoView::Row& row) const {
        if (!row.bar.empty()) {
            return kBarRow;
        }
        const wxFont base = GetFont();
        dc.SetFont(row.heading ? base.Bold() : base);
        const int text = dc.GetTextExtent(row.text.empty() ? wxString(" ") : row.text)
                             .GetHeight() +
                         kLeading;
        // A section separator needs a breath, not a full blank line.
        return row.text.empty() ? text / 2 : text;
    }

    void Relayout() {
        // One height walk, stored: painting and hit-testing read these
        // offsets instead of re-measuring every row on every event.
        wxClientDC dc(this);
        offsets_.clear();
        offsets_.reserve(rows_.size() + 1);
        int width = kBarMinWidth;
        int y = kPadding;
        for (const auto& row : rows_) {
            offsets_.push_back(y);
            const int row_h = RowHeight(dc, row); // sets the row's font
            width = wxMax(width,
                          label_offset(row) +
                              dc.GetTextExtent(row.text.empty() ? wxString(" ") : row.text)
                                  .GetWidth());
            y += row_h;
        }
        offsets_.push_back(y);
        SetVirtualSize(width + 2 * kPadding, y + kPadding);
    }

    // The row containing unscrolled `y`, or -1.
    [[nodiscard]] int RowIndexAt(int y) const {
        if (offsets_.size() < 2 || y < offsets_.front() || y >= offsets_.back()) {
            return -1;
        }
        const auto it = std::upper_bound(offsets_.begin(), offsets_.end(), y);
        return static_cast<int>(it - offsets_.begin()) - 1;
    }

    void OnPaint(wxPaintEvent&) {
        wxPaintDC dc(this);
        DoPrepareDC(dc); // logical coordinates follow the scroll position
        const wxColour fg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
        const int width = GetClientSize().GetWidth();

        // Only the rows intersecting the viewport.
        int view_x = 0;
        int view_y = 0;
        GetViewStart(&view_x, &view_y);
        const int top = view_y * kScrollStep;
        const int bottom = top + GetClientSize().GetHeight();
        std::size_t first = 0;
        if (const int hit = RowIndexAt(std::max(top, offsets_.empty() ? 0 : offsets_.front()));
            hit > 0) {
            first = static_cast<std::size_t>(hit);
        }

        for (std::size_t i = first; i < rows_.size(); ++i) {
            if (offsets_[i] >= bottom) {
                break;
            }
            const int y = offsets_[i];
            const RepoView::Row& row = rows_[i];
            const int height = RowHeight(dc, row); // sets the row's font
            if (!row.bar.empty()) {
                DrawBar(dc, row, kPadding, y, width - 2 * kPadding);
                continue;
            }
            const wxSize extent =
                dc.GetTextExtent(row.text.empty() ? wxString(" ") : row.text);
            const bool highlit = static_cast<int>(i) == selected_row_;
            dc.SetTextForeground(fg);
            if (highlit) {
                // A chip hugging the label — the same bounds the hit-test
                // accepts, so what lights up is exactly what was clickable.
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)));
                dc.DrawRoundedRectangle(kPadding + label_offset(row) - 4, y - 1,
                                        extent.GetWidth() + 12, extent.GetHeight() + 2, 3);
                dc.SetTextForeground(
                    wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));
            }
            if (row.dot) {
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(wxBrush(row.colour));
                dc.DrawEllipse(kPadding, y + (extent.GetHeight() - kDotSize) / 2, kDotSize,
                               kDotSize);
            }
            dc.DrawText(row.text, kPadding + label_offset(row), y);
        }
    }

    void OnLeftDown(wxMouseEvent& event) {
        const wxPoint point = CalcUnscrolledPosition(event.GetPosition());
        const int index = RowIndexAt(point.y);
        if (index < 0) {
            return;
        }
        const RepoView::Row& row = rows_[static_cast<std::size_t>(index)];
        if (row.target.empty()) {
            return;
        }
        // An item is only its label: a click in the blank space beside it is
        // no click on it.
        wxClientDC dc(this);
        const wxFont base = GetFont();
        dc.SetFont(row.heading ? base.Bold() : base);
        const wxSize extent = dc.GetTextExtent(row.text.empty() ? wxString(" ") : row.text);
        const int start = kPadding + label_offset(row);
        if (point.x >= start - 4 && point.x <= start + extent.GetWidth() + 8) {
            selected_row_ = index;
            Refresh();
            if (on_activate_) {
                on_activate_(row.target);
            }
        }
    }

    static void DrawBar(wxDC& dc, const RepoView::Row& row, int left, int y, int width) {
        double total = 0.0;
        for (const auto& segment : row.bar) {
            total += segment.second;
        }
        if (total <= 0.0 || width < kBarMinWidth / 2) {
            return;
        }
        dc.SetPen(*wxTRANSPARENT_PEN);
        const int gaps = static_cast<int>(row.bar.size() - 1) * kBarGap;
        const int usable = width - gaps;
        const int top = y + (kBarRow - kBarHeight) / 2;
        double carry = 0.0;
        int x = left;
        for (const auto& segment : row.bar) {
            carry += usable * (segment.second / total);
            const int segment_width = static_cast<int>(carry);
            carry -= segment_width;
            if (segment_width > 0) {
                dc.SetBrush(wxBrush(segment.first));
                dc.DrawRoundedRectangle(x, top, segment_width, kBarHeight, 2);
                x += segment_width + kBarGap;
            }
        }
    }

    std::vector<RepoView::Row> rows_;
    std::vector<int> offsets_; // unscrolled y of each row, plus the end
    int selected_row_ = -1;
    std::function<void(const RepoView::Target&)> on_activate_;
};

RepoView::RepoView(wxWindow* parent) : wxPanel(parent) {
    header_ = new PaneHeader(this, _("Repository"));
    canvas_ = new DetailsCanvas(this);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(header_, 0, wxEXPAND);
    sizer->Add(canvas_, 1, wxEXPAND);
    SetSizer(sizer);
}

void RepoView::SetRows(std::vector<Row> rows) { canvas_->SetRows(std::move(rows)); }

void RepoView::SetOnActivate(std::function<void(const Target&)> handler) {
    canvas_->SetOnActivate(std::move(handler));
}

wxWindow* RepoView::canvas() const { return canvas_; }

} // namespace repomancer::gui
