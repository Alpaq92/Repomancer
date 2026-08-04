// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#pragma once

#include <repomancer/vcs/model.h>

#include <wx/dataview.h>

#include <vector>

// Virtual list model over the commit log (implementation-plan.md §4.2).
// Row data stays in core model types; this class only renders.
class CommitLogModel : public wxDataViewVirtualListModel {
public:
    enum Column {
        Col_Subject = 0,
        Col_Author,
        Col_Date,
        Col_Hash,
        Col_Count,
    };

    CommitLogModel() : wxDataViewVirtualListModel(0) {}

    void ReplaceAll(std::vector<repomancer::vcs::Commit> commits);

    unsigned int GetColumnCount() const override { return Col_Count; }
    wxString GetColumnType(unsigned int) const override { return "string"; }
    void GetValueByRow(wxVariant& variant, unsigned int row, unsigned int col) const override;
    bool SetValueByRow(const wxVariant&, unsigned int, unsigned int) override { return false; }

private:
    std::vector<repomancer::vcs::Commit> commits_;
};
