# Repomancer

[![CI](https://github.com/Alpaq92/Repomancer/actions/workflows/ci.yml/badge.svg)](https://github.com/Alpaq92/Repomancer/actions/workflows/ci.yml)
[![License: Apache-2.0](https://img.shields.io/badge/License-Apache--2.0-blue.svg)](LICENSE)

**Visual version control for Windows, macOS and Linux — commit graphs, staging,
shell integration.**

Repomancer is a cross-platform GUI frontend deeply inspired by TortoiseGit. It
drives the **locally installed** `git` binary (with `svn` and `hg` planned for
the 1.x line): commit graphs, visual staging and diffing, SSH key management,
GitHub/GitLab integration, and Tortoise-style file-manager integration —
context menus and status overlays in Explorer, Finder, and the major Linux
file managers.

**Status: pre-alpha (M0/M1).** The core git driver, its test suite, and a
minimal wxWidgets shell exist; everything else is under construction. The full
design lives in the [implementation plan](docs/implementation-plan.md), with
the [stack analysis](docs/stack-analysis.md) and the
[permissive VCS implementation research](docs/permissive-vcs-implementations.md)
that preceded it.

## Building

Prerequisites: CMake ≥ 3.25, Ninja, a C++20 compiler, and
[vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` set. Integration
tests need `git` ≥ 2.25 on `PATH`.

```sh
git clone https://github.com/Alpaq92/Repomancer.git
cd Repomancer
git clone --depth 1 https://github.com/microsoft/vcpkg.git vcpkg
./vcpkg/bootstrap-vcpkg.sh -disableMetrics
export VCPKG_ROOT=$PWD/vcpkg

# Core library + tests (no GUI):
cmake --preset linux-core-debug
cmake --build --preset linux-core-debug
ctest --preset linux-core-debug

# GUI against system wxWidgets (Debian/Ubuntu: apt install libwxgtk3.2-dev):
cmake --preset linux-debug-syswx
cmake --build --preset linux-debug-syswx
./build/linux-debug-syswx/gui/repomancer

# GUI with vcpkg-built wxWidgets 3.3 (first build is slow):
cmake --preset linux-debug
```

Presets exist for `linux-*`, `macos-*`, and `windows-msvc-*` — see
`CMakePresets.json`.

## Repository layout

| Path | Contents |
|---|---|
| `core/` | `librepomancer-core` — VCS drivers, parsers, settings. **No wx dependency.** |
| `gui/` | wxWidgets application |
| `tests/` | Catch2 unit + integration tests (fixture repos drive real `git`) |
| `docs/` | planning documents, statusd IPC schema |
| `cmake/` | shared build logic (hardening, warnings) |

## Contributing & security

Contributions are welcome under Apache-2.0 with a DCO sign-off — see
[CONTRIBUTING.md](CONTRIBUTING.md). Please report vulnerabilities privately
per [SECURITY.md](SECURITY.md); the threat model is documented in
[docs/implementation-plan.md §13](docs/implementation-plan.md).

## License

[Apache-2.0](LICENSE) © 2026 Roman Chojnacki and the Repomancer contributors.
