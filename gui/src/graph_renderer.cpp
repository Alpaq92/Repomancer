// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "graph_renderer.h"

#include <wx/brush.h>
#include <wx/dc.h>
#include <wx/pen.h>

#include <algorithm>
#include <cmath>
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

// Rounded joins and caps: the segments a flattened curve is drawn from meet
// without facets, and a line ending at a dot meets it softly.
wxPen graph_pen(const wxColour& colour) {
    wxPen pen(colour, kLineWidth);
    pen.SetJoin(wxJOIN_ROUND);
    pen.SetCap(wxCAP_ROUND);
    return pen;
}

// wxDC::DrawSpline approximates too coarsely over these short spans and leaves
// visible corners, so the curve is flattened here instead — the shape is then
// identical on every port.
constexpr int kCurveSegments = 20;

std::vector<wxPoint> flatten_cubic(double x0, double y0, double x1, double y1, double x2,
                                   double y2, double x3, double y3) {
    std::vector<wxPoint> points;
    points.reserve(kCurveSegments + 1);
    for (int i = 0; i <= kCurveSegments; ++i) {
        const double t = static_cast<double>(i) / kCurveSegments;
        const double u = 1.0 - t;
        const double b0 = u * u * u;
        const double b1 = 3.0 * u * u * t;
        const double b2 = 3.0 * u * t * t;
        const double b3 = t * t * t;
        points.emplace_back(static_cast<int>(std::lround(b0 * x0 + b1 * x1 + b2 * x2 + b3 * x3)),
                            static_cast<int>(std::lround(b0 * y0 + b1 * y1 + b2 * y2 + b3 * y3)));
    }
    return points;
}

} // namespace

GraphStyle graph_style_from_string(std::string_view value) {
    return value == "rounded" ? GraphStyle::Rounded : GraphStyle::Angular;
}

const char* graph_style_to_string(GraphStyle style) {
    return style == GraphStyle::Rounded ? "rounded" : "angular";
}

wxColour lane_colour(int index) {
    // Open Color (https://yeun.github.io/open-color/, MIT) shade 6 — the step
    // designed to stay legible against both light and dark backgrounds. The
    // order is fixed: branch classes index into it (see branch_class_color).
    static const wxColour palette[] = {
        wxColour(0x22, 0x8B, 0xE6), // blue-6   — main
        wxColour(0xFD, 0x7E, 0x14), // orange-6
        wxColour(0x40, 0xC0, 0x57), // green-6  — feature
        wxColour(0xFA, 0x52, 0x52), // red-6    — hotfix
        wxColour(0xBE, 0x4B, 0xDB), // grape-6  — release
        wxColour(0x12, 0xB8, 0x86), // teal-6   — bugfix
        wxColour(0xFA, 0xB0, 0x05), // yellow-6 — develop
        wxColour(0xE6, 0x49, 0x80), // pink-6
        wxColour(0x15, 0xAA, 0xBF), // cyan-6
        wxColour(0x79, 0x50, 0xF2), // violet-6
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
        dc->SetPen(graph_pen(colour));
        dc->DrawLine(x, y1, x, y2);
    };
    // Which end of a transition must be vertical. A line is only ever diagonal
    // next to the dot it belongs to; at a row boundary it has to be upright, or
    // it meets the straight run in the neighbouring row at an angle.
    enum class CurveKind {
        FromDot,  // dot → lane below: diagonal exit, upright arrival
        IntoDot,  // lane above → dot: upright exit, diagonal arrival
        Crossing, // passes the row without touching the dot: upright at both ends
    };
    struct Curve {
        int x1, y1, x2, y2;
        wxColour colour;
        CurveKind kind;
    };
    std::vector<Curve> curves;

    // Lines first, dot last, so the dot always sits on top.
    for (const auto& segment : row->pass) {
        const int from_x = cell.GetLeft() + lane_x(segment.from);
        const int to_x = cell.GetLeft() + lane_x(segment.to);
        if (from_x == to_x) {
            draw_vertical(from_x, top_run, bottom_run, lane_colour(segment.color));
        } else {
            curves.push_back({from_x, top, to_x, bottom, lane_colour(segment.color),
                              CurveKind::Crossing});
        }
    }
    for (const auto& edge : row->children_in) {
        const int x = cell.GetLeft() + lane_x(edge.lane);
        if (x == dot_x) {
            // Stops at the dot: a lane that ends here must not sprout a stub.
            draw_vertical(x, top_run, middle, lane_colour(edge.color));
        } else {
            curves.push_back(
                {x, top, dot_x, middle, lane_colour(edge.color), CurveKind::IntoDot});
        }
    }
    for (const auto& edge : row->parents_out) {
        const int x = cell.GetLeft() + lane_x(edge.lane);
        if (x == dot_x) {
            draw_vertical(x, middle, bottom_run, lane_colour(edge.color));
        } else {
            curves.push_back(
                {dot_x, middle, x, bottom, lane_colour(edge.color), CurveKind::FromDot});
        }
    }

    for (const auto& curve : curves) {
        dc->SetPen(graph_pen(curve.colour));
        if (style_ == GraphStyle::Angular) {
            // Git Extensions / git-graph: the line runs straight from where it
            // leaves to where it arrives. Round caps take the hard edge off the
            // corner it forms with the run in the neighbouring row.
            dc->DrawLine(curve.x1, curve.y1, curve.x2, curve.y2);
            continue;
        }

        const double dx = curve.x2 - curve.x1;
        const double dy = curve.y2 - curve.y1;
        // A control point placed straight below its anchor holds the tangent
        // upright there; offsetting it sideways lets the line lean into the
        // dot. Every end that lands on a row boundary keeps the upright one,
        // so the curve meets the straight run in the next row head-on.
        double c1x = curve.x1;
        double c1y = curve.y1 + dy * 0.55;
        double c2x = curve.x2;
        double c2y = curve.y2 - dy * 0.55;
        if (curve.kind == CurveKind::FromDot) {
            c1x = curve.x1 + dx * 0.5;
            c1y = curve.y1 + dy * 0.2;
        } else if (curve.kind == CurveKind::IntoDot) {
            c2x = curve.x2 - dx * 0.5;
            c2y = curve.y2 - dy * 0.2;
        }
        const auto points =
            flatten_cubic(curve.x1, curve.y1, c1x, c1y, c2x, c2y, curve.x2, curve.y2);
        dc->DrawLines(static_cast<int>(points.size()), points.data());
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
