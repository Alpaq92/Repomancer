// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Forge key-upload core, offline via a fake HttpClient and fixture JSON.

#include <repomancer/forge/api.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace repomancer::forge;

namespace {
struct FakeHttp : HttpClient {
    ForgeResult<HttpResponse> result = HttpResponse{200, ""};
    HttpRequest last;
    ForgeResult<HttpResponse> send(const HttpRequest& request) override {
        last = request;
        return result;
    }
};

bool has_header(const HttpRequest& r, const std::string& name, const std::string& value) {
    for (const auto& [k, v] : r.headers) {
        if (k == name && v == value) {
            return true;
        }
    }
    return false;
}
} // namespace

TEST_CASE("parse_uploaded_key: created (numeric id)") {
    const auto r = parse_uploaded_key(
        201, R"({"id":42,"key":"ssh-ed25519 AAAA","title":"laptop"})");
    REQUIRE(r.ok());
    CHECK(r.value().id == "42");
    CHECK(r.value().title == "laptop");
}

TEST_CASE("parse_uploaded_key: a 422 carries the forge message") {
    const auto r = parse_uploaded_key(422, R"({"message":"Validation Failed"})");
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == ForgeError::Kind::HttpStatus);
    CHECK(r.error().http_status == 422);
    CHECK(r.error().message.find("Validation Failed") != std::string::npos);
}

TEST_CASE("parse_uploaded_key: a 2xx with junk body is a parse error") {
    const auto r = parse_uploaded_key(201, "not json");
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == ForgeError::Kind::ParseError);
}

TEST_CASE("upload_ssh_key posts an authenticated JSON body") {
    FakeHttp http;
    http.result = HttpResponse{201, R"({"id":7,"title":"work laptop"})"};

    const auto r = upload_ssh_key(github_api(), "gho_secret", "work laptop",
                                  "ssh-ed25519 AAAAC3Nz key@host", http);
    REQUIRE(r.ok());
    CHECK(r.value().id == "7");

    CHECK(http.last.method == HttpMethod::Post);
    CHECK(http.last.url == "https://api.github.com/user/keys");
    CHECK(has_header(http.last, "Authorization", "Bearer gho_secret"));
    CHECK(has_header(http.last, "Content-Type", "application/json"));
    CHECK(http.last.body.find("\"key\":\"ssh-ed25519 AAAAC3Nz key@host\"") !=
          std::string::npos);
    CHECK(http.last.body.find("\"title\":\"work laptop\"") != std::string::npos);
}

TEST_CASE("upload_ssh_key surfaces a transport failure") {
    FakeHttp http;
    http.result = ForgeError{ForgeError::Kind::Transport, "offline"};
    const auto r = upload_ssh_key(github_api(), "t", "x", "ssh-ed25519 AAAA", http);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == ForgeError::Kind::Transport);
}

TEST_CASE("gitlab_api targets the v4 keys endpoint") {
    CHECK(gitlab_api().keys_url == "https://gitlab.com/api/v4/user/keys");
    CHECK(gitlab_api("https://gitlab.example.com").keys_url ==
          "https://gitlab.example.com/api/v4/user/keys");
}
