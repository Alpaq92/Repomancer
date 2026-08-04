// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "graph_renderer.h"

#include <wx/bitmap.h>
#include <wx/brush.h>
#include <wx/dc.h>
#include <wx/dcmemory.h>
#include <wx/graphics.h>
#include <wx/pen.h>
#include <wx/settings.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace repomancer::gui {

namespace {

constexpr int kLaneWidth = 16; // px between lane centres
constexpr int kMargin = 7;     // px before the first lane
constexpr double kDotRadius = 4.0;
constexpr double kMergeDotRadius = 5.0;
constexpr double kLineWidth = 2.0;

int lane_x(int lane) { return kMargin + lane * kLaneWidth + kLaneWidth / 2; }

wxPen graph_pen(const wxColour& colour) {
    wxPen pen(colour, static_cast<int>(kLineWidth));
    pen.SetJoin(wxJOIN_ROUND);
    pen.SetCap(wxCAP_ROUND);
    return pen;
}

// Which end of a transition must be vertical. A line is only ever diagonal
// next to the dot it belongs to; at a row boundary it has to be upright, or it
// meets the straight run in the neighbouring row at an angle.
enum class CurveKind {
    FromDot,  // dot → lane below: diagonal exit, upright arrival
    IntoDot,  // lane above → dot: upright exit, diagonal arrival
    Crossing, // passes the row without touching the dot: upright at both ends
};

struct Curve {
    double x1, y1, x2, y2;
    wxColour colour;
    CurveKind kind;
};

struct Line {
    double x, y1, y2;
    wxColour colour;
};

} // namespace

GraphStyle graph_style_from_string(std::string_view value) {
    return value == "rounded" ? GraphStyle::Rounded : GraphStyle::Angular;
}

const char* graph_style_to_string(GraphStyle style) {
    return style == GraphStyle::Angular ? "angular" : "rounded";
}

