// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors
//
// Internal (not shipped in include/): map a ProcessRunner spawn/exit failure to
// an SshError. Shared by every ssh-* wrapper (keys, agent) so the launch-status
// translation lives in one place.

#pragma once

#include <repomancer/process/process_runner.h>
#include <repomancer/ssh/keys.h>

#include <cstddef>
#include <filesystem>
#include <string>

namespace repomancer::ssh::detail {

inline std::string excerpt(const std::string& text) {
    constexpr std::size_t kMaxBytes = 2'048;
    return text.substr(0, kMaxBytes);
}

// Only call when !run.ok(). `binary` names the tool for the message.
inline SshError from_run_failure(const proc::RunResult& run,
                                 const std::filesystem::path& binary) {
    using Kind = SshError::Kind;
    const std::string bin = binary.string();
    switch (run.status) {
    case proc::LaunchStatus::ExeNotFound:
        return {Kind::ExeNotFound, bin + " not found", -1, excerpt(run.err)};
    case proc::LaunchStatus::TimedOut:
        return {Kind::Timeout, bin + " timed out", -1, excerpt(run.err)};
    case proc::LaunchStatus::Cancelled:
        return {Kind::Cancelled, "operation cancelled", -1, {}};
    case proc::LaunchStatus::LaunchFailed:
    case proc::LaunchStatus::IoError:
        return {Kind::LaunchFailed, "failed to run " + bin, -1, excerpt(run.err)};
    case proc::LaunchStatus::Ok:
        break;
    }
    return {Kind::NonZeroExit, bin + " exited with an error", run.exit_code,
            excerpt(run.err)};
}

} // namespace repomancer::ssh::detail
