// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Hunk-staging arithmetic (implementation-plan.md §4): a displayed hunk is
// re-serialized as a minimal, valid unified diff that `git apply` accepts —
// stage with --cached, discard with --reverse.

#pragma once

#include <repomancer/vcs/diff.h>

#include <cstddef>
#include <string>

namespace repomancer::vcs {

// True when `file` is the kind of diff hunk operations are defined on: a
// plain in-place text modification. Creations, deletions, renames and
// binary files stage whole (their single "hunk" is the file).
[[nodiscard]] bool supports_hunk_ops(const FileDiff& file);

// A patch containing exactly `file.hunks[hunk_index]`, byte-faithful to the
// parsed lines (including "\ No newline at end of file" markers). Returns
// an empty string when the file fails supports_hunk_ops or the index is out
// of range — callers treat that as "nothing to apply".
[[nodiscard]] std::string single_hunk_patch(const FileDiff& file, std::size_t hunk_index);

} // namespace repomancer::vcs
