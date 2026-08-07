// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// The "key ceremony" generator (implementation-plan.md §7): an Ed25519 key
// built from a seed the user visibly helped stir, TortoiseGit/PuTTYgen style.
//
// ssh-keygen draws straight from the OS CSPRNG and cannot be handed a seed, so
// a ceremony that genuinely influences the key needs its own generator: this
// one derives the seed with libsodium and writes the OpenSSH key files itself.
//
// HARD RULE: operating-system entropy is ALWAYS mixed in — at both the start
// and the end of the pool. User samples only ever ADD to it, never replace it.
// A ceremony with no samples at all is therefore still cryptographically sound;
// on a modern OS the mouse is trust-building UX, not a security necessity.

#pragma once

#include <repomancer/ssh/keys.h> // KeyInfo / GenerateRequest / SshResult

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace repomancer::ssh {

// Accumulates entropy samples and derives a 32-byte Ed25519 seed. Seeded from
// the OS CSPRNG on construction; a second OS draw is folded in at finalize().
class EntropyPool {
public:
    EntropyPool();

    // Fold in arbitrary bytes (a mouse position, a timestamp, keystroke timing).
    void absorb(const void* data, std::size_t size);
    // Convenience for the GUI ceremony: one mouse sample.
    void absorb_point(std::int32_t x, std::int32_t y, std::int64_t timestamp_us);

    // How many samples have been absorbed — drives the ceremony's progress bar.
    [[nodiscard]] std::size_t samples() const noexcept { return samples_; }

    // Derive the seed (mixing a fresh OS draw). May be called once; further
    // absorb() calls after this are ignored by the returned seed.
    [[nodiscard]] std::vector<std::uint8_t> finalize_seed();

private:
    std::vector<std::uint8_t> state_; // running hash state, hashed on each absorb
    std::size_t samples_ = 0;
};

// How many mouse samples a ceremony collects before it is considered complete.
// Chosen for UX (a few seconds of movement), not for a security threshold —
// the OS CSPRNG already carries that weight.
inline constexpr std::size_t kCeremonySamples = 256;

// Generate an Ed25519 key from `seed` (32 bytes) and write it to req.path in
// OpenSSH format, with req.path + ".pub" beside it. Refuses to clobber an
// existing key, exactly like ssh::generate().
//
// The key is written unencrypted, then — when req.passphrase is non-empty —
// protection is applied with `ssh-keygen -p` (the same tested path as
// change_passphrase), so no private-key encryption is reimplemented here.
[[nodiscard]] SshResult<KeyInfo> generate_from_seed(
    const GenerateRequest& req, const std::vector<std::uint8_t>& seed,
    const SshConfig& cfg = {});

} // namespace repomancer::ssh
