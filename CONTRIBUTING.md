# Contributing to Repomancer

Thank you for considering a contribution!

## Getting started

```sh
git clone https://github.com/Alpaq92/Repomancer.git
cd Repomancer
git clone --depth 1 https://github.com/microsoft/vcpkg.git vcpkg
./vcpkg/bootstrap-vcpkg.sh -disableMetrics
export VCPKG_ROOT=$PWD/vcpkg
cmake --preset linux-core-debug && cmake --build --preset linux-core-debug
ctest --preset linux-core-debug
```

The [implementation plan](docs/implementation-plan.md) is the source of truth
for architecture and roadmap — read the relevant section before starting
non-trivial work, and open an issue to discuss anything that changes it.

## Ground rules

- **License:** all contributions are accepted under [Apache-2.0](LICENSE).
- **DCO:** every commit must carry a `Signed-off-by:` line certifying the
  [Developer Certificate of Origin](https://developercertificate.org/):

  ```sh
  git commit -s
  # produces: Signed-off-by: Your Name <you@example.com>
  ```

- **SPDX:** every new source file starts with
  `// SPDX-License-Identifier: Apache-2.0`.
- **No GPL-derived code — ever.** TortoiseGit, TortoiseSVN, TortoiseHg,
  RabbitVCS, git-cola and the Git/Mercurial sources are GPL: they may be
  studied for behavior, but code must never be copied or closely transcribed
  from them. Permissively licensed projects (SourceGit — MIT,
  git-credential-manager — MIT, JGit — BSD-3) may be ported with attribution
  in [NOTICE](NOTICE).
- **Security-sensitive areas** (process spawning, parsers, IPC, credentials,
  shell extensions) follow the rules in
  [docs/implementation-plan.md §13](docs/implementation-plan.md) — e.g. argv
  exec only, `--end-of-options` in every git invocation, hard parse limits.
  Vulnerabilities go through [SECURITY.md](SECURITY.md), not the issue
  tracker.

## Code style & quality

- `clang-format` (config in repo) is authoritative — format before pushing.
- CI must be green: 3-OS build + tests, plus the ASan/UBSan job.
- Parser and driver changes need unit tests; anything touching a VCS
  invocation needs a fixture-repo integration test (see `tests/`).
- All user-visible strings go through wxWidgets' `_()` macro — no bare
  literals in UI code.
- Prefer small, reviewable PRs against `main` with an imperative subject
  (≤ 72 chars) and a body explaining *why*; reference issues with `Fixes #N`.
