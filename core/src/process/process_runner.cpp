// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Repomancer contributors

#include <repomancer/process/process_runner.h>

#include <reproc++/drain.hpp>
#include <reproc++/reproc.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <cstdlib> // _environ
#else
extern "C" char** environ;
#endif

namespace repomancer::proc {

namespace {

// The child environment as inherited-plus-overrides. reproc's env::extend
// appends the extras *after* the inherited variables, and getenv() returns the
// first match — so an inherited value would silently shadow an override
// (defeating LC_ALL=C, SSH_AUTH_SOCK, and the git driver's env discipline).
// Enumerate the parent environment and let env_extra win. Empty (only the
// extras) if the platform environ pointer could not be read.
std::map<std::string, std::string> merged_environment(
    const std::map<std::string, std::string>& extra) {
#if defined(_WIN32)
    char** env = _environ;
#else
    char** env = environ;
#endif
    std::map<std::string, std::string> merged;
    for (char** e = env; e != nullptr && *e != nullptr; ++e) {
        const std::string entry(*e);
        const auto eq = entry.find('=');
        if (eq != std::string::npos) {
            merged.emplace(entry.substr(0, eq), entry.substr(eq + 1));
        }
    }
    for (const auto& [name, value] : extra) {
        merged[name] = value; // override or add
    }
    return merged;
}

struct StreamingSink {
    std::string& capture;
    bool is_stderr;
    const ChunkSink* chunk_sink;
    const std::atomic<bool>* cancel;
    bool* cancelled;

    std::error_code operator()(reproc::stream /*stream*/, const std::uint8_t* buffer,
                               std::size_t size) {
        if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) {
            *cancelled = true;
            return std::make_error_code(std::errc::operation_canceled); // stop draining
        }
        const auto* chars = reinterpret_cast<const char*>(buffer);
        capture.append(chars, size);
        if (chunk_sink != nullptr && *chunk_sink) {
            (*chunk_sink)(std::string_view(chars, size), is_stderr);
        }
        return {};
    }
};

RunResult run_impl(const RunSpec& spec, const ChunkSink* sink, const std::atomic<bool>* cancel) {
    RunResult result;

    reproc::options options;
    // Capture everything explicitly — reproc's defaults do not pipe stderr.
    options.redirect.in.type = reproc::redirect::pipe;
    options.redirect.out.type = reproc::redirect::pipe;
    options.redirect.err.type = reproc::redirect::pipe;
    std::string cwd_storage;
    if (spec.cwd) {
        cwd_storage = spec.cwd->string();
        options.working_directory = cwd_storage.c_str();
    }
    if (!spec.env_extra.empty()) {
        // Supply the full merged environment so extras override same-named
        // inherited vars. If the parent env could not be read (merged holds
        // only the extras), fall back to extend so the child still inherits a
        // working environment — losing only the override guarantee.
        auto merged = merged_environment(spec.env_extra);
        if (merged.size() > spec.env_extra.size()) {
            options.env.behavior = reproc::env::empty;
            options.env.extra = merged;
        } else {
            options.env.behavior = reproc::env::extend;
            options.env.extra = spec.env_extra;
        }
    }
    options.deadline = reproc::milliseconds(static_cast<int>(spec.timeout.count()));
    options.stop = reproc::stop_actions{
        {reproc::stop::wait, reproc::milliseconds(250)},
        {reproc::stop::terminate, reproc::milliseconds(2000)},
        {reproc::stop::kill, reproc::milliseconds(1000)},
    };

    std::vector<std::string> argv;
    argv.reserve(spec.args.size() + 1);
    argv.push_back(spec.exe.string());
    argv.insert(argv.end(), spec.args.begin(), spec.args.end());

    reproc::process process;
    std::error_code ec = process.start(argv, options);
    if (ec) {
        result.status = (ec == std::errc::no_such_file_or_directory)
                            ? LaunchStatus::ExeNotFound
                            : LaunchStatus::LaunchFailed;
        result.err = ec.message();
        return result;
    }

    if (!spec.stdin_data.empty()) {
        std::size_t offset = 0;
        while (offset < spec.stdin_data.size()) {
            const auto [written, write_ec] = process.write(
                reinterpret_cast<const std::uint8_t*>(spec.stdin_data.data()) + offset,
                spec.stdin_data.size() - offset);
            if (write_ec) {
                result.status = LaunchStatus::IoError;
                result.err = write_ec.message();
                process.stop(options.stop);
                return result;
            }
            offset += written;
        }
    }
    if (spec.close_stdin) {
        process.close(reproc::stream::in);
    }

    bool cancelled = false;
    StreamingSink out_sink{result.out, false, sink, cancel, &cancelled};
    StreamingSink err_sink{result.err, true, sink, cancel, &cancelled};
    ec = reproc::drain(process, out_sink, err_sink);

    if (cancelled) {
        result.status = LaunchStatus::Cancelled;
        process.stop(options.stop);
        return result;
    }
    if (ec == std::errc::timed_out) {
        result.status = LaunchStatus::TimedOut;
        process.stop(options.stop);
        return result;
    }
    if (ec) {
        result.status = LaunchStatus::IoError;
        process.stop(options.stop);
        return result;
    }

    auto [exit_code, stop_ec] = process.stop(options.stop);
    if (stop_ec == std::errc::timed_out) {
        result.status = LaunchStatus::TimedOut;
        return result;
    }
    if (stop_ec) {
        result.status = LaunchStatus::IoError;
        result.err += stop_ec.message();
        return result;
    }
    result.exit_code = exit_code;
    return result;
}

} // namespace

RunResult ProcessRunner::run(const RunSpec& spec) { return run_impl(spec, nullptr, nullptr); }

RunResult ProcessRunner::run_streaming(const RunSpec& spec, const ChunkSink& sink,
                                       const std::atomic<bool>* cancel) {
    return run_impl(spec, &sink, cancel);
}

} // namespace repomancer::proc
