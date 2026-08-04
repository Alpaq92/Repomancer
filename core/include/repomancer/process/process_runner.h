// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// The single choke point through which every external tool is spawned.
// Rules enforced here (implementation-plan.md §13.3): argv exec only — never a
// shell; explicit binary path or PATH lookup by the OS loader; bounded by
// timeout; secrets travel via stdin, never argv/env.

#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace repomancer::proc {

enum class LaunchStatus {
    Ok,
    ExeNotFound,
    LaunchFailed,
    TimedOut,
    Cancelled,
    IoError,
};

struct RunSpec {
    std::filesystem::path exe;
    std::vector<std::string> args;
    std::optional<std::filesystem::path> cwd;
    // Added to (or overriding) the inherited environment of the child.
    std::map<std::string, std::string> env_extra;
    std::chrono::milliseconds timeout{std::chrono::milliseconds(120'000)};
    // Written to the child's stdin before reading output.
    std::string stdin_data;
    // Close stdin after writing stdin_data (tools that read-until-EOF need
    // this). Set false only in tests that exercise timeouts.
    bool close_stdin = true;
};

struct RunResult {
    LaunchStatus status = LaunchStatus::Ok;
    int exit_code = -1;
    std::string out;
    std::string err;

    [[nodiscard]] bool ok() const { return status == LaunchStatus::Ok && exit_code == 0; }
};

// chunk is raw bytes as read from the pipe; called on the draining thread.
using ChunkSink = std::function<void(std::string_view chunk, bool is_stderr)>;

class ProcessRunner {
public:
    // Full-capture run. Blocks up to spec.timeout.
    static RunResult run(const RunSpec& spec);

    // Streams chunks to `sink` as they arrive; stdout/stderr are still
    // captured in the result (bounded by the same limits as run()).
    // If `cancel` becomes true the child is stopped and status is Cancelled.
    static RunResult run_streaming(const RunSpec& spec, const ChunkSink& sink,
                                   const std::atomic<bool>* cancel = nullptr);
};

} // namespace repomancer::proc
