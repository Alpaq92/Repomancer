// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/secret/secure_buffer.h>

#include <sodium.h>

#include <cstring>
#include <mutex>
#include <new>
#include <stdexcept>
#include <utility>

namespace repomancer::secret {

void ensure_initialized() {
    static std::once_flag once;
    std::call_once(once, [] {
        if (sodium_init() < 0) {
            // -1 is a hard failure; 1 means "already initialized" and is fine.
            throw std::runtime_error("libsodium initialization failed");
        }
    });
}

SecureBuffer::SecureBuffer(std::size_t size) {
    if (size == 0) {
        return;
    }
    ensure_initialized();
    data_ = static_cast<unsigned char*>(sodium_malloc(size));
    if (data_ == nullptr) {
        throw std::bad_alloc();
    }
    size_ = size;
    sodium_memzero(data_, size_);
}

SecureBuffer::SecureBuffer(const void* data, std::size_t size) : SecureBuffer(size) {
    if (size_ != 0) {
        std::memcpy(data_, data, size_);
    }
}

SecureBuffer::~SecureBuffer() { reset(); }

SecureBuffer::SecureBuffer(SecureBuffer&& other) noexcept
    : data_(other.data_), size_(other.size_) {
    other.data_ = nullptr;
    other.size_ = 0;
}

SecureBuffer& SecureBuffer::operator=(SecureBuffer&& other) noexcept {
    if (this != &other) {
        reset();
        data_ = other.data_;
        size_ = other.size_;
        other.data_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

void SecureBuffer::reset() noexcept {
    if (data_ != nullptr) {
        sodium_free(data_); // zeroes, munlocks, and frees
        data_ = nullptr;
    }
    size_ = 0;
}

SecretString::SecretString(std::string_view value)
    : buf_(value.data(), value.size()) {}

std::string SecretString::reveal() const {
    return std::string(reinterpret_cast<const char*>(buf_.data()), buf_.size());
}

} // namespace repomancer::secret
