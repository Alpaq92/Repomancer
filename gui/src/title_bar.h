// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// The integrated top bar (user requirement, modelled on wx-notepad-plus-plus):
// menu buttons, a draggable stretch, and app-drawn minimize/maximize/close,
// all in one strip that replaces the native title bar over wxbf's borderless
// frame.

#pragma once

#include <wx/panel.h>

#include <utility>
#include <vector>

class wxMenu;

namespace repomancer::gui {

class TitleBar : public wxPanel {
public:
    static constexpr int kHeight = 30;

    // How the window controls are drawn — mirrors wx-notepad-plus-plus.
    enum class Buttons {
        Native,  // GTK header bar draws the theme's own; the bar holds menus only
        Flat,    // app-drawn flat rectangles (default)
        Rounded, // app-drawn with round hover pills
    };

    // The Preferences integer (0 native, 1 flat, 2 rounded) as a Buttons
    // value; the platform fallback lives here — native buttons exist on GTK
    // only, elsewhere the choice falls back to flat.
    static Buttons FromSetting(int setting);

    // `menus` stay owned by the caller and must outlive the bar. Labels are
    // shown on the buttons as given.
    TitleBar(wxWindow* frame, std::vector<std::pair<wxString, wxMenu*>> menus,
             Buttons buttons = Buttons::Flat);
};

} // namespace repomancer::gui
