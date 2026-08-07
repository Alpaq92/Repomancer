// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// A tiny transport abstraction so the forge/OAuth logic (implementation-plan.md
// §6) stays wx-free and unit-testable. Core and tests depend only on this
// interface; the app plugs in a concrete client later — wxWebRequest in the GUI
// or libcurl for headless paths (§6). No HTTP library is pulled into core here.

#pragma once

#include <repomancer/outcome.h>

#include <string>
#include <utility>
#include <vector>

namespace repomancer::forge {

enum class HttpMethod { Get, Post };

struct HttpRequest {
    HttpMethod method = HttpMethod::Get;
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body; // request body for POST
};

struct HttpResponse {
    long status = 0;
    std::string body;
};

struct ForgeError {
    enum class Kind {
        Transport,   // the request never completed (DNS/TLS/socket)
        HttpStatus,  // a non-success HTTP status where success was required
        ParseError,  // the response body was not the expected JSON shape
        OAuthError,  // the forge returned a well-formed OAuth error
    };

    ForgeError() = default;
    ForgeError(Kind k, std::string msg, long status = 0)
        : kind(k), message(std::move(msg)), http_status(status) {}

    Kind kind{};
    std::string message;
    long http_status = 0;
};

template <typename T>
using ForgeResult = Outcome<T, ForgeError>;

// The transport seam. A concrete implementation performs the request; a
// transport-level failure (never reached the server) is a Transport error.
// Any HTTP status — including 4xx/5xx — that *did* come back is a successful
// send() returning that HttpResponse; interpreting the status is the caller's.
class HttpClient {
public:
    virtual ~HttpClient() = default;
    [[nodiscard]] virtual ForgeResult<HttpResponse> send(const HttpRequest& request) = 0;
};

} // namespace repomancer::forge
