// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "log_model.h"

#include <wx/datetime.h>

namespace {

// Display sanitization (§13.1): commit data is attacker-influenced; C0
// control characters must never reach a text control verbatim.
wxString sanitized_utf8(const std::string& raw) {
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

} // namespace

void CommitLogModel::ReplaceAll(std::vector<repomancer::vcs::Commit> commits) {
    commits_ = std::move(commits);
    Reset(static_cast<unsigned int>(commits_.size()));
}

void CommitLogModel::GetValueByRow(wxVariant& variant, unsigned int row, unsigned int col) const {
    if (row >= commits_.size()) {
        variant = wxString();
        return;
    }
    const auto& commit = commits_[row];
    switch (col) {
    case Col_Subject:
        variant = sanitized_utf8(commit.subject);
        break;
    case Col_Author:
        variant = sanitized_utf8(commit.author_name);
        break;
    case Col_Date: {
        const wxDateTime when(static_cast<time_t>(commit.commit_time));
        variant = when.Format("%Y-%m-%d %H:%M");
        break;
    }
    case Col_Hash:
        variant = wxString::FromUTF8(commit.hash.substr(0, 10));
        break;
    default:
        variant = wxString();
        break;
    }
}
