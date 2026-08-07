// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// OAuth device-flow core, exercised entirely offline through a fake HttpClient
// and fixture JSON — no network, no real forge.

#include <repomancer/forge/oauth.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace repomancer::forge;

namespace {

// Returns a canned response (or error) and records the request it was handed.
struct FakeHttp : HttpClient {
    ForgeResult<HttpResponse> result = HttpResponse{200, ""};
    HttpRequest last;

    ForgeResult<HttpResponse> send(const HttpRequest& request) override {
        last = request;
        return result;
    }
};

constexpr const char* kDeviceJson = R"({
  "device_code": "3584d83530557fdd1f46af8289938c8ef79f9dc5",
  "user_code": "WDJB-MJHT",
  "verification_uri": "https://github.com/login/device",
  "expires_in": 900,
  "interval": 5
})";

} // namespace

TEST_CASE("parse_device_code reads the fields") {
    const auto r = parse_device_code(kDeviceJson);
    REQUIRE(r.ok());
    CHECK(r.value().user_code == "WDJB-MJHT");
    CHECK(r.value().verification_uri == "https://github.com/login/device");
    CHECK(r.value().interval == 5);
    CHECK(r.value().expires_in == 900);
    CHECK(r.value().device_code.rfind("3584", 0) == 0);
}

TEST_CASE("parse_device_code rejects missing fields and junk") {
    CHECK_FALSE(parse_device_code(R"({"user_code":"X"})").ok()); // no device_code/uri
    CHECK_FALSE(parse_device_code("not json").ok());
    CHECK_FALSE(parse_device_code("[]").ok()); // not an object
}

TEST_CASE("parse_token_response: authorized") {
    const auto r = parse_token_response(
        R"({"access_token":"gho_abc","token_type":"bearer","scope":"write:public_key"})");
    REQUIRE(r.ok());
    CHECK(r.value().state == PollState::Authorized);
    CHECK(r.value().access_token == "gho_abc");
    CHECK(r.value().scope == "write:public_key");
}

TEST_CASE("parse_token_response: the pending/slow_down/denied/expired states") {
    CHECK(parse_token_response(R"({"error":"authorization_pending"})").value().state ==
          PollState::Pending);
    CHECK(parse_token_response(R"({"error":"slow_down"})").value().state ==
          PollState::SlowDown);
    CHECK(parse_token_response(R"({"error":"access_denied"})").value().state ==
          PollState::Denied);
    CHECK(parse_token_response(R"({"error":"expired_token"})").value().state ==
          PollState::Expired);
}

TEST_CASE("parse_token_response: an unknown error is an OAuthError") {
    const auto r = parse_token_response(R"({"error":"tea_pot"})");
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == ForgeError::Kind::OAuthError);
}

TEST_CASE("request_device_code posts the right shape and parses the reply") {
    FakeHttp http;
    http.result = HttpResponse{200, kDeviceJson};

    const auto r = request_device_code(github_provider(), http);
    REQUIRE(r.ok());
    CHECK(r.value().user_code == "WDJB-MJHT");

    CHECK(http.last.method == HttpMethod::Post);
    CHECK(http.last.url == "https://github.com/login/device/code");
    // The scope's colon must be percent-encoded in the form body.
    CHECK(http.last.body.find("scope=write%3Apublic_key") != std::string::npos);
    CHECK(http.last.body.find("client_id=") != std::string::npos);
}

TEST_CASE("request_device_code surfaces a transport failure") {
    FakeHttp http;
    http.result = ForgeError{ForgeError::Kind::Transport, "no network"};
    const auto r = request_device_code(github_provider(), http);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == ForgeError::Kind::Transport);
}

TEST_CASE("request_device_code treats a non-2xx as an HttpStatus error") {
    FakeHttp http;
    http.result = HttpResponse{404, "nope"};
    const auto r = request_device_code(github_provider(), http);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == ForgeError::Kind::HttpStatus);
    CHECK(r.error().http_status == 404);
}

TEST_CASE("poll_token sends the device grant and returns pending") {
    FakeHttp http;
    http.result = HttpResponse{200, R"({"error":"authorization_pending"})"};
    const auto r = poll_token(github_provider(), "dev-code-xyz", http);
    REQUIRE(r.ok());
    CHECK(r.value().state == PollState::Pending);
    CHECK(http.last.url == "https://github.com/login/oauth/access_token");
    CHECK(http.last.body.find("device_code=dev-code-xyz") != std::string::npos);
    CHECK(http.last.body.find("grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type"
                              "%3Adevice_code") != std::string::npos);
}

TEST_CASE("poll_token returns the token once authorized (GitLab endpoints)") {
    FakeHttp http;
    http.result = HttpResponse{200, R"({"access_token":"glpat-xyz","token_type":"bearer"})"};
    const auto prov = gitlab_provider();
    const auto r = poll_token(prov, "dc", http);
    REQUIRE(r.ok());
    CHECK(r.value().state == PollState::Authorized);
    CHECK(r.value().access_token == "glpat-xyz");
    CHECK(prov.token_url == "https://gitlab.com/oauth/token");
}
