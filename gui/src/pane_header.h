// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// The strip that titles a docked pane. It is a real wxHeaderCtrl, drawn by the
// platform's header renderer, so a pane is labelled by the same kind of
// control that titles the log's columns rather than by something painted to
// resemble one.

#pragma once

#include <wx/headerctrl.h>

#include <utility>

namespace repomancer::gui {

class PaneHeader : public wxHeaderCtrl {
public:
    PaneHeader(wxWindow* parent, wxString title)
        // wxBORDER_SIMPLE outlines the strip: GTK draws a lone header column
        // flat with only a hairline beneath it, which does not read as the
        // same object as the log's outlined column headers.
        : wxHeaderCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                       wxHD_DEFAULT_STYLE | wxBORDER_SIMPLE),
          column_(std::move(title)) {
        SetColumnCount(1);
        // Give the column a width before the first paint, so the strip is
        // drawn as a header button rather than as empty header background.
        column_.SetWidth(parent->GetClientSize().GetWidth());
        // The single column spans the pane, so the title reads as the pane's
        // own header rather than one column of several.
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            event.Skip();
            column_.SetWidth(GetClientSize().GetWidth());
            UpdateColumn(0);
        });
    }

    void SetTitle(const wxString& title) {
        column_.SetTitle(title);
        UpdateColumn(0);
    }

protected:
    const wxHeaderColumn& GetColumn(unsigned int) const override { return column_; }

private:
    wxHeaderColumnSimple column_;
};

} // namespace repomancer::gui
