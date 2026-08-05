// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Display sanitization (§13.1): VCS data is attacker-influenced; C0 control
// characters must never reach a text control verbatim.

#pragma once

#include <repomancer/vcs/model.h>

#include <wx/string.h>

#include <string>

namespace repomancer::gui {

[[nodiscard]] inline wxString sanitized_utf8(const std::string& raw) {
    std::string clean;
    clean.reserve(raw.size());
    for (const char ch : raw) {
        clean.push_back((static_cast<unsigned char>(ch) < 0x20) ? ' ' : ch);
    }
    wxString out = wxString::FromUTF8(clean.data(), clean.size());
    if (out.empty() && !clean.empty()) {
        // Invalid UTF-8: fall back to a lossy latin-1 view rather than
        // showing nothing.
        out = wxString::From8BitData(clean.data(), clean.size());
    }
    return out;
}

// The most useful line of a failed git run: the tool's own stderr when
// there is one, the driver's summary otherwise.
[[nodiscard]] inline wxString error_text(const repomancer::vcs::VcsError& error) {
    return sanitized_utf8(error.stderr_excerpt.empty() ? error.message
                                                       : error.stderr_excerpt);
}

} // namespace repomancer::gui
