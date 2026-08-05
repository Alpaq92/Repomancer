// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/vcs/diff.h>
#include <repomancer/vcs/patch.h>

#include <catch2/catch_test_macros.hpp>

using namespace repomancer::vcs;

namespace {

std::vector<FileDiff> parse(std::string_view text) {
    auto result = parse_unified_diff(text);
    REQUIRE(result.ok());
    return std::move(result).value();
}

} // namespace

TEST_CASE("single_hunk_patch extracts one hunk of a two-hunk diff", "[patch]") {
    const auto files = parse("diff --git a/f.txt b/f.txt\n"
                             "index 000..111 100644\n"
                             "--- a/f.txt\n"
                             "+++ b/f.txt\n"
                             "@@ -1,3 +1,3 @@\n"
                             "-one\n"
                             "+ONE\n"
                             " two\n"
                             " three\n"
                             "@@ -10,3 +10,4 @@ heading()\n"
                             " ten\n"
                             "+ten and a half\n"
                             " eleven\n"
                             " twelve\n");
    REQUIRE(files.size() == 1);
    REQUIRE(files[0].hunks.size() == 2);
    REQUIRE(supports_hunk_ops(files[0]));

    CHECK(single_hunk_patch(files[0], 0) == "--- a/f.txt\n"
                                            "+++ b/f.txt\n"
                                            "@@ -1,3 +1,3 @@\n"
                                            "-one\n"
                                            "+ONE\n"
                                            " two\n"
                                            " three\n");
    // The heading survives, and only the requested hunk is present.
    CHECK(single_hunk_patch(files[0], 1) == "--- a/f.txt\n"
                                            "+++ b/f.txt\n"
                                            "@@ -10,3 +10,4 @@ heading()\n"
                                            " ten\n"
                                            "+ten and a half\n"
                                            " eleven\n"
                                            " twelve\n");
    CHECK(single_hunk_patch(files[0], 2).empty()); // out of range
}

TEST_CASE("single_hunk_patch keeps the no-newline marker byte-faithful", "[patch]") {
    const std::string text = "diff --git a/f.txt b/f.txt\n"
                             "--- a/f.txt\n"
                             "+++ b/f.txt\n"
                             "@@ -1 +1 @@\n"
                             "-old\n"
                             "+new\n"
                             "\\ No newline at end of file\n";
    const auto files = parse(text);
    REQUIRE(files.size() == 1);
    CHECK(single_hunk_patch(files[0], 0) == "--- a/f.txt\n"
                                            "+++ b/f.txt\n"
                                            "@@ -1,1 +1,1 @@\n"
                                            "-old\n"
                                            "+new\n"
                                            "\\ No newline at end of file\n");
}

TEST_CASE("hunk ops are refused where the patch shape is not a plain edit",
          "[patch]") {
    SECTION("creation (old side is /dev/null)") {
        const auto files = parse("diff --git a/n.txt b/n.txt\n"
                                 "--- /dev/null\n"
                                 "+++ b/n.txt\n"
                                 "@@ -0,0 +1 @@\n"
                                 "+content\n");
        REQUIRE(files.size() == 1);
        CHECK(!supports_hunk_ops(files[0]));
        CHECK(single_hunk_patch(files[0], 0).empty());
    }
    SECTION("deletion (new side is /dev/null)") {
        const auto files = parse("diff --git a/g.txt b/g.txt\n"
                                 "--- a/g.txt\n"
                                 "+++ /dev/null\n"
                                 "@@ -1 +0,0 @@\n"
                                 "-content\n");
        REQUIRE(files.size() == 1);
        CHECK(!supports_hunk_ops(files[0]));
    }
    SECTION("rename with edits") {
        const auto files = parse("diff --git a/old.txt b/new.txt\n"
                                 "rename from old.txt\n"
                                 "rename to new.txt\n"
                                 "--- a/old.txt\n"
                                 "+++ b/new.txt\n"
                                 "@@ -1 +1 @@\n"
                                 "-x\n"
                                 "+y\n");
        REQUIRE(files.size() == 1);
        CHECK(!supports_hunk_ops(files[0]));
    }
    SECTION("binary") {
        FileDiff file;
        file.old_path = file.new_path = "b.bin";
        file.is_binary = true;
        CHECK(!supports_hunk_ops(file));
    }
}

TEST_CASE("body lines that look like file headers stay body lines", "[patch]") {
    // Removing an SQL comment "-- drop old index" serializes as
    // "--- drop old index"; adding "++ b/f.txt" serializes as "+++ b/f.txt".
    // Both must parse as hunk content, not clobber the paths.
    const auto files = parse("diff --git a/schema.sql b/schema.sql\n"
                             "--- a/schema.sql\n"
                             "+++ b/schema.sql\n"
                             "@@ -1,3 +1,3 @@\n"
                             " select 1;\n"
                             "--- drop old index\n"
                             "+++ b/f.txt\n"
                             " select 2;\n");
    REQUIRE(files.size() == 1);
    CHECK(files[0].old_path == "schema.sql");
    CHECK(files[0].new_path == "schema.sql");
    REQUIRE(files[0].hunks.size() == 1);
    REQUIRE(files[0].hunks[0].lines.size() == 4);
    CHECK(files[0].hunks[0].lines[1].kind == DiffLineKind::Removed);
    CHECK(files[0].hunks[0].lines[1].text == "-- drop old index");
    CHECK(files[0].hunks[0].lines[2].kind == DiffLineKind::Added);
    CHECK(files[0].hunks[0].lines[2].text == "++ b/f.txt");
    CHECK(supports_hunk_ops(files[0]));
    // And the round-trip re-emits them byte-for-byte.
    const std::string patch = single_hunk_patch(files[0], 0);
    CHECK(patch.find("--- drop old index\n") != std::string::npos);
    CHECK(patch.find("+++ b/f.txt\n") != std::string::npos);
}

TEST_CASE("CRLF hunk bodies keep their carriage returns", "[patch]") {
    const auto files = parse("diff --git a/dos.txt b/dos.txt\n"
                             "--- a/dos.txt\n"
                             "+++ b/dos.txt\n"
                             "@@ -1,3 +1,3 @@\n"
                             " one\r\n"
                             "-two\r\n"
                             "+TWO\r\n"
                             " three\r\n");
    REQUIRE(files.size() == 1);
    CHECK(files[0].old_path == "dos.txt"); // headers still strip their CR
    REQUIRE(files[0].hunks.size() == 1);
    CHECK(files[0].hunks[0].lines[0].text == "one\r");
    const std::string patch = single_hunk_patch(files[0], 0);
    CHECK(patch == "--- a/dos.txt\n"
                   "+++ b/dos.txt\n"
                   "@@ -1,3 +1,3 @@\n"
                   " one\r\n"
                   "-two\r\n"
                   "+TWO\r\n"
                   " three\r\n");
}
