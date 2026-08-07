// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/forge/api.h>

#include "json_util.h"

#include <nlohmann/json.hpp>

#include <string>

namespace repomancer::forge {

ForgeApi github_api() {
    return {"GitHub", "https://api.github.com/user/keys"};
}

ForgeApi gitlab_api(const std::string& base_url) {
    return {"GitLab", base_url + "/api/v4/user/keys"};
}

ForgeResult<UploadedKey> parse_uploaded_key(long status, const std::string& body) {
    const auto j = detail::parse_object(body);
    if (status >= 200 && status < 300) {
        if (!j) {
            return ForgeError{ForgeError::Kind::ParseError,
                              "key upload response was not a JSON object", status};
        }
        UploadedKey key;
        key.id = detail::json_id(*j, "id");
        key.title = detail::json_str(*j, "title");
        return key;
    }
    // Non-2xx: surface the forge's message where it is a plain string (GitHub,
    // and GitLab's simple errors); otherwise a trimmed body. GitHub returns 422
    // when the key is already present or malformed.
    std::string message = j ? detail::json_str(*j, "message") : std::string{};
    if (message.empty()) {
        message = body.substr(0, 500);
    }
    return ForgeError{ForgeError::Kind::HttpStatus, "key upload failed: " + message,
                      status};
}

ForgeResult<UploadedKey> upload_ssh_key(const ForgeApi& api, const std::string& token,
                                        const std::string& title,
                                        const std::string& public_key, HttpClient& http) {
    nlohmann::json payload;
    payload["title"] = title;
    payload["key"] = public_key;

    HttpRequest req;
    req.method = HttpMethod::Post;
    req.url = api.keys_url;
    req.headers = {{"Accept", "application/json"},
                   {"Content-Type", "application/json"},
                   {"Authorization", "Bearer " + token}};
    req.body = payload.dump();

    auto resp = http.send(req);
    if (!resp.ok()) {
        return resp.error();
    }
    return parse_uploaded_key(resp.value().status, resp.value().body);
}

} // namespace repomancer::forge
