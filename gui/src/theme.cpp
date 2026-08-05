// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "theme.h"

#include <wx/app.h>

#if !wxCHECK_VERSION(3, 3, 0) && defined(__WXGTK__)
#include <gtk/gtk.h>
#endif

namespace repomancer::gui {

wxColour blend_colour(const wxColour& a, const wxColour& b, int pct) {
    return wxColour((a.Red() * pct + b.Red() * (100 - pct)) / 100,
                    (a.Green() * pct + b.Green() * (100 - pct)) / 100,
                    (a.Blue() * pct + b.Blue() * (100 - pct)) / 100);
}

ThemeMode theme_mode_from_string(std::string_view value) {
    if (value == "light") {
        return ThemeMode::Light;
    }
    if (value == "dark") {
        return ThemeMode::Dark;
    }
    return ThemeMode::System;
}

const char* theme_mode_to_string(ThemeMode mode) {
    switch (mode) {
    case ThemeMode::Light:
        return "light";
    case ThemeMode::Dark:
        return "dark";
    case ThemeMode::System:
        break;
    }
    return "system";
}

#if wxCHECK_VERSION(3, 3, 0)

void init_theme_support() {}

bool apply_theme(ThemeMode mode) {
    wxApp::Appearance appearance = wxApp::Appearance::System;
    switch (mode) {
    case ThemeMode::Light:
        appearance = wxApp::Appearance::Light;
        break;
    case ThemeMode::Dark:
        appearance = wxApp::Appearance::Dark;
        break;
    case ThemeMode::System:
        break;
    }
    return wxTheApp->SetAppearance(appearance) == wxApp::AppearanceResult::Ok;
}

#elif defined(__WXGTK__)

namespace {
gboolean g_initial_prefer_dark = FALSE;
bool g_captured = false;
} // namespace

void init_theme_support() {
    if (GtkSettings* settings = gtk_settings_get_default()) {
        g_object_get(settings, "gtk-application-prefer-dark-theme", &g_initial_prefer_dark,
                     nullptr);
        g_captured = true;
    }
}

bool apply_theme(ThemeMode mode) {
    GtkSettings* settings = gtk_settings_get_default();
    if (settings == nullptr) {
        return false;
    }
    gboolean prefer_dark = FALSE;
    switch (mode) {
    case ThemeMode::Dark:
        prefer_dark = TRUE;
        break;
    case ThemeMode::Light:
        prefer_dark = FALSE;
        break;
    case ThemeMode::System:
        prefer_dark = g_captured ? g_initial_prefer_dark : FALSE;
        break;
    }
    g_object_set(settings, "gtk-application-prefer-dark-theme", prefer_dark, nullptr);
    if (GdkScreen* screen = gdk_screen_get_default()) {
        gtk_style_context_reset_widgets(screen);
    }
    // Reporting failure is not pessimism: the preference selects a different
    // theme file, and GTK resolves that when a window is created, so windows
    // that already exist keep the variant they were built with — resetting
    // their style contexts re-reads the same one. Launching in a theme works;
    // switching cannot, so the caller is told a restart is needed rather than
    // being left with a setting that silently did nothing. wx 3.3's
    // SetAppearance handles this properly, and that is the path releases take.
    return false;
}

#else

// wx < 3.3 on MSW/macOS: no supported way to force an appearance.
void init_theme_support() {}

bool apply_theme(ThemeMode mode) { return mode == ThemeMode::System; }

#endif

} // namespace repomancer::gui
