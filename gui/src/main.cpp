// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "main_frame.h"

#include <wx/app.h>
#include <wx/intl.h>

class RepomancerApp : public wxApp {
public:
    bool OnInit() override {
        if (!wxApp::OnInit()) {
            return false;
        }
#if defined(__WXMSW__) && wxCHECK_VERSION(3, 3, 0)
        MSWEnableDarkMode(DarkMode_Auto);
#endif
        SetAppName("Repomancer");
        SetVendorName("Repomancer");
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
