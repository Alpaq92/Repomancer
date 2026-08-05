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

int graph_strip_width(int lanes) { return kMargin * 2 + std::max(1, lanes) * kLaneWidth; }

// The strip is taller than the row: it carries half a row of transparent
// overhang above and below so straight runs meet across row boundaries.
// Callers with adjacent rows must clip the blit to the row band, or the
// overhang wipes the neighbouring rows' curve tails.
wxBitmap render_graph_strip(const repomancer::vcs::GraphRow& row_data, bool first_row,
                            bool last_row, int width, int row_height, bool selected,
                            GraphStyle style) {
    width = std::max(1, width);
    const repomancer::vcs::GraphRow* row = &row_data;
    const int half = std::max(row_height, kRowHeight) / 2 + 1;
    const int overhang = kRowHeight / 2;
    const int height = 2 * (half + overhang);
    const double middle = half + overhang;
    const double top = middle - half;
    const double bottom = middle + half;
    const double dot_x = lane_x(row->lane);

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

    // Painted offscreen through a graphics context, because a plain DC gives
    // aliased, visibly stepped diagonals. A selected row is painted in the
    // system highlight colour, which a lane of a similar hue vanishes into —
    // lines and dots are laid down over a wider casing stroke in the
    // highlight's own text colour first, so every lane keeps its branch
    // colour and stays legible whatever is behind it.
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
                if (style == GraphStyle::Angular) {
                    // Orthogonal routing with square corners; whichever end
                    // sits on a row edge arrives vertically, or the next row
                    // resumes the lane at the wrong x.
                    wxGraphicsPath path = gc->CreatePath();
                    path.MoveToPoint(curve.x1, curve.y1);
                    switch (curve.kind) {
                    case CurveKind::FromDot:
                        path.AddLineToPoint(curve.x2, curve.y1);
                        break;
                    case CurveKind::IntoDot:
                        path.AddLineToPoint(curve.x1, curve.y2);
                        break;
                    case CurveKind::Crossing: {
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
                // A control point straight below its anchor holds the tangent
                // upright; offsetting it sideways lets the line lean into the
                // dot. Row-boundary ends keep the upright one.
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
    return painted ? strip : wxNullBitmap;
}

} // namespace repomancer::gui
