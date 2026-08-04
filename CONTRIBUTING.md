# Contributing to Repomancer

Thank you for considering a contribution!

## Ground rules

- **License:** all contributions are accepted under [Apache-2.0](LICENSE).
- **DCO:** every commit must carry a `Signed-off-by:` line
  (`git commit -s`), certifying the
  [Developer Certificate of Origin](https://developercertificate.org/).
- **SPDX:** every new source file starts with
  `// SPDX-License-Identifier: Apache-2.0`.
- **No GPL-derived code — ever.** TortoiseGit, TortoiseSVN, TortoiseHg,
  RabbitVCS, git-cola and Mercurial/Git sources are GPL: they may be studied
  for behavior, but code must never be copied or closely transcribed from
  them. Permissively licensed projects (SourceGit — MIT,
  git-credential-manager — MIT, JGit — BSD-3) may be ported with attribution
  in NOTICE.
- **Formatting:** `clang-format` (config in repo) is authoritative. CI runs
  `clang-tidy`; warnings in changed code should be fixed, not suppressed.
- **Tests:** parser and driver changes need unit tests; anything touching a
  VCS invocation needs a fixture-repo integration test.
- **Strings:** all user-visible strings go through wxWidgets' `_()` macro —
  no bare literals in UI code (see docs/implementation-plan.md §4.3).

## Commit style

Imperative subject ≤ 72 chars, body explains *why*. Reference issues with
`Fixes #N` where applicable.
