// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include "entropy_dialog.h"

#include <wx/button.h>
#include <wx/dcclient.h>
#include <wx/gauge.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <chrono>

namespace repomancer::gui {

namespace {
// Only sample when the pointer has actually travelled — a jittering or parked
// mouse must not fill the bar with near-identical positions.
constexpr long kMinTravel = 3;

std::int64_t now_micros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
} // namespace

EntropyDialog::EntropyDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _("Mouse Entropy"), wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE) {
    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(new wxStaticText(this, wxID_ANY,
                               _("Move the mouse over the area below to stir "
                                 "randomness into the key.")),
              0, wxLEFT | wxRIGHT | wxTOP, 12);

    // The collection surface. A plain panel with a sunken border reads as "do
    // something here" without pretending to be a canvas.
    pad_ = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(420, 180),
                       wxBORDER_SUNKEN);
    pad_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX));
    pad_->Bind(wxEVT_MOTION, &EntropyDialog::OnMotion, this);
    root->Add(pad_, 1, wxEXPAND | wxALL, 12);

    gauge_ = new wxGauge(this, wxID_ANY,
                         static_cast<int>(repomancer::ssh::kCeremonySamples));
    root->Add(gauge_, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

    status_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    root->Add(status_, 0, wxLEFT | wxRIGHT | wxTOP, 12);

    // Say plainly what this does and does not do — the ceremony is confidence,
    // not the security floor.
    auto* note = new wxStaticText(
        this, wxID_ANY,
        _("Your system's secure random source is always mixed in; the mouse "
          "only adds to it."));
    note->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    root->Add(note, 0, wxLEFT | wxRIGHT | wxTOP, 12);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer();
    // Skipping is legitimate: with no samples the seed is pure OS entropy,
    // exactly what ssh-keygen would have used.
    buttons->Add(new wxButton(this, wxID_OK, _("&Skip and use system randomness")), 0,
                 wxRIGHT, 6);
    buttons->Add(new wxButton(this, wxID_CANCEL, _("Cancel")), 0);
    root->Add(buttons, 0, wxEXPAND | wxALL, 12);

    SetSizerAndFit(root);

    Bind(wxEVT_BUTTON, [this](wxCommandEvent& event) {
        if (event.GetId() == wxID_OK) {
            seed_ = pool_.finalize_seed();
        }
        event.Skip();
    });

    status_->SetLabel(wxString::Format(_("Collected 0 of %zu samples"),
                                       repomancer::ssh::kCeremonySamples));
}

void EntropyDialog::OnMotion(wxMouseEvent& event) {
    event.Skip();
    const std::size_t target = repomancer::ssh::kCeremonySamples;
    if (pool_.samples() >= target) {
        return;
    }
    const wxPoint p = event.GetPosition();
    if (last_x_ >= 0 && std::abs(p.x - last_x_) < kMinTravel &&
        std::abs(p.y - last_y_) < kMinTravel) {
        return; // too small a move to carry timing/position entropy
    }
    last_x_ = p.x;
    last_y_ = p.y;
    pool_.absorb_point(p.x, p.y, now_micros());

    const std::size_t n = pool_.samples();
    gauge_->SetValue(static_cast<int>(n));
    if (n >= target) {
        status_->SetLabel(_("Enough randomness collected — you can continue."));
        // Finish the ceremony as soon as it is complete; the seed is ready.
        seed_ = pool_.finalize_seed();
        EndModal(wxID_OK);
        return;
    }
    status_->SetLabel(wxString::Format(_("Collected %zu of %zu samples"), n, target));
}

} // namespace repomancer::gui
