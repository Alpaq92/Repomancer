// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/vcs/branch_model.h>

#include <algorithm>
#include <regex>

namespace repomancer::vcs {

namespace {

// Remote-tracking refs describe the same branch as their local counterpart.
std::string_view strip_remote(std::string_view ref) {
    const std::size_t slash = ref.find('/');
    if (slash == std::string_view::npos) {
        return ref;
    }
    // Only strip a single leading remote name; "feature/x" must survive.
    static constexpr std::string_view kKnownPrefixes[] = {"origin/", "upstream/", "remotes/"};
    for (const auto prefix : kKnownPrefixes) {
        if (ref.substr(0, prefix.size()) == prefix) {
            return ref.substr(prefix.size());
        }
    }
    return ref;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

} // namespace

int branch_class_order(BranchClass cls) {
    switch (cls) {
    case BranchClass::Main:
        return 0;
    case BranchClass::Develop:
        return 1;
    case BranchClass::Release:
    case BranchClass::Hotfix:
        return 2;
    case BranchClass::Bugfix:
    case BranchClass::Feature:
        return 3;
    case BranchClass::Unknown:
        break;
    }
    return 4;
}

int branch_class_color(BranchClass cls) {
    // Indices into the renderer's palette (see gui/src/graph_renderer.cpp).
    switch (cls) {
    case BranchClass::Main:
        return 0; // blue
    case BranchClass::Develop:
        return 6; // yellow
    case BranchClass::Release:
        return 4; // purple
    case BranchClass::Hotfix:
        return 3; // red
    case BranchClass::Bugfix:
        return 5; // teal
    case BranchClass::Feature:
        return 2; // green
    case BranchClass::Unknown:
        break;
    }
    return -1; // cycle through the palette
}

std::string_view branch_class_name(BranchClass cls) {
    switch (cls) {
    case BranchClass::Main:
        return "main";
    case BranchClass::Develop:
        return "develop";
    case BranchClass::Release:
        return "release";
    case BranchClass::Hotfix:
        return "hotfix";
    case BranchClass::Bugfix:
        return "bugfix";
    case BranchClass::Feature:
        return "feature";
    case BranchClass::Unknown:
        break;
    }
    return "unknown";
}

BranchModel BranchModel::git_flow() {
    BranchModel model;
    model.rules_ = {
        {R"(^(master|main|trunk)$)", BranchClass::Main},
        {R"(^(develop|dev)$)", BranchClass::Develop},
        {R"(^release[/-].*$)", BranchClass::Release},
        {R"(^hotfix[/-].*$)", BranchClass::Hotfix},
        {R"(^bugfix[/-].*$)", BranchClass::Bugfix},
        {R"(^feature[/-].*$)", BranchClass::Feature},
    };
    return model;
}

BranchModel BranchModel::simple() {
    BranchModel model;
    model.rules_ = {
        {R"(^(master|main|trunk)$)", BranchClass::Main},
        {R"(^.+$)", BranchClass::Feature},
    };
    return model;
}

BranchModel BranchModel::none() { return {}; }

BranchClass BranchModel::classify(std::string_view branch) const {
    const std::string_view name = strip_remote(trim(branch));
    if (name.empty()) {
        return BranchClass::Unknown;
    }
    const std::string subject(name);
    for (const auto& rule : rules_) {
        // Patterns are fixed at construction, so a failed compile is a bug in
        // this file rather than anything user-supplied.
        const std::regex regex(rule.pattern, std::regex::ECMAScript);
        if (std::regex_match(subject, regex)) {
            return rule.cls;
        }
    }
    return BranchClass::Unknown;
}

BranchClass BranchModel::classify_refs(std::string_view decoration) const {
    BranchClass best = BranchClass::Unknown;
    int best_order = branch_class_order(BranchClass::Unknown);

    std::size_t start = 0;
    while (start <= decoration.size()) {
        std::size_t comma = decoration.find(',', start);
        if (comma == std::string_view::npos) {
            comma = decoration.size();
        }
        std::string_view ref = trim(decoration.substr(start, comma - start));
        start = comma + 1;

        if (ref.empty()) {
            continue;
        }
        // "HEAD -> main" names the checked-out branch; a bare "HEAD" does not.
        constexpr std::string_view kHeadArrow = "HEAD -> ";
        if (ref.substr(0, kHeadArrow.size()) == kHeadArrow) {
            ref = ref.substr(kHeadArrow.size());
        } else if (ref == "HEAD") {
            continue;
        }
        // Tags say nothing about which branch a commit belongs to.
        constexpr std::string_view kTag = "tag: ";
        if (ref.substr(0, kTag.size()) == kTag) {
            continue;
        }

        const BranchClass cls = classify(ref);
        const int order = branch_class_order(cls);
        if (cls != BranchClass::Unknown && order < best_order) {
            best = cls;
            best_order = order;
        }
    }
    return best;
}

} // namespace repomancer::vcs
