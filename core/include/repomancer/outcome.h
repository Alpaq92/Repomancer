// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Minimal success-or-error result type (std::expected arrives with C++23;
// this is the subset we need until then).

#pragma once

#include <utility>
#include <variant>

namespace repomancer {

template <typename T, typename E>
class Outcome {
public:
    Outcome(T value) : v_(std::in_place_index<0>, std::move(value)) {}
    Outcome(E error) : v_(std::in_place_index<1>, std::move(error)) {}

    [[nodiscard]] bool ok() const noexcept { return v_.index() == 0; }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] T& value() & { return std::get<0>(v_); }
    [[nodiscard]] const T& value() const& { return std::get<0>(v_); }
    [[nodiscard]] T&& value() && { return std::get<0>(std::move(v_)); }

    [[nodiscard]] E& error() & { return std::get<1>(v_); }
    [[nodiscard]] const E& error() const& { return std::get<1>(v_); }

private:
    std::variant<T, E> v_;
};

} // namespace repomancer
