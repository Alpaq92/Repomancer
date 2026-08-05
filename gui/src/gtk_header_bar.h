// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// GTK native-buttons mode for the integrated title bar: the menu panel is
// hosted inside the window's GtkHeaderBar, which then draws the desktop
// theme's real minimize/maximize/close and handles dragging itself.
// Ported from wx-notepad-plus-plus (same author) — see its src/gtk_native.cpp.

#pragma once

#ifdef __WXGTK__

namespace repomancer::gui {

// Moves `panel`'s GTK widget into `frame`'s header bar (the empty one wxbf
// installed), enables the theme's window buttons, compacts the bar, and — per
// `sharp_corners` — either rounds the top corners to match the platform or
// keeps them square.
void host_in_header_bar(void* gtk_window, void* panel_widget, int bar_width_px,
                        int bar_height_px, bool sharp_corners);

} // namespace repomancer::gui

#endif
