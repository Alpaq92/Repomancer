// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// OAuth 2.0 Device Authorization Grant for GitHub and GitLab
// (implementation-plan.md §6): the user is shown a short code to type into the
// forge, and we poll for the resulting token. All logic is wx-free and runs
// against the HttpClient seam, so it is fully unit-testable with a fake client.

#pragma once

#include <repomancer/forge/http.h>

#include <string>

namespace repomancer::forge {

struct OAuthProvider {
    std::string name;
    std::string device_code_url; // where the device/user codes are requested
    std::string token_url;       // where the token is polled for
    std::string client_id;       // embedded public client id (see oauth.cpp)
    std::string scope;           // requested scope (key upload)
};

// The forges we ship with. client_id is a placeholder until the OAuth apps are
// registered — see the note in oauth.cpp.
[[nodiscard]] OAuthProvider github_provider();
[[nodiscard]] OAuthProvider gitlab_provider(const std::string& base_url = "https://gitlab.com");

struct DeviceCode {
    std::string device_code;               // used to poll — treat as a secret
    std::string user_code;                 // shown to the user to type in
    std::string verification_uri;          // the page the user opens
    std::string verification_uri_complete; // optional: opens with the code filled
    int interval = 5;                      // seconds between polls
    int expires_in = 900;                  // lifetime of device_code
};

enum class PollState {
    Authorized, // the user approved — access_token is set
    Pending,    // authorization_pending — keep polling at `interval`
    SlowDown,   // slow_down — poll less often
    Denied,     // access_denied — the user refused
    Expired,    // expired_token — the device_code lapsed; restart the flow
};

struct TokenGrant {
    PollState state = PollState::Pending;
    std::string access_token; // when Authorized — treat as a secret
    std::string token_type;
    std::string scope;
};

// Start the flow: POST to device_code_url and parse the device/user codes.
[[nodiscard]] ForgeResult<DeviceCode> request_device_code(const OAuthProvider& provider,
                                                          HttpClient& http);

// Poll the token endpoint once. authorization_pending and slow_down are
// ordinary states (not errors); only a transport/parse failure is a ForgeError.
[[nodiscard]] ForgeResult<TokenGrant> poll_token(const OAuthProvider& provider,
                                                 const std::string& device_code,
                                                 HttpClient& http);

// The response parsers, exposed for testing (pure — no HTTP).
[[nodiscard]] ForgeResult<DeviceCode> parse_device_code(const std::string& json_body);
[[nodiscard]] ForgeResult<TokenGrant> parse_token_response(const std::string& json_body);

} // namespace repomancer::forge
