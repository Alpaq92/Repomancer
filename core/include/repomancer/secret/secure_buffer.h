// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Secure memory for secrets (implementation-plan.md §6): tokens, PATs and
// in-transit passphrases live in libsodium-guarded memory — mlock'd (never
// swapped), bracketed by guard pages, wiped on destruction — and are move-only
// so a secret is never silently copied. libsodium is an implementation detail
// here; it is not exposed in this header.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace repomancer::secret {

// Initialize libsodium (idempotent, thread-safe). The types below call it
// themselves; expose it so callers can front-load it at startup.
void ensure_initialized();

// A move-only byte buffer in sodium_malloc'd memory: guard pages, mlock, and a
// guaranteed wipe (sodium_free) on destruction. Copying is deleted.
class SecureBuffer {
public:
    SecureBuffer() = default;
    explicit SecureBuffer(std::size_t size);            // zero-filled
    SecureBuffer(const void* data, std::size_t size);   // copied in, then wiped on free

    ~SecureBuffer();
    SecureBuffer(SecureBuffer&& other) noexcept;
    SecureBuffer& operator=(SecureBuffer&& other) noexcept;
    SecureBuffer(const SecureBuffer&) = delete;
    SecureBuffer& operator=(const SecureBuffer&) = delete;

    [[nodiscard]] unsigned char* data() noexcept { return data_; }
    [[nodiscard]] const unsigned char* data() const noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    void reset() noexcept; // wipe + free

private:
    unsigned char* data_ = nullptr;
    std::size_t size_ = 0;
};

// A secret string kept in a SecureBuffer. Move-only. There is deliberately no
// operator<< / implicit std::string conversion — reveal() only at the point of
// use (an HTTP header, an argv-free stdin write), and drop the copy promptly.
class SecretString {
public:
    SecretString() = default;
    explicit SecretString(std::string_view value);

    [[nodiscard]] std::string reveal() const;
    // Read access to the secret bytes for crypto at the point of use — no heap
    // copy, unlike reveal(). Valid while this SecretString is alive.
    [[nodiscard]] const unsigned char* bytes() const noexcept { return buf_.data(); }
    [[nodiscard]] std::size_t size() const noexcept { return buf_.size(); }
    [[nodiscard]] bool empty() const noexcept { return buf_.empty(); }

private:
    SecureBuffer buf_;
};

} // namespace repomancer::secret
