// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "title_bar.h"

#include "theme.h"

#include <wx/button.h>
#include <wx/dcclient.h>
#include <wx/menu.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/toplevel.h>

#ifdef REPOMANCER_HAVE_WXBF
#include <wxbf/window_gripper.h>
#endif

#include <memory>

namespace repomancer::gui {

namespace {

// One caption control — minimize, maximize/restore or close — drawn by hand
// so wx owns every pixel: a flat rect in the bar colour, the glyph in thin
// lines, a hover fill (red for close, the VS convention).
class CaptionButton : public wxWindow {
public:
    enum class Kind { Minimize, Maximize, Close };

    CaptionButton(wxWindow* parent, Kind kind, bool rounded)
        : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(46, TitleBar::kHeight)),
          kind_(kind),
          rounded_(rounded) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &CaptionButton::OnPaint, this);
        Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) {
            hover_ = true;
            Refresh();
        });
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
            hover_ = false;
            Refresh();
        });
        Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) {
            if (!GetClientRect().Contains(event.GetPosition())) {
                return;
            }
            auto* top = static_cast<wxTopLevelWindow*>(wxGetTopLevelParent(this));
            switch (kind_) {
            case Kind::Minimize:
                top->Iconize();
                break;
            case Kind::Maximize:
                top->Maximize(!top->IsMaximized());
                break;
            case Kind::Close:
                top->Close();
                break;
            }
        });
        // The press must not fall through to the bar's drag handler.
        Bind(wxEVT_LEFT_DOWN, [](wxMouseEvent&) {});
    }

private:
    void OnPaint(wxPaintEvent&) {
        wxPaintDC dc(this);
        const wxColour bar = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
        const wxColour fg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
        const bool close = kind_ == Kind::Close;
        wxColour bg = bar;
        wxColour glyph = fg;
        if (hover_) {
            bg = close ? wxColour(232, 17, 35) : blend_colour(fg, bar, 15);
            if (close) {
                glyph = *wxWHITE;
            }
        }
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(bar));
        dc.DrawRectangle(GetClientRect());
        dc.SetBrush(wxBrush(bg));
        if (rounded_) {
            // Opera-style: the hover is a pill floating in the bar.
            const wxRect rect = GetClientRect();
            const int d = rect.GetHeight() - 6;
            dc.DrawEllipse(rect.GetWidth() / 2 - d / 2, 3, d, d);
        } else {
            dc.DrawRectangle(GetClientRect());
        }

        const wxRect rect = GetClientRect();
        const int cx = rect.GetWidth() / 2;
        const int cy = rect.GetHeight() / 2;
        const int r = 5; // half the glyph box
        dc.SetPen(wxPen(glyph, 1));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        auto* top = static_cast<wxTopLevelWindow*>(wxGetTopLevelParent(this));
        switch (kind_) {
        case Kind::Minimize:
            dc.DrawLine(cx - r, cy, cx + r + 1, cy);
            break;
        case Kind::Maximize:
            if (top->IsMaximized()) {
                // Restore: two offset squares.
                dc.DrawRectangle(cx - r, cy - r + 2, 2 * r - 2, 2 * r - 2);
                dc.SetPen(wxPen(glyph, 1));
                dc.DrawLine(cx - r + 2, cy - r + 2, cx - r + 2, cy - r);
                dc.DrawLine(cx - r + 2, cy - r, cx + r, cy - r);
                dc.DrawLine(cx + r, cy - r, cx + r, cy + r - 4);
                dc.DrawLine(cx + r, cy + r - 4, cx + r - 2, cy + r - 4);
            } else {
                dc.DrawRectangle(cx - r, cy - r, 2 * r + 1, 2 * r + 1);
            }
            break;
        case Kind::Close:
            dc.DrawLine(cx - r, cy - r, cx + r, cy + r);
            dc.DrawLine(cx - r, cy + r, cx + r, cy - r);
            break;
        }
    }

    Kind kind_;
    bool rounded_ = false;
    bool hover_ = false;
};

} // namespace

TitleBar::Buttons TitleBar::FromSetting(int setting) {
#ifdef __WXGTK__
    if (setting == 0) {
        return Buttons::Native;
    }
#endif
    return setting == 2 ? Buttons::Rounded : Buttons::Flat;
}

TitleBar::TitleBar(wxWindow* frame, std::vector<std::pair<wxString, wxMenu*>> menus,
                   Buttons buttons)
    : wxPanel(frame, wxID_ANY, wxDefaultPosition, wxSize(-1, kHeight)) {
    const bool native = buttons == Buttons::Native;
    const wxColour bar = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
    const wxColour fg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    if (!native) {
        // Native mode: the header bar's theme shows through instead.
        SetBackgroundColour(bar);
    }

    auto* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->AddSpacer(4);

    for (auto& [label, menu] : menus) {
        auto* button = new wxButton(this, wxID_ANY, label, wxDefaultPosition,
                                    wxDefaultSize, wxBU_EXACTFIT | wxBORDER_NONE);
        if (!native) {
            button->SetBackgroundColour(bar);
            button->SetForegroundColour(fg);
        }
        // The air lives inside the button so its hover highlight has padding
        // (wx-notepad-plus-plus's finding: a sizer border leaves the
        // highlight hugging the text).
        button->SetMinSize(wxSize(button->GetBestSize().x + FromDIP(16), -1));
        wxMenu* popup = menu;
        button->Bind(wxEVT_BUTTON, [this, button, popup](wxCommandEvent&) {
            wxWindow* top = wxGetTopLevelParent(this);
            const wxPoint at = top->ScreenToClient(
                ClientToScreen(button->GetPosition() + wxPoint(0, button->GetSize().y)));
            top->PopupMenu(popup, at);
        });
        sizer->Add(button, 0, wxALIGN_CENTRE_VERTICAL);
    }

    sizer->AddStretchSpacer(1); // the empty middle stays draggable

    if (!native) {
        const bool rounded = buttons == Buttons::Rounded;
        sizer->Add(new CaptionButton(this, CaptionButton::Kind::Minimize, rounded), 0,
                   wxEXPAND);
        auto* max_button =
            new CaptionButton(this, CaptionButton::Kind::Maximize, rounded);
        sizer->Add(max_button, 0, wxEXPAND);
        sizer->Add(new CaptionButton(this, CaptionButton::Kind::Close, rounded), 0,
                   wxEXPAND);
        // The maximize glyph follows the real state (snap, double-click,
        // WM) — but size events are constant during interactive resizes, so
        // only an actual state flip repaints.
        auto* top = static_cast<wxTopLevelWindow*>(wxGetTopLevelParent(this));
        top->Bind(wxEVT_SIZE,
                  [max_button, top, was = top->IsMaximized()](wxSizeEvent& event) mutable {
                      event.Skip();
                      const bool now = top->IsMaximized();
                      if (now != was) {
                          was = now;
                          max_button->Refresh();
                      }
                  });
    }
    SetSizer(sizer);

    if (!native) {
#ifdef REPOMANCER_HAVE_WXBF
        // Dragging the empty bar moves the window: the gripper begins a
        // native move drag. In native mode the header bar handles all this.
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
            static std::unique_ptr<wxWindowGripper> gripper(wxWindowGripper::Create());
            if (!gripper || !gripper->StartDragMove(wxGetTopLevelParent(this))) {
                event.Skip();
            }
        });
#endif
        Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent&) {
            auto* top = static_cast<wxTopLevelWindow*>(wxGetTopLevelParent(this));
            top->Maximize(!top->IsMaximized());
        });
    }
}

} // namespace repomancer::gui
