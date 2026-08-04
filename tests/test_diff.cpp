// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/vcs/diff.h>

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <string>

using namespace repomancer::vcs;

namespace {

std::string join_z(std::initializer_list<std::string> records) {
    std::string data;
    for (const auto& record : records) {
        data += record;
        data.push_back('\0');
    }
    return data;
}

} // namespace

TEST_CASE("name-status: plain statuses") {
    const auto data = join_z({"M", "src/a.cpp", "A", "new file.txt", "D", "gone.txt"});
    const auto result = parse_name_status_z(data);
    REQUIRE(result.ok());
    const auto& files = result.value();
    REQUIRE(files.size() == 3);
    CHECK(files[0].change == FileChange::Modified);
    CHECK(files[0].path == "src/a.cpp");
    CHECK(files[1].change == FileChange::Added);
    CHECK(files[1].path == "new file.txt");
    CHECK(files[2].change == FileChange::Deleted);
}

TEST_CASE("name-status: renames carry both paths and a score") {
    const auto data = join_z({"R100", "old/name.txt", "new/name.txt", "M", "other.txt"});
    const auto result = parse_name_status_z(data);
    REQUIRE(result.ok());
    const auto& files = result.value();
    REQUIRE(files.size() == 2);
    CHECK(files[0].change == FileChange::Renamed);
    CHECK(files[0].score == 100);
    CHECK(files[0].old_path == "old/name.txt");
    CHECK(files[0].path == "new/name.txt");
    // The record after the rename's two paths must still be read as a status.
    CHECK(files[1].path == "other.txt");
}

TEST_CASE("name-status: malformed input is rejected") {
    CHECK_FALSE(parse_name_status_z(join_z({"M"})).ok());               // status, no path
    CHECK_FALSE(parse_name_status_z(join_z({"R100", "only-one"})).ok()); // rename, one path
    CHECK_FALSE(parse_name_status_z(join_z({"Z", "weird.txt"})).ok());   // unknown status
}

TEST_CASE("unified diff: hunk header and line numbering") {
    const std::string patch =
        "diff --git a/f.txt b/f.txt\n"
        "index 4d588cf..d00dbc5 100644\n"
        "--- a/f.txt\n"
        "+++ b/f.txt\n"
        "@@ -3,3 +3,4 @@ main work\n"
        " dev setup\n"
        "-login form\n"
        "+login form v2\n"
        "+more dev\n"
        " login tests\n";
    const auto result = parse_unified_diff(patch);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 1);
    const auto& file = result.value()[0];
    CHECK(file.old_path == "f.txt");
    CHECK(file.new_path == "f.txt");
    CHECK_FALSE(file.is_binary);
    REQUIRE(file.hunks.size() == 1);

    const auto& hunk = file.hunks[0];
    CHECK(hunk.old_start == 3);
    CHECK(hunk.old_count == 3);
    CHECK(hunk.new_start == 3);
    CHECK(hunk.new_count == 4);
    CHECK(hunk.heading == "main work");
    REQUIRE(hunk.lines.size() == 5);

    CHECK(hunk.lines[0].kind == DiffLineKind::Context);
    CHECK(hunk.lines[0].old_lineno == 3);
    CHECK(hunk.lines[0].new_lineno == 3);
    CHECK(hunk.lines[0].text == "dev setup");

    CHECK(hunk.lines[1].kind == DiffLineKind::Removed);
    CHECK(hunk.lines[1].old_lineno == 4);
    CHECK(hunk.lines[1].new_lineno == 0); // absent on the new side

    CHECK(hunk.lines[2].kind == DiffLineKind::Added);
    CHECK(hunk.lines[2].new_lineno == 4);
    CHECK(hunk.lines[2].old_lineno == 0);

    CHECK(hunk.lines[4].kind == DiffLineKind::Context);
    CHECK(hunk.lines[4].old_lineno == 5);
    CHECK(hunk.lines[4].new_lineno == 6);
}

TEST_CASE("unified diff: several files in one patch") {
    const std::string patch =
        "diff --git a/a.txt b/a.txt\n"
        "--- a/a.txt\n"
        "+++ b/a.txt\n"
        "@@ -1 +1 @@\n"
        "-one\n"
        "+two\n"
        "diff --git a/b.txt b/b.txt\n"
        "--- a/b.txt\n"
        "+++ b/b.txt\n"
        "@@ -1,0 +2,1 @@\n"
        "+added\n";
    const auto result = parse_unified_diff(patch);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 2);
    CHECK(result.value()[0].new_path == "a.txt");
    CHECK(result.value()[1].new_path == "b.txt");
    // "@@ -1 +1 @@" — a range without a comma covers exactly one line.
    CHECK(result.value()[0].hunks[0].old_count == 1);
    CHECK(result.value()[0].hunks[0].new_count == 1);
}

TEST_CASE("unified diff: creation and deletion use /dev/null") {
    const std::string patch =
        "diff --git a/new.txt b/new.txt\n"
        "new file mode 100644\n"
        "--- /dev/null\n"
        "+++ b/new.txt\n"
        "@@ -0,0 +1,2 @@\n"
        "+first\n"
        "+second\n";
    const auto result = parse_unified_diff(patch);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 1);
    CHECK(result.value()[0].old_path.empty());
    CHECK(result.value()[0].new_path == "new.txt");
    CHECK(result.value()[0].hunks[0].lines.size() == 2);
}

TEST_CASE("unified diff: binary files and missing trailing newline") {
    const std::string binary = "diff --git a/i.png b/i.png\n"
                               "Binary files a/i.png and b/i.png differ\n";
    const auto bin = parse_unified_diff(binary);
    REQUIRE(bin.ok());
    REQUIRE(bin.value().size() == 1);
    CHECK(bin.value()[0].is_binary);
    CHECK(bin.value()[0].hunks.empty());

    const std::string no_eol = "diff --git a/f b/f\n"
                               "--- a/f\n"
                               "+++ b/f\n"
                               "@@ -1 +1 @@\n"
                               "-old\n"
                               "\\ No newline at end of file\n"
                               "+new\n";
    const auto result = parse_unified_diff(no_eol);
    REQUIRE(result.ok());
    const auto& lines = result.value()[0].hunks[0].lines;
    REQUIRE(lines.size() == 3);
    CHECK(lines[1].kind == DiffLineKind::NoNewline);
    // The marker must not consume a line number on either side.
    CHECK(lines[1].old_lineno == 0);
    CHECK(lines[1].new_lineno == 0);
    CHECK(lines[2].kind == DiffLineKind::Added);
}

TEST_CASE("unified diff: empty input and malformed hunk header") {
    const auto empty = parse_unified_diff("");
    REQUIRE(empty.ok());
    CHECK(empty.value().empty());

    const std::string bad = "diff --git a/f b/f\n"
                            "--- a/f\n"
                            "+++ b/f\n"
                            "@@ nonsense @@\n";
    const auto result = parse_unified_diff(bad);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().kind == VcsError::Kind::ParseError);
}

TEST_CASE("unified diff: limits are enforced") {
    ParseLimits limits;
    limits.max_record_bytes = 8;
    const std::string patch = "diff --git a/f b/f\n"
                              "--- a/f\n"
                              "+++ b/f\n"
                              "@@ -1 +1 @@\n"
                              "+a line far longer than the limit\n";
    const auto result = parse_unified_diff(patch, limits);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().kind == VcsError::Kind::LimitExceeded);
}
