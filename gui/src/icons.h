// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Menu icons from Lucide (https://lucide.dev), used under the ISC licence —
// see NOTICE. The sources are embedded as SVG and rendered through
// wxBitmapBundle, so they scale with the display instead of shipping as
// fixed-size pixmaps.

#pragma once

#include <wx/bmpbndl.h>
#include <wx/colour.h>
#include <wx/settings.h>

#include <string>

namespace repomancer::gui::icons {

// lucide "bookmark"
inline constexpr const char* kBookmark = R"svg(<svg
  xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"
  fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"
  stroke-linejoin="round">
  <path d="M17 3a2 2 0 0 1 2 2v15a1 1 0 0 1-1.496.868l-4.512-2.578a2 2 0 0 0-1.984 0l-4.512 2.578A1 1 0 0 1 5 20V5a2 2 0 0 1 2-2z" />
</svg>)svg";

// lucide "folder-git-2"
inline constexpr const char* kFolderGit = R"svg(<svg
  xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"
  fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"
  stroke-linejoin="round">
  <path d="M18 19a5 5 0 0 1-5-5v8" />
  <path d="M9 20H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h3.9a2 2 0 0 1 1.69.9l.81 1.2a2 2 0 0 0 1.67.9H20a2 2 0 0 1 2 2v5" />
  <circle cx="13" cy="12" r="2" />
  <circle cx="20" cy="19" r="2" />
</svg>)svg";

// lucide "square-arrow-right-exit"
inline constexpr const char* kExit = R"svg(<svg
  xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"
  fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"
  stroke-linejoin="round">
  <path d="M10 12h11" />
  <path d="m17 16 4-4-4-4" />
  <path d="M21 6.344V5a2 2 0 0 0-2-2H5a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-1.344" />
</svg>)svg";

// lucide "eclipse"
inline constexpr const char* kEclipse = R"svg(<svg
  xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"
  fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"
  stroke-linejoin="round">
  <circle cx="12" cy="12" r="10" />
  <path d="M12 2a7 7 0 1 0 10 10" />
</svg>)svg";

// lucide "git-pull-request-arrow"
inline constexpr const char* kGitPullRequestArrow = R"svg(<svg
  xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"
  fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"
  stroke-linejoin="round">
  <circle cx="5" cy="6" r="3" />
  <path d="M5 9v12" />
  <circle cx="19" cy="18" r="3" />
  <path d="m15 9-3-3 3-3" />
  <path d="M12 6h5a2 2 0 0 1 2 2v7" />
</svg>)svg";

// A menu-sized bundle of the icon, stroked in the menu's own text colour —
// Lucide sources use `currentColor`, which the SVG rasteriser does not
// resolve by itself.
[[nodiscard]] inline wxBitmapBundle menu_icon(const char* svg) {
    std::string data(svg);
    const wxColour colour = wxSystemSettings::GetColour(wxSYS_COLOUR_MENUTEXT);
    const std::string hex(colour.GetAsString(wxC2S_HTML_SYNTAX).utf8_str());
    const std::string needle = "currentColor";
    for (auto at = data.find(needle); at != std::string::npos;
         at = data.find(needle, at + hex.size())) {
        data.replace(at, needle.size(), hex);
    }
    return wxBitmapBundle::FromSVG(data.c_str(), wxSize(16, 16));
}

} // namespace repomancer::gui::icons
