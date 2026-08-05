// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/vcs/patch.h>

namespace repomancer::vcs {

bool supports_hunk_ops(const FileDiff& file) {
    return !file.is_binary && !file.old_path.empty() && !file.new_path.empty() &&
           file.old_path == file.new_path;
}

std::string single_hunk_patch(const FileDiff& file, std::size_t hunk_index) {
    if (!supports_hunk_ops(file) || hunk_index >= file.hunks.size()) {
        return {};
    }
    const DiffHunk& hunk = file.hunks[hunk_index];

    std::string patch;
    patch.reserve(64 + hunk.lines.size() * 40);
    patch += "--- a/" + file.old_path + "\n";
    patch += "+++ b/" + file.new_path + "\n";

    // The original @@ numbers stay: they are hints, and `git apply` anchors
    // on the context lines, offsetting as needed.
    patch += "@@ -" + std::to_string(hunk.old_start) + "," +
             std::to_string(hunk.old_count) + " +" + std::to_string(hunk.new_start) +
             "," + std::to_string(hunk.new_count) + " @@";
    if (!hunk.heading.empty()) {
        patch += " " + hunk.heading;
    }
    patch += "\n";

    for (const DiffLine& line : hunk.lines) {
        switch (line.kind) {
        case DiffLineKind::Context:
            patch += " ";
            break;
        case DiffLineKind::Added:
            patch += "+";
            break;
        case DiffLineKind::Removed:
            patch += "-";
            break;
        case DiffLineKind::NoNewline:
            // The parsed text keeps its leading space: "\" restores the
            // exact " No newline at end of file" marker line.
            patch += "\\";
            break;
        }
        patch += line.text;
        patch += "\n";
    }
    return patch;
}

} // namespace repomancer::vcs
