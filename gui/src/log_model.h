// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#pragma once

#include <repomancer/vcs/graph.h>
#include <repomancer/vcs/model.h>

#include <wx/string.h>

#include <array>
#include <vector>

// Row data for the commit log (implementation-plan.md §4.2): the commits, the
// graph layout, and the display text for every cell, formatted once when the
// history is loaded rather than on every paint.
class CommitLogModel {
public:
    enum Column {
        Col_Graph = 0,
        Col_Subject,
        Col_Author,
        Col_Date,
        Col_Hash,
        Col_Count,
    };

    void ReplaceAll(std::vector<repomancer::vcs::Commit> commits);

    [[nodiscard]] unsigned int GetCount() const {
        return static_cast<unsigned int>(commits_.size());
    }
    [[nodiscard]] const std::vector<repomancer::vcs::GraphRow>& graph_rows() const {
        return graph_.rows;
    }
    [[nodiscard]] int max_lanes() const { return graph_.max_lanes; }
    [[nodiscard]] const repomancer::vcs::Commit* commit_at(unsigned int row) const;

    // Display text for a text column (Col_Subject … Col_Hash), sanitized and
    // formatted at load time.
    [[nodiscard]] const wxString& text(unsigned int row, Column column) const;

private:
    std::vector<repomancer::vcs::Commit> commits_;
    repomancer::vcs::GraphLayout graph_;
    // One entry per row: subject, author, date, hash.
    std::vector<std::array<wxString, Col_Count - Col_Subject>> display_;
};
