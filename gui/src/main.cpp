// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "main_frame.h"
#include "theme.h"

#include <repomancer/settings.h>

#include <wx/app.h>
#include <wx/cmdline.h>
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
        if (!repo_path_.empty()) {
            frame->OpenRepository(repo_path_);
        }
        return true;
    }

    void OnInitCmdLine(wxCmdLineParser& parser) override {
        wxApp::OnInitCmdLine(parser);
        parser.AddParam(_("repository path"), wxCMD_LINE_VAL_STRING,
                        wxCMD_LINE_PARAM_OPTIONAL);
    }

    bool OnCmdLineParsed(wxCmdLineParser& parser) override {
        if (!wxApp::OnCmdLineParsed(parser)) {
            return false;
        }
        if (parser.GetParamCount() > 0) {
            repo_path_ = parser.GetParam(0);
        }
        return true;
    }

private:
    wxLocale locale_;
    wxString repo_path_;
};

wxIMPLEMENT_APP(RepomancerApp);
