// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Ported from wx-notepad-plus-plus's gtk_native.cpp (same author); the
// comments carry that file's hard-won findings.

#ifdef __WXGTK__

#include "gtk_header_bar.h"

#include <gtk/gtk.h>

namespace repomancer::gui {

namespace {

// Recursively transparent-ise the moved panel's containers (a wxPanel can
// otherwise paint an opaque window-background rectangle over the header-bar
// theme) and flatten its buttons so they read as header-bar buttons.
// Widget-scoped providers don't cascade to children, so each node's own
// context is touched.
void style_subtree(GtkWidget* w, gpointer provider) {
    if (w == nullptr) {
        return;
    }
    if (GTK_IS_BUTTON(w)) {
        gtk_style_context_add_class(gtk_widget_get_style_context(w), "flat");
    } else {
        gtk_style_context_add_provider(gtk_widget_get_style_context(w),
                                       GTK_STYLE_PROVIDER(provider), G_MAXUINT);
    }
    if (GTK_IS_CONTAINER(w)) {
        gtk_container_forall(GTK_CONTAINER(w),
                             reinterpret_cast<GtkCallback>(style_subtree), provider);
    }
}

} // namespace

void host_in_header_bar(void* gtk_window, void* panel_widget, int bar_width_px,
                        int bar_height_px, bool sharp_corners) {
    if (gtk_window == nullptr || panel_widget == nullptr) {
        return;
    }
    GtkWidget* hb = gtk_window_get_titlebar(GTK_WINDOW(gtk_window));
    if (hb == nullptr || !GTK_IS_HEADER_BAR(hb)) {
        return;
    }
    GtkWidget* child = GTK_WIDGET(panel_widget);

    // Real theme min/max/close on the right (decoration_layout left at the
    // desktop default, so it honours gtk-decoration-layout).
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hb), TRUE);
    gtk_header_bar_set_has_subtitle(GTK_HEADER_BAR(hb), FALSE);
    // An EMPTY custom-title label replaces the default title label AND
    // collapses the header bar's reserved centre area to ~0 — that centre
    // reservation would otherwise squeeze the menu row. The taskbar title is
    // unaffected.
    gtk_header_bar_set_custom_title(GTK_HEADER_BAR(hb), gtk_label_new(""));

    // Compact the bar: a themed header bar defaults to ~46px. Scope the
    // compaction to THIS header bar with a style class + a screen-wide
    // provider (only a screen-wide provider's selectors reach the header's
    // own titlebutton child nodes; the class keeps it off other header bars).
    gtk_style_context_add_class(gtk_widget_get_style_context(hb), "rm-hb");
    // Corner style, scoped to this window via a class. wxbf's decoration CSS
    // forces border-radius:0; against the theme's rounded header bar that
    // reads as odd corners. Default: round decoration AND header bar alike;
    // sharp: force both square.
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(gtk_window)),
                                sharp_corners ? "rm-hbsharp" : "rm-hbround");
    static gboolean s_compact_added = FALSE;
    if (!s_compact_added) {
        GtkCssProvider* compact = gtk_css_provider_new();
        gtk_css_provider_load_from_data(
            compact,
            "headerbar.rm-hb { min-height: 0; padding-top: 0; padding-bottom: 0; }"
            // Keep a little padding so the theme's hover/focus feedback still
            // has room; min-height 24 (< the panel height, which drives the
            // bar) removes the theme's tall-button floor.
            "headerbar.rm-hb button.titlebutton { min-height: 24px; min-width: 24px; "
            "padding: 2px; margin: 0; }"
            // The menu buttons' size is wx's business; clear the theme's own
            // floors so wx is the sole authority.
            "headerbar.rm-hb button:not(.titlebutton) { min-height: 0; padding: 0; "
            "margin: 0; }"
            // Rounded (default): match the platform's header rounding; square
            // when maximized/tiled/fullscreen, where the window fills to a
            // hard edge. The per-edge tiled-* classes matter on Muffin.
            ".rm-hbround decoration { border-radius: 8px 8px 0 0; "
            "border-color: rgba(0,0,0,0.18); }"
            ".rm-hbround headerbar.rm-hb { border-radius: 8px 8px 0 0; }"
            ".rm-hbround.maximized decoration, .rm-hbround.fullscreen decoration,"
            " .rm-hbround.tiled decoration, .rm-hbround.tiled-top decoration,"
            " .rm-hbround.tiled-right decoration, .rm-hbround.tiled-bottom decoration,"
            " .rm-hbround.tiled-left decoration,"
            " .rm-hbround.maximized headerbar.rm-hb, .rm-hbround.fullscreen headerbar.rm-hb,"
            " .rm-hbround.tiled headerbar.rm-hb, .rm-hbround.tiled-top headerbar.rm-hb,"
            " .rm-hbround.tiled-right headerbar.rm-hb,"
            " .rm-hbround.tiled-bottom headerbar.rm-hb,"
            " .rm-hbround.tiled-left headerbar.rm-hb { border-radius: 0; }"
            // Sharp: keep every corner square.
            ".rm-hbsharp decoration { border-radius: 0; }"
            ".rm-hbsharp headerbar.rm-hb { border-radius: 0; }",
            -1, nullptr);
        if (GdkScreen* screen = gdk_screen_get_default()) {
            gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(compact),
                                                      G_MAXUINT);
        }
        g_object_unref(compact);
        s_compact_added = TRUE;
    }

    // Move the whole panel as one unit to the header bar's start. Ref across
    // the reparent so the remove doesn't drop the last reference.
    GtkWidget* old_parent = gtk_widget_get_parent(child);
    g_object_ref(child);
    if (old_parent != nullptr) {
        gtk_container_remove(GTK_CONTAINER(old_parent), child);
    }
    // hexpand is REQUIRED: wxPizza reports natural width 0, and GTK would
    // allocate the panel zero width without it. The explicit size request
    // gives the menu row its real width and keeps the bar compact.
    gtk_widget_set_hexpand(child, TRUE);
    gtk_widget_set_size_request(child, bar_width_px > 0 ? bar_width_px : -1,
                                bar_height_px > 0 ? bar_height_px : 30);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), child);
    g_object_unref(child);

    GtkCssProvider* prov = gtk_css_provider_new();
    gtk_css_provider_load_from_data(
        prov, "* { background-color: transparent; background-image: none; }", -1,
        nullptr);
    style_subtree(child, prov);
    g_object_unref(prov); // each context we added it to holds its own reference

    gtk_widget_show_all(hb);
}

} // namespace repomancer::gui

#endif
