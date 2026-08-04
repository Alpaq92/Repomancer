// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/vcs/stats.h>

#include <algorithm>
#include <charconv>
#include <unordered_map>

namespace repomancer::vcs {

namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

std::string_view next_line(std::string_view& data) {
    std::size_t eol = data.find('\n');
    if (eol == std::string_view::npos) {
        const std::string_view line = data;
        data = {};
        return line;
    }
    const std::string_view line = data.substr(0, eol);
    data.remove_prefix(eol + 1);
    return line;
}

std::string_view extension_of(std::string_view path) {
    const std::size_t slash = path.find_last_of('/');
    const std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1 >= name.size()) {
        return {};
    }
    return name.substr(dot + 1);
}

} // namespace

VcsResult<std::vector<Contributor>> parse_shortlog(std::string_view data,
                                                   const ParseLimits& limits) {
    std::vector<Contributor> contributors;

    while (!data.empty()) {
        const std::string_view line = next_line(data);
        if (trim(line).empty()) {
            continue;
        }
        if (contributors.size() >= limits.max_entries) {
            return VcsError{VcsError::Kind::LimitExceeded, "too many contributors"};
        }

        const std::size_t tab = line.find('\t');
        if (tab == std::string_view::npos) {
            return VcsError{VcsError::Kind::ParseError, "shortlog line without a count"};
        }
        Contributor contributor;
        const std::string_view count = trim(line.substr(0, tab));
        const auto* begin = count.data();
        const auto* end = begin + count.size();
        if (std::from_chars(begin, end, contributor.commits).ec != std::errc{}) {
            return VcsError{VcsError::Kind::ParseError, "shortlog count is not a number"};
        }

        std::string_view identity = trim(line.substr(tab + 1));
        // "Name <email>" — the address is optional, since -s alone omits it.
        if (!identity.empty() && identity.back() == '>') {
            const std::size_t open = identity.find_last_of('<');
            if (open != std::string_view::npos) {
                contributor.email =
                    std::string(identity.substr(open + 1, identity.size() - open - 2));
                identity = trim(identity.substr(0, open));
            }
        }
        contributor.name = std::string(identity);
        contributors.push_back(std::move(contributor));
    }
    return contributors;
}

std::string_view language_for_path(std::string_view path) {
    // Paths that would drown out what the project is actually written in.
    static constexpr std::string_view kIgnoredSegments[] = {
        "third_party/", "vendor/", "node_modules/", "external/", "build/",
    };
    for (const auto segment : kIgnoredSegments) {
        if (path.rfind(segment, 0) == 0 || path.find("/" + std::string(segment)) !=
                                               std::string_view::npos) {
            return {};
        }
    }

    struct Mapping {
        std::string_view extension;
        std::string_view language;
    };
    static constexpr Mapping kByExtension[] = {
        {"c", "C"},          {"h", "C/C++ header"}, {"cc", "C++"},      {"cpp", "C++"},
        {"cxx", "C++"},      {"hpp", "C/C++ header"}, {"hxx", "C/C++ header"},
        {"cs", "C#"},        {"java", "Java"},      {"kt", "Kotlin"},   {"swift", "Swift"},
        {"m", "Objective-C"}, {"mm", "Objective-C++"}, {"go", "Go"},    {"rs", "Rust"},
        {"py", "Python"},    {"rb", "Ruby"},        {"pl", "Perl"},     {"php", "PHP"},
        {"js", "JavaScript"}, {"mjs", "JavaScript"}, {"ts", "TypeScript"},
        {"tsx", "TypeScript"}, {"jsx", "JavaScript"}, {"lua", "Lua"},   {"sh", "Shell"},
        {"bash", "Shell"},   {"zsh", "Shell"},      {"ps1", "PowerShell"},
        {"bat", "Batchfile"}, {"cmd", "Batchfile"}, {"html", "HTML"},   {"htm", "HTML"},
        {"css", "CSS"},      {"scss", "SCSS"},      {"sql", "SQL"},     {"r", "R"},
        {"scala", "Scala"},  {"hs", "Haskell"},     {"ml", "OCaml"},    {"ex", "Elixir"},
        {"exs", "Elixir"},   {"dart", "Dart"},      {"zig", "Zig"},     {"vim", "Vim script"},
        {"cmake", "CMake"},  {"yml", "YAML"},       {"yaml", "YAML"},   {"json", "JSON"},
        {"toml", "TOML"},    {"xml", "XML"},        {"md", "Markdown"}, {"rst", "reStructuredText"},
        {"tex", "TeX"},      {"po", "Gettext"},
    };

    const std::string_view extension = extension_of(path);
    if (extension.empty()) {
        // A few names carry their language without one.
        const std::size_t slash = path.find_last_of('/');
        const std::string_view name =
            slash == std::string_view::npos ? path : path.substr(slash + 1);
        if (name == "CMakeLists.txt") {
            return "CMake";
        }
        if (name == "Makefile" || name == "makefile") {
            return "Makefile";
        }
        if (name == "Dockerfile") {
            return "Dockerfile";
        }
        return {};
    }

    std::string lowered(extension);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const auto& mapping : kByExtension) {
        if (mapping.extension == lowered) {
            return mapping.language;
        }
    }
    return {};
}

VcsResult<std::vector<LanguageStat>> parse_ls_tree_sizes(std::string_view data,
                                                         const ParseLimits& limits) {
    std::unordered_map<std::string, std::uint64_t> totals;
    std::uint64_t classified = 0;
    std::size_t seen = 0;

    while (!data.empty()) {
        const std::string_view line = next_line(data);
        if (trim(line).empty()) {
            continue;
        }
        if (++seen > limits.max_entries) {
            return VcsError{VcsError::Kind::LimitExceeded, "too many tree entries"};
        }

        // "<mode> <type> <oid> <size>\t<path>"
        const std::size_t tab = line.find('\t');
        if (tab == std::string_view::npos) {
            return VcsError{VcsError::Kind::ParseError, "tree entry without a path"};
        }
        const std::string_view path = line.substr(tab + 1);
        std::string_view head = line.substr(0, tab);

        // The size is the last whitespace-separated field before the tab; for
        // anything that is not a blob git writes "-" there.
        const std::size_t size_start = head.find_last_of(" \t");
        if (size_start == std::string_view::npos) {
            return VcsError{VcsError::Kind::ParseError, "tree entry without a size"};
        }
        const std::string_view size_text = trim(head.substr(size_start + 1));
        if (size_text == "-") {
            continue; // submodule or tree, which has no size of its own
        }
        std::uint64_t size = 0;
        const auto* begin = size_text.data();
        const auto* end = begin + size_text.size();
        if (std::from_chars(begin, end, size).ec != std::errc{}) {
            return VcsError{VcsError::Kind::ParseError, "tree entry size is not a number"};
        }

        const std::string_view language = language_for_path(path);
        if (language.empty()) {
            continue;
        }
        totals[std::string(language)] += size;
        classified += size;
    }

    std::vector<LanguageStat> stats;
    stats.reserve(totals.size());
    for (auto& [name, bytes] : totals) {
        LanguageStat stat;
        stat.name = name;
        stat.bytes = bytes;
        stat.percent = classified > 0 ? (100.0 * static_cast<double>(bytes) /
                                         static_cast<double>(classified))
                                      : 0.0;
        stats.push_back(std::move(stat));
    }
    std::sort(stats.begin(), stats.end(), [](const LanguageStat& a, const LanguageStat& b) {
        return a.bytes != b.bytes ? a.bytes > b.bytes : a.name < b.name;
    });
    return stats;
}

} // namespace repomancer::vcs
