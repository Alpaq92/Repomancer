// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "main_frame.h"
#include "theme.h"

#include <repomancer/settings.h>

#include <wx/app.h>
#include <wx/intl.h>

class RepomancerApp : public wxApp {
public:
    bool OnInit() override {
        if (!wxApp::OnInit()) {
            return false;
        }
        SetAppName("Repomancer");
        SetVendorName("Repomancer");

        // Theme must be applied before the first window exists (MSW
        // constraint); the choice persists in settings.json.
        const auto settings = repomancer::load_settings();
        repomancer::gui::init_theme_support();
        repomancer::gui::apply_theme(
            repomancer::gui::theme_mode_from_string(settings.theme));

        // Catalogs ship later (implementation-plan.md §4.3); initializing the
        // locale now keeps date/number formatting correct from day one.
        locale_.Init(wxLANGUAGE_DEFAULT, wxLOCALE_DONT_LOAD_DEFAULT);

        auto* frame = new MainFrame();
        frame->Show();
        return true;
    }

private:
    wxLocale locale_;
};

wxIMPLEMENT_APP(RepomancerApp);
