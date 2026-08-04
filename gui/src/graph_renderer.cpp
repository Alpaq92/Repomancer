// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "graph_renderer.h"

#include <wx/brush.h>
#include <wx/dc.h>
#include <wx/pen.h>

#include <algorithm>
#include <iterator>
#include <tuple>
#include <vector>

namespace repomancer::gui {

namespace {

constexpr int kLaneWidth = 16;  // px between lane centres
constexpr int kMargin = 7;      // px before the first lane
constexpr int kDotRadius = 4;
constexpr int kMergeDotRadius = 5;
constexpr int kLineWidth = 2;

int lane_x(int lane) { return kMargin + lane * kLaneWidth + kLaneWidth / 2; }

} // namespace

wxColour lane_colour(int index) {
    static const wxColour palette[] = {
        wxColour(0x4E, 0x79, 0xA7), wxColour(0xF2, 0x8E, 0x2B), wxColour(0x59, 0xA1, 0x4F),
        wxColour(0xE1, 0x57, 0x59), wxColour(0xB0, 0x7A, 0xA1), wxColour(0x76, 0xB7, 0xB2),
        wxColour(0xED, 0xC9, 0x48), wxColour(0xFF, 0x9D, 0xA7),
    };
    constexpr int count = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
    if (index < 0) {
        index = 0;
    }
    return palette[index % count];
}

GraphRenderer::GraphRenderer(const std::vector<repomancer::vcs::GraphRow>* rows)
    : wxDataViewCustomRenderer("long", wxDATAVIEW_CELL_INERT, wxALIGN_LEFT), rows_(rows) {}

bool GraphRenderer::SetValue(const wxVariant& value) {
    row_index_ = value.GetLong();
    return true;
}

bool GraphRenderer::GetValue(wxVariant& value) const {
    value = row_index_;
    return true;
}

const repomancer::vcs::GraphRow* GraphRenderer::current_row() const {
    if (rows_ == nullptr || row_index_ < 0 ||
        static_cast<std::size_t>(row_index_) >= rows_->size()) {
        return nullptr;
    }
    return &(*rows_)[static_cast<std::size_t>(row_index_)];
}

wxSize GraphRenderer::GetSize() const {
    return wxSize(kMargin * 2 + max_lanes_ * kLaneWidth, kRowHeight);
}

bool GraphRenderer::Render(wxRect cell, wxDC* dc, int /*state*/) {
    const auto* row = current_row();
    if (row == nullptr) {
        return true;
    }

    const int middle = cell.GetTop() + cell.GetHeight() / 2;
    // Draw to the row's own edges — never further. Each row hands an edge over
    // at exactly the lane's x, so the next row picks it up seamlessly; a
    // longer span would only reach that x past the visible boundary and the
    // line would break. The extra pixel is overlap against antialiasing seams
    // and the padding ports put around the cell rect.
    const int half = std::max(cell.GetHeight(), kRowHeight) / 2 + 1;
    const int top = middle - half;
    const int bottom = middle + half;
    // Straight runs may overhang into the neighbouring rows — whatever padding
    // the port leaves around the cell rect gets covered either way. Curves
    // must not: they have to reach their lane exactly at the boundary, or the
    // next row picks the line up at the wrong x.
    const int top_run = middle - half - kRowHeight / 2;
    const int bottom_run = middle + half + kRowHeight / 2;
    const int dot_x = cell.GetLeft() + lane_x(row->lane);

    // Straight runs go through the plain DC: pixel-aligned, no antialiasing,
    // so a lane reads as one solid line instead of a blurred double edge.
    const auto draw_vertical = [&](int x, int y1, int y2, const wxColour& colour) {
        dc->SetPen(wxPen(colour, kLineWidth));
        dc->DrawLine(x, y1, x, y2);
    };
    std::vector<std::tuple<int, int, int, int, wxColour>> curves;

    // Lines first, dot last, so the dot always sits on top.
    for (const auto& segment : row->pass) {
        const int from_x = cell.GetLeft() + lane_x(segment.from);
        const int to_x = cell.GetLeft() + lane_x(segment.to);
        if (from_x == to_x) {
            draw_vertical(from_x, top_run, bottom_run, lane_colour(segment.color));
        } else {
            curves.emplace_back(from_x, top, to_x, bottom, lane_colour(segment.color));
        }
    }
    for (const auto& edge : row->children_in) {
        const int x = cell.GetLeft() + lane_x(edge.lane);
        if (x == dot_x) {
            // Stops at the dot: a lane that ends here must not sprout a stub.
            draw_vertical(x, top_run, middle, lane_colour(edge.color));
        } else {
            curves.emplace_back(x, top, dot_x, middle, lane_colour(edge.color));
        }
    }
    for (const auto& edge : row->parents_out) {
        const int x = cell.GetLeft() + lane_x(edge.lane);
        if (x == dot_x) {
            draw_vertical(x, middle, bottom_run, lane_colour(edge.color));
        } else {
            curves.emplace_back(dot_x, middle, x, bottom, lane_colour(edge.color));
        }
    }

    // DrawSpline works on every wxDC — unlike wxGraphicsContext, which the
    // DataView cell DC does not hand out on all ports. The two interior
    // points sit directly below the start and above the end, so the spline
    // leaves and enters its lane vertically and the join with the next row is
    // invisible.
    for (const auto& [x1, y1, x2, y2, colour] : curves) {
        const int dy = y2 - y1;
        const wxPoint points[] = {
            wxPoint(x1, y1),
            wxPoint(x1, y1 + dy / 3),
            wxPoint(x2, y2 - dy / 3),
            wxPoint(x2, y2),
        };
        dc->SetPen(wxPen(colour, kLineWidth));
        dc->DrawSpline(static_cast<int>(std::size(points)), points);
    }

    // No halo around the dot: it would be painted over the lane that runs into
    // and out of the dot, notching a gap into an otherwise continuous line.
    // The dot is drawn last, so it already covers the line ends it should.
    const int radius = row->is_merge ? kMergeDotRadius : kDotRadius;
    dc->SetBrush(wxBrush(lane_colour(row->color)));
    dc->SetPen(wxPen(lane_colour(row->color), 1));
    dc->DrawCircle(dot_x, middle, radius);
    return true;
}

} // namespace repomancer::gui
