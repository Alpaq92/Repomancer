// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "main_frame.h"
#include "theme.h"

#include <repomancer/settings.h>

#include <wx/app.h>
#include <wx/cmdline.h>
#include <wx/intl.h>

#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

class RepomancerApp : public wxApp {
public:
    bool OnInit() override {
        if (!wxApp::OnInit()) {
            return false;
        }
        SetAppName("Repomancer");
        SetVendorName("Repomancer");

#ifdef __WXGTK__
        // GtkScrolledWindow decorates any edge with more content beyond it
        // with a dashed "undershoot" line. Every pane here scrolls, so the
        // dashes read as broken table borders rather than as a hint; the
        // scrollbars already tell the story.
        GtkCssProvider* css = gtk_css_provider_new();
        gtk_css_provider_load_from_data(
            css,
            "scrolledwindow undershoot.top, scrolledwindow undershoot.bottom,"
            "scrolledwindow undershoot.left, scrolledwindow undershoot.right"
            "{ background-image: none; border: none; }",
            // (Menu-icon vertical alignment is baked into the bitmap in
            // icons.h::menu_icon — sub-pixel and cross-platform, where a
            // whole-pixel CSS margin could only over- or under-shoot.)
            -1, nullptr);
        gtk_style_context_add_provider_for_screen(
            gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(css);
#endif

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
        // A Tier-0 shell menu launches us with --action; the frame runs it
        // once the repository has loaded (or straight away for settings).
        frame->SetStartupAction(action_);
        frame->Show();
        if (!repo_path_.empty()) {
            frame->OpenRepository(repo_path_);
        } else if (action_ == MainFrame::StartupAction::Settings) {
            frame->OpenPreferences();
        }
        return true;
    }

    void OnInitCmdLine(wxCmdLineParser& parser) override {
        wxApp::OnInitCmdLine(parser);
        // Tier-0 shell integration: "Repomancer ▸ Commit / Log / Sync /
        // Settings" invoke us with --action and the folder as the path.
        parser.AddOption("a", "action",
                         _("log | commit | sync | settings (from a shell menu)"),
                         wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
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
        if (wxString action; parser.Found("action", &action)) {
            action_ = MainFrame::StartupActionFromString(std::string(action.utf8_str()));
        }
        return true;
    }

private:
    wxLocale locale_;
    wxString repo_path_;
    MainFrame::StartupAction action_ = MainFrame::StartupAction::None;
};

wxIMPLEMENT_APP(RepomancerApp);
