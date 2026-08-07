// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// The "key ceremony" (implementation-plan.md §7): the user stirs randomness by
// moving the mouse, TortoiseGit/PuTTYgen style, and the collected samples seed
// the Ed25519 key. Honest about what it is — the operating system's randomness
// is always mixed in, so this adds to the seed rather than being trusted alone.

#pragma once

#include <repomancer/ssh/entropy.h>

#include <wx/dialog.h>

#include <vector>

class wxGauge;
class wxStaticText;

namespace repomancer::gui {

class EntropyDialog : public wxDialog {
public:
    explicit EntropyDialog(wxWindow* parent);

    // The derived 32-byte seed. Valid once the dialog returns wxID_OK.
    [[nodiscard]] const std::vector<std::uint8_t>& seed() const { return seed_; }

private:
    void OnMotion(wxMouseEvent& event);

    repomancer::ssh::EntropyPool pool_;
    std::vector<std::uint8_t> seed_;
    wxGauge* gauge_ = nullptr;
    wxStaticText* status_ = nullptr;
    wxWindow* pad_ = nullptr;
    long last_x_ = -1;
    long last_y_ = -1;
};

} // namespace repomancer::gui
