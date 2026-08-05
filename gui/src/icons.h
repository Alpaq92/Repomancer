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

// lucide "settings"
inline constexpr const char* kSettings = R"svg(<svg
  xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"
  fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"
  stroke-linejoin="round">
  <path d="M9.671 4.136a2.34 2.34 0 0 1 4.659 0 2.34 2.34 0 0 0 3.319 1.915 2.34 2.34 0 0 1 2.33 4.033 2.34 2.34 0 0 0 0 3.831 2.34 2.34 0 0 1-2.33 4.033 2.34 2.34 0 0 0-3.319 1.915 2.34 2.34 0 0 1-4.659 0 2.34 2.34 0 0 0-3.32-1.915 2.34 2.34 0 0 1-2.33-4.033 2.34 2.34 0 0 0 0-3.831A2.34 2.34 0 0 1 6.35 6.051a2.34 2.34 0 0 0 3.319-1.915" />
  <circle cx="12" cy="12" r="3" />
</svg>)svg";

// lucide "file"
inline constexpr const char* kFile = R"svg(<svg
  xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"
  fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"
  stroke-linejoin="round">
  <path d="M6 22a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h8a2.4 2.4 0 0 1 1.704.706l3.588 3.588A2.4 2.4 0 0 1 20 8v12a2 2 0 0 1-2 2z" />
  <path d="M14 2v5a1 1 0 0 0 1 1h5" />
</svg>)svg";

// lucide "folder"
inline constexpr const char* kFolder = R"svg(<svg
  xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"
  fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"
  stroke-linejoin="round">
  <path d="M20 20a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-7.9a2 2 0 0 1-1.69-.9L9.6 3.9A2 2 0 0 0 7.93 3H4a2 2 0 0 0-2 2v13a2 2 0 0 0 2 2Z" />
</svg>)svg";

// lucide "search"
inline constexpr const char* kSearch = R"svg(<svg
  xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"
  fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"
  stroke-linejoin="round">
  <path d="m21 21-4.34-4.34" />
  <circle cx="11" cy="11" r="8" />
</svg>)svg";

// lucide "rotate-cw"
inline constexpr const char* kRotateCw = R"svg(<svg
  xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"
  fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"
  stroke-linejoin="round">
  <path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8" />
  <path d="M21 3v5h-5" />
</svg>)svg";

// lucide "folder-clock"
inline constexpr const char* kFolderClock = R"svg(<svg
  xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"
  fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"
  stroke-linejoin="round">
  <path d="M16 14v2.2l1.6 1" />
  <path d="M7 20H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h3.9a2 2 0 0 1 1.69.9l.81 1.2a2 2 0 0 0 1.67.9H20a2 2 0 0 1 2 2" />
  <circle cx="16" cy="16" r="6" />
</svg>)svg";

// lucide "clock-4"
inline constexpr const char* kClock4 = R"svg(<svg
  xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"
  fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"
  stroke-linejoin="round">
  <circle cx="12" cy="12" r="10" />
  <path d="M12 6v6l4 2" />
</svg>)svg";

// lucide "square-arrow-right-enter"
inline constexpr const char* kSquareArrowRightEnter = R"svg(<svg
  xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"
  fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"
  stroke-linejoin="round">
  <path d="m10 16 4-4-4-4" />
  <path d="M3 12h11" />
  <path d="M3 8V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-3" />
</svg>)svg";

// lucide "git-branch-plus"
inline constexpr const char* kGitBranchPlus = R"svg(<svg
  xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"
  fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"
  stroke-linejoin="round">
  <path d="M6 3v12" />
  <path d="M18 9a3 3 0 1 0 0-6 3 3 0 0 0 0 6z" />
  <path d="M6 21a3 3 0 1 0 0-6 3 3 0 0 0 0 6z" />
  <path d="M15 6a9 9 0 0 0-9 9" />
  <path d="M18 15v6" />
  <path d="M21 18h-6" />
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
