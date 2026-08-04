// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#pragma once

#include <repomancer/vcs/graph.h>
#include <repomancer/vcs/model.h>

#include <wx/dataview.h>

#include <vector>

// Virtual list model over the commit log (implementation-plan.md §4.2).
// Row data stays in core model types; this class only renders.
class CommitLogModel : public wxDataViewVirtualListModel {
public:
    enum Column {
        Col_Graph = 0,
        Col_Subject,
        Col_Author,
        Col_Date,
        Col_Hash,
        Col_Count,
    };

    CommitLogModel() : wxDataViewVirtualListModel(0) {}

    void ReplaceAll(std::vector<repomancer::vcs::Commit> commits);

    [[nodiscard]] const std::vector<repomancer::vcs::GraphRow>& graph_rows() const {
        return graph_.rows;
    }
    [[nodiscard]] int max_lanes() const { return graph_.max_lanes; }
    [[nodiscard]] const repomancer::vcs::Commit* commit_at(unsigned int row) const;

    unsigned int GetColumnCount() const override { return Col_Count; }
    wxString GetColumnType(unsigned int col) const override {
        return col == Col_Graph ? "long" : "string";
    }
    void GetValueByRow(wxVariant& variant, unsigned int row, unsigned int col) const override;
    bool SetValueByRow(const wxVariant&, unsigned int, unsigned int) override { return false; }

private:
    std::vector<repomancer::vcs::Commit> commits_;
    repomancer::vcs::GraphLayout graph_;
};
