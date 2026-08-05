// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "log_model.h"

#include "text_sanitize.h"

#include <wx/datetime.h>

namespace {

// "HEAD -> main, tag: v1, origin/main" → "main | v1 | origin/main"
wxString format_refs(const std::string& raw) {
    if (raw.empty()) {
        return {};
    }
    wxString text = repomancer::gui::sanitized_utf8(raw);
    text.Replace("HEAD -> ", "");
    text.Replace("tag: ", "");
    text.Replace(", ", " | ");
    return text;
}

} // namespace

void CommitLogModel::ReplaceAll(std::vector<repomancer::vcs::Commit> commits) {
    commits_ = std::move(commits);
    graph_ = repomancer::vcs::compute_graph_layout(commits_);

    display_.clear();
    display_.reserve(commits_.size());
    for (const auto& commit : commits_) {
        const wxString refs = format_refs(commit.refs);
        const wxDateTime when(static_cast<time_t>(commit.commit_time));
        display_.push_back({refs.empty() ? repomancer::gui::sanitized_utf8(commit.subject)
                                         : "[" + refs + "] " + repomancer::gui::sanitized_utf8(commit.subject),
                            repomancer::gui::sanitized_utf8(commit.author_name),
                            when.Format("%Y-%m-%d %H:%M"),
                            wxString::FromUTF8(commit.hash.substr(0, 10))});
    }
}

const repomancer::vcs::Commit* CommitLogModel::commit_at(unsigned int row) const {
    return row < commits_.size() ? &commits_[row] : nullptr;
}

const wxString& CommitLogModel::text(unsigned int row, Column column) const {
    static const wxString empty;
    if (row >= display_.size() || column < Col_Subject || column >= Col_Count) {
        return empty;
    }
    return display_[row][static_cast<std::size_t>(column - Col_Subject)];
}
