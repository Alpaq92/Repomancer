// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Forge REST calls that use an OAuth token (implementation-plan.md §6). The
// wizard's upload step lives here: POST the generated public key to the user's
// forge account. Wx-free and testable through the HttpClient seam; the token
// travels only in the Authorization header, never argv/env.

#pragma once

#include <repomancer/forge/http.h>

#include <string>

namespace repomancer::forge {

struct ForgeApi {
    std::string name;
    std::string keys_url; // POST target for uploading an SSH public key
};

[[nodiscard]] ForgeApi github_api();
[[nodiscard]] ForgeApi gitlab_api(const std::string& base_url = "https://gitlab.com");

struct UploadedKey {
    std::string id;    // the forge's id for the created key
    std::string title;
};

// Parse a "create SSH key" response. A 2xx status yields the created key;
// anything else is a ForgeError carrying the forge's message. Pure — no HTTP.
[[nodiscard]] ForgeResult<UploadedKey> parse_uploaded_key(long status,
                                                          const std::string& body);

// POST `public_key` (a full "ssh-ed25519 AAAA… comment" line) under `title`,
// authenticated with a Bearer `token`. Network via `http`.
[[nodiscard]] ForgeResult<UploadedKey> upload_ssh_key(const ForgeApi& api,
                                                      const std::string& token,
                                                      const std::string& title,
                                                      const std::string& public_key,
                                                      HttpClient& http);

} // namespace repomancer::forge