wxColour lane_colour(int index) {
    // Open Color (https://yeun.github.io/open-color/, MIT) shade 6 — the step
    // designed to stay legible against both light and dark backgrounds. The
    // order is deliberate: adjacent lanes take adjacent entries, so it runs
    // through the hue circle rather than along it, and neighbouring lanes stay
    // told apart. Branch classes index into it by name (branch_class_color).
    static const wxColour palette[] = {
        wxColour(0xE6, 0x49, 0x80), // pink-6
        wxColour(0x79, 0x50, 0xF2), // violet-6
        wxColour(0x22, 0x8B, 0xE6), // blue-6
        wxColour(0x12, 0xB8, 0x86), // teal-6
        wxColour(0x82, 0xC9, 0x1E), // lime-6
        wxColour(0xFD, 0x7E, 0x14), // orange-6
        wxColour(0xFA, 0x52, 0x52), // red-6
        wxColour(0xBE, 0x4B, 0xDB), // grape-6
        wxColour(0x4C, 0x6E, 0xF5), // indigo-6
        wxColour(0x15, 0xAA, 0xBF), // cyan-6
        wxColour(0x40, 0xC0, 0x57), // green-6
        wxColour(0xFA, 0xB0, 0x05), // yellow-6
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

bool GraphRenderer::Render(wxRect cell, wxDC* dc, int state) {
    const auto* row = current_row();
    if (row == nullptr) {
        return true;
    }

    // Geometry, in coordinates local to the strip painted below.
    //
    // Straight runs may overhang into the neighbouring rows — whatever padding
    // the port leaves around the cell rect gets covered from both sides.
    // Curves must not: they have to reach their lane exactly at the row
    // boundary, or the next row picks the line up at the wrong x.
    const int half = std::max(cell.GetHeight(), kRowHeight) / 2 + 1;
    const int overhang = kRowHeight / 2;
    const int width = std::max(1, cell.GetWidth());
    const int height = 2 * (half + overhang);
    const double middle = half + overhang;
    const double top = middle - half;
    const double bottom = middle + half;
    const double dot_x = lane_x(row->lane);

    // The overhang exists so a lane meets its continuation in the next row
    // across whatever padding the port leaves around a cell. Above the first
    // row and below the last there is no continuation, and painting there puts
    // ink outside the rows — visible as a stray mark when the view is pulled
    // past its end.
    const bool first_row = row_index_ == 0;
    const bool last_row = rows_ != nullptr &&
                          static_cast<std::size_t>(row_index_) + 1 == rows_->size();
    const double run_top = first_row ? top : 0.0;
    const double run_bottom = last_row ? bottom : static_cast<double>(height);

    std::vector<Line> lines;
    std::vector<Curve> curves;

    for (const auto& segment : row->pass) {
        const double from_x = lane_x(segment.from);
        const double to_x = lane_x(segment.to);
        if (from_x == to_x) {
            lines.push_back({from_x, run_top, run_bottom, lane_colour(segment.color)});
        } else {
            curves.push_back({from_x, top, to_x, bottom, lane_colour(segment.color),
                              CurveKind::Crossing});
        }
    }
    for (const auto& edge : row->children_in) {
        const double x = lane_x(edge.lane);
        if (x == dot_x) {
            // Stops at the dot: a lane that ends here must not sprout a stub.
            lines.push_back({x, run_top, middle, lane_colour(edge.color)});
        } else {
            curves.push_back({x, top, dot_x, middle, lane_colour(edge.color),
                              CurveKind::IntoDot});
        }
    }
    for (const auto& edge : row->parents_out) {
        const double x = lane_x(edge.lane);
        if (x == dot_x) {
            lines.push_back({x, middle, run_bottom, lane_colour(edge.color)});
        } else {
            curves.push_back({dot_x, middle, x, bottom, lane_colour(edge.color),
                              CurveKind::FromDot});
        }
    }

    // Painted offscreen through a graphics context, because the DataView cell
    // DC hands out neither a wxGCDC nor a context of its own on every port —
    // drawing straight onto it gives aliased, visibly stepped diagonals.
    // wxGraphicsContext::Create(wxMemoryDC&) is supported everywhere, so the
    // strip is composited with antialiasing and then blitted over the row.
    // A selected row is painted in the system highlight colour, which a lane
    // of a similar hue vanishes into — the blue mainline against a blue
    // selection being the obvious case. Lines and dots are laid down over a
    // wider stroke in the highlight's own text colour first, so every lane
    // keeps its branch colour and stays legible whatever is behind it.
    const bool selected = (state & wxDATAVIEW_CELL_SELECTED) != 0;
    const wxColour casing = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT);
    const double casing_width = kLineWidth + 2.0;

    wxBitmap strip(width, height, 32);
    strip.UseAlpha();
    bool painted = false;
    {
        wxMemoryDC memory(strip);
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(memory));
        if (gc) {
            painted = true;
            gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
            // Start from fully transparent so the row's own background — and
            // its selection highlight — shows through everywhere we do not
            // draw.
            gc->SetCompositionMode(wxCOMPOSITION_SOURCE);
            gc->SetBrush(wxBrush(wxColour(0, 0, 0, wxALPHA_TRANSPARENT)));
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->DrawRectangle(0, 0, width, height);
            gc->SetCompositionMode(wxCOMPOSITION_OVER);

            for (const auto& line : lines) {
                if (selected) {
                    gc->SetPen(wxPen(casing, static_cast<int>(casing_width)));
                    gc->StrokeLine(line.x, line.y1, line.x, line.y2);
                }
                gc->SetPen(graph_pen(line.colour));
                gc->StrokeLine(line.x, line.y1, line.x, line.y2);
            }
            for (const auto& curve : curves) {
                if (style_ == GraphStyle::Angular) {
                    // Orthogonal routing: the line only ever runs straight up,
                    // down or across, turning through square corners. Which leg
                    // comes first is fixed by the boundary — whichever end sits
                    // on a row edge has to arrive vertically, or the next row
                    // resumes the lane at the wrong x.
                    wxGraphicsPath path = gc->CreatePath();
                    path.MoveToPoint(curve.x1, curve.y1);
                    switch (curve.kind) {
                    case CurveKind::FromDot:
                        // Out of the dot sideways, then down into the lane.
                        path.AddLineToPoint(curve.x2, curve.y1);
                        break;
                    case CurveKind::IntoDot:
                        // Down the lane, then across into the dot.
                        path.AddLineToPoint(curve.x1, curve.y2);
                        break;
                    case CurveKind::Crossing: {
                        // Down, across, and down again — both ends upright.
                        const double mid = (curve.y1 + curve.y2) / 2.0;
                        path.AddLineToPoint(curve.x1, mid);
                        path.AddLineToPoint(curve.x2, mid);
                        break;
                    }
                    }
                    path.AddLineToPoint(curve.x2, curve.y2);
                    if (selected) {
                        wxPen halo(casing, static_cast<int>(casing_width));
                        halo.SetJoin(wxJOIN_MITER);
                        gc->SetPen(halo);
                        gc->StrokePath(path);
                    }
                    wxPen pen = graph_pen(curve.colour);
                    pen.SetJoin(wxJOIN_MITER); // square corners, not rounded
                    gc->SetPen(pen);
                    gc->StrokePath(path);
                    continue;
                }
                // A control point placed straight below its anchor holds the
                // tangent upright there; offsetting it sideways lets the line
                // lean into the dot. Every end that lands on a row boundary
                // keeps the upright one, so the curve meets the straight run in
                // the next row head-on.
                const double dx = curve.x2 - curve.x1;
                const double dy = curve.y2 - curve.y1;
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
                wxGraphicsPath path = gc->CreatePath();
                path.MoveToPoint(curve.x1, curve.y1);
                path.AddCurveToPoint(c1x, c1y, c2x, c2y, curve.x2, curve.y2);
                if (selected) {
                    gc->SetPen(wxPen(casing, static_cast<int>(casing_width)));
                    gc->StrokePath(path);
                }
                gc->SetPen(graph_pen(curve.colour));
                gc->StrokePath(path);
            }

            // The dot goes on last so it covers the line ends it should.
            const double radius = row->is_merge ? kMergeDotRadius : kDotRadius;
            const wxColour dot_colour = lane_colour(row->color);
            if (selected) {
                gc->SetBrush(wxBrush(casing));
                gc->SetPen(wxPen(casing, 1));
                const double outer = radius + 1.5;
                gc->DrawEllipse(dot_x - outer, middle - outer, outer * 2, outer * 2);
            }
            gc->SetBrush(wxBrush(dot_colour));
            gc->SetPen(wxPen(dot_colour, 1));
            gc->DrawEllipse(dot_x - radius, middle - radius, radius * 2, radius * 2);
        }
    }

    const int origin_y = cell.GetTop() + cell.GetHeight() / 2 - (half + overhang);
    if (painted) {
        dc->DrawBitmap(strip, cell.GetLeft(), origin_y, /*useMask=*/true);
        return true;
    }

    // No graphics context anywhere: fall back to aliased drawing rather than
    // showing nothing.
    for (const auto& line : lines) {
        dc->SetPen(graph_pen(line.colour));
        dc->DrawLine(cell.GetLeft() + static_cast<int>(line.x), origin_y + static_cast<int>(line.y1),
                     cell.GetLeft() + static_cast<int>(line.x),
                     origin_y + static_cast<int>(line.y2));
    }
    for (const auto& curve : curves) {
        dc->SetPen(graph_pen(curve.colour));
        dc->DrawLine(cell.GetLeft() + static_cast<int>(curve.x1),
                     origin_y + static_cast<int>(curve.y1),
                     cell.GetLeft() + static_cast<int>(curve.x2),
                     origin_y + static_cast<int>(curve.y2));
    }
    const int radius = row->is_merge ? static_cast<int>(kMergeDotRadius)
                                     : static_cast<int>(kDotRadius);
    dc->SetBrush(wxBrush(lane_colour(row->color)));
    dc->SetPen(wxPen(lane_colour(row->color), 1));
    dc->DrawCircle(cell.GetLeft() + static_cast<int>(dot_x),
                   origin_y + static_cast<int>(middle), radius);
    return true;
}

} // namespace repomancer::gui
