// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// ssh-agent tests drive a throwaway agent (its own socket, killed on teardown)
// so add/list/remove run for real. Skipped where ssh-agent/ssh-add are absent.

#include <repomancer/process/process_runner.h>
#include <repomancer/ssh/agent.h>
#include <repomancer/ssh/keys.h>

#include <catch2/catch_test_macros.hpp>

#include <cctype>
#include <filesystem>
#include <random>
#include <string>

using namespace repomancer::ssh;
namespace fs = std::filesystem;
namespace proc = repomancer::proc;

namespace {

// A private ssh-agent bound to a socket under a temp dir; torn down (and
// killed) on destruction. `ok` is false if the agent could not be started.
struct AgentFixture {
    fs::path dir;
    std::string sock;
    std::string pid;
    bool ok = false;

    AgentFixture() {
        std::random_device rd;
        dir = fs::temp_directory_path() / ("repomancer-agent-" + std::to_string(rd()));
        std::error_code ec;
        fs::create_directories(dir, ec);
        sock = (dir / "agent.sock").string();

        proc::RunSpec spec;
        spec.exe = "ssh-agent";
        spec.args = {"-a", sock};
        const auto run = proc::ProcessRunner::run(spec);
        if (!run.ok()) return;
        pid = extract(run.out, "SSH_AGENT_PID=");
        ok = !pid.empty() && fs::exists(sock);
    }

    ~AgentFixture() {
        if (!pid.empty()) {
            proc::RunSpec kill;
            kill.exe = "ssh-agent";
            kill.args = {"-k"};
            kill.env_extra["SSH_AGENT_PID"] = pid;
            kill.env_extra["SSH_AUTH_SOCK"] = sock;
            proc::ProcessRunner::run(kill);
        }
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    [[nodiscard]] AgentConfig config() const {
        AgentConfig c;
        c.auth_sock = sock;
        return c;
    }

    // The digits of "<key>=<digits>;" in ssh-agent's shell output.
    static std::string extract(const std::string& text, const std::string& key) {
        const auto at = text.find(key);
        if (at == std::string::npos) return {};
        std::string out;
        for (std::size_t i = at + key.size(); i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])); ++i) {
            out.push_back(text[i]);
        }
        return out;
    }
};

// Generate a fresh key under `dir`; returns its KeyInfo (fingerprint etc.).
KeyInfo make_key(const fs::path& dir, const std::string& name,
                 const std::string& passphrase = {}) {
    GenerateRequest req;
    req.type = KeyType::Ed25519;
    req.path = dir / name;
    req.comment = name + "@agent-test";
    req.passphrase = passphrase;
    auto r = generate(req);
    REQUIRE(r.ok());
    return std::move(r).value();
}

bool ssh_add_available() {
    proc::RunSpec spec;
    spec.exe = "ssh-add";
    spec.args = {"-l"};
    return proc::ProcessRunner::run(spec).status != proc::LaunchStatus::ExeNotFound;
}

} // namespace

TEST_CASE("agent_list reports an empty agent as NoIdentities, not an error") {
    if (!ssh_add_available()) SKIP("ssh-add not found on PATH");
    AgentFixture agent;
    if (!agent.ok) SKIP("could not start a throwaway ssh-agent");

    const auto r = agent_list(agent.config());
    REQUIRE(r.ok());
    CHECK(r.value().state == AgentReachability::NoIdentities);
    CHECK(r.value().identities.empty());
}

TEST_CASE("agent_add then agent_list shows the identity") {
    if (!ssh_add_available()) SKIP("ssh-add not found on PATH");
    AgentFixture agent;
    if (!agent.ok) SKIP("could not start a throwaway ssh-agent");

    const KeyInfo key = make_key(agent.dir, "id_plain");
    REQUIRE(agent_add(key.private_path, {}, agent.config()).ok());

    const auto r = agent_list(agent.config());
    REQUIRE(r.ok());
    CHECK(r.value().state == AgentReachability::Running);
    REQUIRE(r.value().identities.size() == 1);
    CHECK(r.value().identities[0].fingerprint_sha256 == key.fingerprint_sha256);
    CHECK(r.value().identities[0].type == KeyType::Ed25519);
}

TEST_CASE("agent_add loads a passphrase-protected key via stdin") {
    if (!ssh_add_available()) SKIP("ssh-add not found on PATH");
    AgentFixture agent;
    if (!agent.ok) SKIP("could not start a throwaway ssh-agent");

    const KeyInfo key = make_key(agent.dir, "id_prot", "s3cret-agent-pass");
    REQUIRE(agent_add(key.private_path, "s3cret-agent-pass", agent.config()).ok());

    const auto r = agent_list(agent.config());
    REQUIRE(r.ok());
    REQUIRE(r.value().identities.size() == 1);
    CHECK(r.value().identities[0].fingerprint_sha256 == key.fingerprint_sha256);
}

TEST_CASE("agent_add of a protected key with the wrong passphrase fails cleanly") {
    if (!ssh_add_available()) SKIP("ssh-add not found on PATH");
    AgentFixture agent;
    if (!agent.ok) SKIP("could not start a throwaway ssh-agent");

    const KeyInfo key = make_key(agent.dir, "id_prot", "the-right-one");
    // Wrong passphrase must return an error (and, crucially, not hang waiting on
    // a GUI askpass — SSH_ASKPASS_REQUIRE=never keeps it on stdin).
    const auto r = agent_add(key.private_path, "the-wrong-one", agent.config());
    REQUIRE_FALSE(r.ok());
    CHECK(agent_list(agent.config()).value().identities.empty());
}

TEST_CASE("agent_remove drops a single identity") {
    if (!ssh_add_available()) SKIP("ssh-add not found on PATH");
    AgentFixture agent;
    if (!agent.ok) SKIP("could not start a throwaway ssh-agent");

    const KeyInfo key = make_key(agent.dir, "id_plain");
    REQUIRE(agent_add(key.private_path, {}, agent.config()).ok());
    REQUIRE(agent_remove(key.private_path, agent.config()).ok());

    const auto r = agent_list(agent.config());
    REQUIRE(r.ok());
    CHECK(r.value().state == AgentReachability::NoIdentities);
}

TEST_CASE("agent_remove_all empties the agent") {
    if (!ssh_add_available()) SKIP("ssh-add not found on PATH");
    AgentFixture agent;
    if (!agent.ok) SKIP("could not start a throwaway ssh-agent");

    REQUIRE(agent_add(make_key(agent.dir, "id_a").private_path, {}, agent.config()).ok());
    REQUIRE(agent_add(make_key(agent.dir, "id_b").private_path, {}, agent.config()).ok());
    REQUIRE(agent_remove_all(agent.config()).ok());

    CHECK(agent_list(agent.config()).value().identities.empty());
}

TEST_CASE("agent_list against a dead socket reports Unavailable") {
    if (!ssh_add_available()) SKIP("ssh-add not found on PATH");
    AgentConfig cfg;
    cfg.auth_sock = "/nonexistent/repomancer/agent.sock";
    const auto r = agent_list(cfg);
    REQUIRE(r.ok()); // not reachable is a state, not an error
    CHECK(r.value().state == AgentReachability::Unavailable);
}

TEST_CASE("a missing ssh-add binary is reported, not crashed") {
    AgentConfig cfg;
    cfg.ssh_add_binary = "repomancer-no-such-ssh-add-9c2b";
    const auto r = agent_list(cfg);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == SshError::Kind::ExeNotFound);
}
