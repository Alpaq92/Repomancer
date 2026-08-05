// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// The strip that titles a docked pane.
//
// It is a real table — a wxDataViewListCtrl with one column and no rows,
// clamped to the height of its own header — so the title is drawn by exactly
// the widget that draws the log's column headers. Earlier attempts painted a
// lookalike, first with wxHeaderCtrl and then with the native renderer, and
// each differed somewhere: weight, indent, height or border. Using the same
// control removes the question.

#pragma once

#include <wx/dataview.h>

#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

namespace repomancer::gui {

class PaneHeader : public wxDataViewListCtrl {
public:
    PaneHeader(wxWindow* parent, const wxString& title) : PaneHeader(parent) {
        column_ = AppendTextColumn(title, wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_AUTOSIZE,
                                   wxALIGN_LEFT, 0);
        // The column spans the pane, so the title reads as the pane's own
        // header rather than as one column among several.
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            event.Skip();
            if (column_ != nullptr) {
                column_->SetWidth(GetClientSize().GetWidth());
            }
        });
    }

    // The native header's own height, measured from the realized widget.
    [[nodiscard]] int NativeHeaderHeight() {
#ifdef __WXGTK__
        if (GtkWidget* tree = GtkGetTreeView(); tree != nullptr) {
            int x = 0;
            int y = 0;
            gtk_tree_view_convert_bin_window_to_widget_coords(GTK_TREE_VIEW(tree), 0, 0,
                                                              &x, &y);
            if (y > 0) {
                return y;
            }
        }
#endif
        return GetCharHeight() + 9;
    }

    void ClampToNativeHeader() { SetHeight(NativeHeaderHeight()); }

    void SetTitle(const wxString& title) {
        if (column_ != nullptr) {
            column_->SetTitle(title);
        }
    }

    // Height of the header this has to line up with. Without it the empty
    // table would show its blank body under the title as well.
    void SetHeight(int height) {
        if (height <= 0 || height == height_) {
            return;
        }
        height_ = height;
        SetMinSize(wxSize(-1, height));
        SetMaxSize(wxSize(-1, height));
        InvalidateBestSize();
        if (wxWindow* parent = GetParent()) {
            parent->Layout();
        }
    }

protected:
    wxSize DoGetBestClientSize() const override {
        return wxSize(-1, height_ > 0 ? height_ : GetCharHeight() + 9);
    }

    // Columns for derived headers (the log's strip).
    explicit PaneHeader(wxWindow* parent)
        : wxDataViewListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxBORDER_NONE) {
        // Every header strip clamps itself to its own native header height.
        // Paint events never arrive for native widgets on GTK, so the clamp
        // is queued for after realization and repeated on size changes —
        // both delivered reliably, and the clamp is idempotent. All strips
        // are the same widget with the same font, so they all land on the
        // same height without any external synchronising.
        CallAfter([this] { ClampToNativeHeader(); });
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            event.Skip();
            ClampToNativeHeader();
        });
    }

private:
    wxDataViewColumn* column_ = nullptr;
    int height_ = 0;
};

} // namespace repomancer::gui
