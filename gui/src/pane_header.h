// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// The strip that titles a docked pane.
//
// It paints itself with wxRendererNative::DrawHeaderButton — the very call the
// list control uses for its own column headers — so a pane title and a column
// title are the same drawing, not two things adjusted until they look alike. A
// wxHeaderCtrl was the obvious choice but takes a different path on GTK and
// comes out flat, so the two never matched.

#pragma once

#include <wx/control.h>
#include <wx/dcbuffer.h>
#include <wx/renderer.h>
#include <wx/settings.h>

#include <utility>

namespace repomancer::gui {

class PaneHeader : public wxControl {
public:
    PaneHeader(wxWindow* parent, wxString title)
        : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
          title_(std::move(title)) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &PaneHeader::OnPaint, this);
    }

    void SetTitle(const wxString& title) {
        title_ = title;
        Refresh();
    }

protected:
    wxSize DoGetBestClientSize() const override {
        return wxSize(-1, wxRendererNative::Get().GetHeaderButtonHeight(
                              const_cast<PaneHeader*>(this)));
    }

private:
    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
        dc.Clear();

        wxHeaderButtonParams params;
        params.m_labelText = title_;
        params.m_labelAlignment = wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL;
        params.m_labelColour = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);

        wxRect rect(GetClientSize());
        wxRendererNative::Get().DrawHeaderButton(this, dc, rect, 0, wxHDR_SORT_ICON_NONE,
                                                 &params);
    }

    wxString title_;
};

} // namespace repomancer::gui
