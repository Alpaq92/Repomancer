// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Branching-model classification of refs, so the graph can be arranged by
// what a branch *means* rather than only by the order commits happen to be
// discovered: persistent branches keep the left-most lanes and each class
// carries its own colour.
//
// The model — the notion of per-class "persistence" and "order", and the
// git-flow rule set — follows git-graph (MIT, Copyright (c) 2021 Martin Lange),
// reimplemented here in C++. See NOTICE.

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace repomancer::vcs {

enum class BranchClass {
    Main,    // master / main / trunk
    Develop, // develop / dev
    Release,
    Hotfix,
    Bugfix,
    Feature,
    Unknown, // anything unmatched, including detached history
};

// Lane a class prefers: lower is further left. Persistent branches sit left so
// the mainline stays put instead of wandering as topics come and go.
[[nodiscard]] int branch_class_order(BranchClass cls);

// Palette index for a class, or -1 to let the lane cycle through the palette
// (Unknown branches carry no meaning worth colouring consistently).
[[nodiscard]] int branch_class_color(BranchClass cls);

[[nodiscard]] std::string_view branch_class_name(BranchClass cls);

class BranchModel {
public:
    // git-flow: master/main/trunk, develop, release/*, hotfix/*, bugfix/*,
    // feature/*.
    [[nodiscard]] static BranchModel git_flow();
    // Only the mainline is special; everything else is a topic branch.
    [[nodiscard]] static BranchModel simple();
    // Classify nothing — purely structural layout.
    [[nodiscard]] static BranchModel none();

    [[nodiscard]] BranchClass classify(std::string_view branch) const;

    // Classifies the branch names in a `git log %D` decoration string, e.g.
    // "HEAD -> main, origin/main, tag: v1". Tags are ignored, remote prefixes
    // are stripped, and the most persistent match wins.
    [[nodiscard]] BranchClass classify_refs(std::string_view decoration) const;

    [[nodiscard]] bool empty() const { return rules_.empty(); }

private:
    struct Rule {
        std::string pattern; // ECMAScript regex, matched case-sensitively
        BranchClass cls;
    };
    std::vector<Rule> rules_;
};

} // namespace repomancer::vcs
