# Repomancer

A cross-platform (Windows / macOS / Linux) visual client for version control —
deeply inspired by TortoiseGit. Repomancer is a GUI frontend that drives the
**locally installed** `git` (and, in later releases, `svn` and `hg`) binaries:
commit graphs, visual staging and diffing, SSH key management, GitHub/GitLab
integration, and Tortoise-style file-manager integration (context menus and
status overlays).

**Status: pre-alpha — M0 scaffolding.** See the
[implementation plan](docs/implementation-plan.md) for the full design and
roadmap, [stack analysis](docs/stack-analysis.md) for why C++20 + wxWidgets,
and [permissive VCS implementations](docs/permissive-vcs-implementations.md)
for the research that preceded it.

## Building

Prerequisites: CMake ≥ 3.25, Ninja, a C++20 compiler, and
[vcpkg](https://github.com/microsoft/vcpkg) (`VCPKG_ROOT` must point at it).
On Linux, building the GUI additionally needs GTK 3 development headers.

```sh
# core library + tests (no GUI):
cmake --preset linux-core-debug
cmake --build --preset linux-core-debug
ctest --preset linux-core-debug

# with the wxWidgets GUI (vcpkg builds wx on first run — slow):
cmake --preset linux-debug
cmake --build --preset linux-debug
```

Presets exist for `linux-*`, `macos-*`, and `windows-msvc-*`; see
`CMakePresets.json`.

Integration tests require a `git` binary (≥ 2.25) on `PATH`.

## Repository layout

| Path | Contents |
|---|---|
| `core/` | `librepomancer-core` — VCS drivers, models, parsers. **No wx dependency.** |
| `gui/` | wxWidgets application |
| `tests/` | Catch2 unit + integration tests |
| `docs/` | planning documents, IPC schema |
| `cmake/` | shared build logic (hardening, warnings) |

## Security

See [SECURITY.md](SECURITY.md) for the reporting process and
[docs/implementation-plan.md §13](docs/implementation-plan.md) for the
security design.

## License

[Apache-2.0](LICENSE). Contributions require a DCO sign-off — see
[CONTRIBUTING.md](CONTRIBUTING.md).
