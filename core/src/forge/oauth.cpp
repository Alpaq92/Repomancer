// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/forge/oauth.h>

#include "json_util.h"

#include <cctype>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace repomancer::forge {
namespace {

// OAuth device flow needs the *public* client_id of a registered OAuth app
// (not a secret). TODO(m3): register the Repomancer apps and embed the IDs
// here; until then the flow cannot run against the real forges and the wizard
// should show a clear "OAuth not configured yet" message. Tests do not depend
// on the value.
constexpr const char* kGithubClientId = ""; // e.g. "Iv1.0123456789abcdef"
constexpr const char* kGitlabClientId = "";

// x-www-form-urlencode one value (RFC 3986 unreserved kept, all else %XX).
std::string form_encode(std::string_view s) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (const unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

std::string form_body(std::initializer_list<std::pair<const char*, std::string>> fields) {
    std::string body;
    for (const auto& [key, value] : fields) {
        if (!body.empty()) {
            body.push_back('&');
        }
        body += key;
        body.push_back('=');
        body += form_encode(value);
    }
    return body;
}

HttpRequest form_post(const std::string& url, std::string body) {
    HttpRequest req;
    req.method = HttpMethod::Post;
    req.url = url;
    req.headers = {{"Accept", "application/json"},
                   {"Content-Type", "application/x-www-form-urlencoded"}};
    req.body = std::move(body);
    return req;
}

} // namespace

OAuthProvider github_provider() {
    return {"GitHub", "https://github.com/login/device/code",
            "https://github.com/login/oauth/access_token", kGithubClientId,
            "write:public_key"};
}

OAuthProvider gitlab_provider(const std::string& base_url) {
    return {"GitLab", base_url + "/oauth/authorize_device", base_url + "/oauth/token",
            kGitlabClientId, "api"};
}

ForgeResult<DeviceCode> parse_device_code(const std::string& json_body) {
    const auto j = detail::parse_object(json_body);
    if (!j) {
        return ForgeError{ForgeError::Kind::ParseError,
                          "device code response was not a JSON object"};
    }
    DeviceCode dc;
    dc.device_code = detail::json_str(*j, "device_code");
    dc.user_code = detail::json_str(*j, "user_code");
    dc.verification_uri = detail::json_str(*j, "verification_uri");
    dc.verification_uri_complete = detail::json_str(*j, "verification_uri_complete");
    dc.interval = detail::json_int(*j, "interval", 5);
    dc.expires_in = detail::json_int(*j, "expires_in", 900);
    if (dc.device_code.empty() || dc.user_code.empty() || dc.verification_uri.empty()) {
        return ForgeError{ForgeError::Kind::ParseError,
                          "device code response missing required fields"};
    }
    return dc;
}

ForgeResult<TokenGrant> parse_token_response(const std::string& json_body) {
    const auto j = detail::parse_object(json_body);
    if (!j) {
        return ForgeError{ForgeError::Kind::ParseError,
                          "token response was not a JSON object"};
    }
    if (const std::string token = detail::json_str(*j, "access_token"); !token.empty()) {
        TokenGrant g;
        g.state = PollState::Authorized;
        g.access_token = token;
        g.token_type = detail::json_str(*j, "token_type");
        g.scope = detail::json_str(*j, "scope");
        return g;
    }
    const std::string error = detail::json_str(*j, "error");
    TokenGrant g;
    if (error == "authorization_pending") {
        g.state = PollState::Pending;
    } else if (error == "slow_down") {
        g.state = PollState::SlowDown;
    } else if (error == "access_denied") {
        g.state = PollState::Denied;
    } else if (error == "expired_token") {
        g.state = PollState::Expired;
    } else {
        return ForgeError{ForgeError::Kind::OAuthError,
                          error.empty() ? "token response had neither access_token "
                                          "nor error"
                                        : "OAuth error: " + error};
    }
    return g;
}

ForgeResult<DeviceCode> request_device_code(const OAuthProvider& provider,
                                            HttpClient& http) {
    auto resp = http.send(form_post(
        provider.device_code_url,
        form_body({{"client_id", provider.client_id}, {"scope", provider.scope}})));
    if (!resp.ok()) {
        return resp.error();
    }
    const auto& r = resp.value();
    if (r.status < 200 || r.status >= 300) {
        return ForgeError{ForgeError::Kind::HttpStatus,
                          "device code request failed", r.status};
    }
    return parse_device_code(r.body);
}

ForgeResult<TokenGrant> poll_token(const OAuthProvider& provider,
                                   const std::string& device_code, HttpClient& http) {
    auto resp = http.send(form_post(
        provider.token_url,
        form_body({{"client_id", provider.client_id},
                   {"device_code", device_code},
                   {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"}})));
    if (!resp.ok()) {
        return resp.error();
    }
    // OAuth errors (authorization_pending, slow_down, …) arrive in the body,
    // with HTTP 200 (GitHub) or 400 (GitLab) — so the body, not the status,
    // decides. A non-JSON body (a real 5xx) falls through to a ParseError.
    return parse_token_response(resp.value().body);
}

} // namespace repomancer::forge
