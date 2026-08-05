# Repomancer — Implementation Plan (C++ / wxWidgets)

**Decision:** C++20 + wxWidgets 3.3.x. Cross-platform (Windows / macOS / Linux) GUI frontend
for locally installed **git / SVN / Mercurial**, TortoiseGit-inspired: full visual flow,
revision graphs, diff/merge, SSH key generation, GitHub/GitLab auth, and Tortoise-style shell
integration (context menus + status overlays) in Explorer / Finder / Linux file managers.

Companion docs:
[stack-analysis.md](stack-analysis.md) (why this stack) ·
[permissive-vcs-implementations.md](permissive-vcs-implementations.md) (VCS implementation landscape).

Each section lists **Options** with a ✅ recommendation. Plan date: 2026-08-04.

---

## 0. Ground rules

- The app **drives installed VCS binaries** — it implements no VCS. Subprocess boundary keeps
  GPL out of the codebase entirely.
- One native **core library** shared by the GUI, the status daemon, and (on Windows) the shell
  DLL. Shell pieces are thin, dumb clients.
- GPL codebases (TortoiseGit, TortoiseHg, RabbitVCS, git-cola) are **learn-only** references —
  never copy code. Permissive references may be mined for approach *and* code where licenses
  allow (SourceGit MIT, git-credential-manager MIT, dolphin-plugins LGPL learn-only too).
- Never scrape human-facing VCS output; machine interfaces only (see §3).

---

## 1. Toolchain & project foundation

| Decision | Options | Recommendation |
|---|---|---|
| C++ standard | C++17 (max compat) · **C++20** · C++23 (partial support) | ✅ C++20 (coroutines optional, concepts, ranges; all three compilers fine) |
| Build system | **CMake** · Meson · Bazel | ✅ CMake ≥ 3.25 + presets; it's what wx, vcpkg, IDEs expect |
| Dependencies | **vcpkg** (manifest mode) · Conan 2 · system pkgs + FetchContent | ✅ vcpkg manifest (wxwidgets port exists); Linux distro builds can use system wx later |
| wxWidgets line | 3.2.x stable (no Win dark mode) · **3.3.x dev line** → 3.4 when released | ✅ 3.3.x pinned (dark mode + runtime switch, 3.3.3+); migrate to 3.4 stable on release |
| Compilers | MSVC 2022 (Win) · AppleClang (macOS) · GCC 12+/Clang (Linux) | ✅ all three in CI from day one |
| Unit tests | **Catch2** (BSL-1.0) · GoogleTest (BSD-3) · doctest (MIT) | ✅ Catch2 |
| Logging | **spdlog** (MIT) · glog · wxLog only | ✅ spdlog (+ wxLog bridge for GUI log window) |
| Formatting/lint | clang-format + clang-tidy in CI | ✅ non-negotiable from first commit |
| Utility libs | {fmt} (MIT), nlohmann/json (MIT), SQLite (PD, caches), xxHash (BSD-2), pugixml (MIT — svn XML, no DTD/XXE by design §13.2), utf8proc (MIT — UTF-8 ingest validation) | ✅ all |
| CI | **GitHub Actions** (win/mac/linux matrix) · GitLab CI | ✅ GH Actions; artifacts = installers per platform |

### Repository layout (monorepo)

```
repomancer/
  cmake/                     # toolchain, presets, sign/notarize helpers
  core/                      # librepomancer-core (static): VCS drivers, models, graph layout,
                             #   auth, keygen, settings, IPC protocol — NO wx dependency
  statusd/                   # status daemon (depends: core)
  gui/                       # wxWidgets app (depends: core)
  shellext/
    windows/                 # COM DLL: IExplorerCommand + overlays (C++, no wx, no CRT surprises)
    windows-msix/            # sparse package manifest + assets
    macos/                   # FinderSync extension (Swift) + XPC/socket client
    linux/
      dolphin/               # KVersionControlPlugin/KOverlayIconPlugin (small Qt shim)
      nautilus/              # nautilus-python extension (Python client to statusd)
      servicemenus/          # zero-code: .desktop service menus, Thunar UCA, Nautilus scripts
  packaging/                 # WiX/Inno, dmg, deb/rpm/AUR, AppImage, flatpak (later)
  tests/                     # unit + fixture-repo integration tests
  tools/                     # fixture-repo generators, release scripts
```

Rule: `core/` never includes wx headers — it must link into statusd and the Windows shell DLL.

---

## 2. Architecture

```
                 ┌────────────────────────────────────┐
                 │        librepomancer-core          │  VCS drivers · status model ·
                 │      (C++20 static library)        │  graph layout · auth · keygen ·
                 └──────┬──────────────────┬──────────┘  settings · IPC protocol
                        │                  │
        ┌───────────────▼──┐        ┌──────▼─────────────┐
        │  repomancer GUI  │◄──────►│ repomancer-statusd │  FS watcher · repo discovery ·
        │   (wxWidgets)    │  IPC   │  (headless daemon) │  status cache · sub-ms answers
        └──────────────────┘        └──────┬─────────────┘
                                           │ named pipe (Win) / UDS (mac+Linux)
     ┌───────────────────┬─────────────────┼──────────────────────┬──────────────────┐
     │ Windows COM DLL   │ FinderSync ext  │ Dolphin plugin (Qt)  │ Nautilus ext (py) │
     │ IExplorerCommand  │ (Swift)         │ KVersionControl /    │ MenuProvider +    │
     │ + overlay handler │                 │ KOverlayIcon         │ InfoProvider      │
     └───────────────────┴─────────────────┴──────────────────────┴──────────────────┘
```

### IPC (GUI/shell ↔ statusd)

| Aspect | Options | Recommendation |
|---|---|---|
| Transport | **named pipe (Win) / Unix domain socket (mac, Linux)** · TCP localhost · D-Bus (Linux) | ✅ pipe/UDS; optionally also a D-Bus facade on Linux (Nautilus/GNOME idiom) |
| Framing | **length-prefixed JSON (nlohmann)** · MessagePack (msgpack-c, BSL) · Cap'n Proto · FlatBuffers | ✅ length-prefixed JSON v1 (debuggable); switch hot status path to MessagePack only if profiling demands |
| Contract | versioned request/response + subscription for status-change push | ✅ design before Tier-1 shell work — shell plugins are clients of this protocol forever |
| Daemon lifecycle | autostart on login · **launch-on-demand + idle exit** · always-on service | ✅ launch-on-demand (GUI or shell piece starts it; exits after idle), TGitCache-style |

---

## 3. VCS integration layer (`core/vcs`)

### 3.1 Abstraction

`IVcsProvider` + per-backend capability flags (IntelliJ model): `supportsStaging`,
`supportsShelve`, `supportsRevisionGraphServerSide`, `hasLocking` (svn), etc. Every UI feature
queries capabilities — no `if (isGit)` in the GUI.

Repo detection: walk up for `.git` (file or dir — worktrees!), `.svn`, `.hg`; multi-VCS nesting
resolved by nearest root; user override per folder.

**Preferences → VCS Providers (settled 2026-08-04):** a first-class settings page, shipped in
M1. Per VCS (git / SVN / Hg): **enable toggle** · **binary path** (auto-detected from PATH,
shown resolved, with browse-override) · detected **version probe** readout · **Test** button
(runs `--version` + a capability probe). Defaults: **git enabled, resolved from PATH**; SVN and
Hg present in the UI but disabled until their backends ship (marked "available in 1.x"), then
default-disabled until the user flips them. All drivers read binary location exclusively from
this config — no hardcoded paths anywhere. Extra per-VCS knobs live here too (git: fsmonitor
opt-in; hg: command-server on/off; svn: default depth).

**Preferences → Status & Overlays (ships with M7, modeled on TortoiseGit's proven page):**
TortoiseGit precedent — Settings → Icon Overlays exposes *Status cache: Default (TGitCache
process) / Shell (compute in-extension, blocking) / None*, plus drive-type checkboxes
(fixed/removable/network/…) and include/exclude path lists. Our equivalent:
- **Status engine** (global + per-root override): **Auto** (default — prefers git fsmonitor
  where supported, else native FS watch, else polling) · git fsmonitor · native watch ·
  polling · off.
- Drive-type policy toggles (network/removable default-off, per §5.1) and include/exclude
  path lists.
- Per-root overrides surface in the repo's context menu too ("Overlays on this repo: …").

### 3.2 git driver

| Aspect | Options | Recommendation |
|---|---|---|
| Primary interface | **CLI plumbing** · libgit2 · gitoxide FFI (no stable C API — out) | ✅ CLI plumbing (what GitHub Desktop/IntelliJ do) |
| Fast-read accelerator | none · **libgit2 (GPLv2 + linking exception — linking is fine, code is not vendorable)** behind `REPOMANCER_WITH_LIBGIT2` | ✅ optional, phase 2; CLI must remain the reference path |
| Status | `git status --porcelain=v2 -z --no-optional-locks` | ✅ |
| Log/graph | `git log --format` with `%x00`/`%x01` separators, batched (`--max-count` + cursor), `--topo-order` for graph | ✅ |
| Object reads | persistent `git cat-file --batch` / `--batch-check` processes per repo | ✅ (blob preview, diff sources) |
| Refs | `git for-each-ref --format` | ✅ |
| Hunk staging | build patches in-app → `git apply --cached [--reverse]` | ✅ (line-level stage/unstage/discard) |
| Env discipline | `-c color.ui=false`, `LC_ALL=C` for parsed cmds, `GIT_TERMINAL_PROMPT=0`, credential/askpass wiring (§7) | ✅ |
| Floor version | git ≥ 2.25 (probe at startup, degrade politely) | ✅ |

### 3.3 Mercurial driver

| Aspect | Options | Recommendation |
|---|---|---|
| Primary interface | **command server** (`hg serve --cmdserver pipe`) · one-shot CLI spawns | ✅ command server — persistent, designed for GUIs (python-hglib/JavaHg precedents; protocol is simple channel-framed I/O). One-shot CLI as fallback. |
| Output parsing | `-T json` templates (hg ≥ 4.x) for log/status | ✅ JSON templates — no scraping |
| Floor version | hg ≥ 5.0 | ✅ |
| Risk note | hg usage declining; keep backend strictly pluggable so effort is capped | — |

### 3.4 Subversion driver

| Aspect | Options | Recommendation |
|---|---|---|
| Primary interface | **CLI with `--xml`** (`log`, `info`, `status`, `blame`, `list`) · link libsvn (Apache-2.0) | ✅ CLI `--xml` primary |
| statusd accelerator | libsvn (`svn_client_status6`) — Apache-2.0, license-clean, faster for overlay-scale status | ✅ optional, phase 2, Linux/Win where libsvn dev pkgs are easy |
| SVN-specific UI | lock/unlock, properties, externals display, repo-browser (server-side `svn list`) | ✅ capability-gated |
| Floor version | svn ≥ 1.10 (1.14 LTS target) | ✅ |

### 3.5 Process runner (shared)

| Options | Recommendation |
|---|---|
| wxExecute/wxProcess · Boost.Process · **reproc++ (MIT)** · custom (CreateProcess/posix_spawn) | ✅ reproc++ wrapped in our own `ProcessRunner` (async, streamed stdout/stderr, cancellation, timeouts, env control, UTF-8 everywhere, hidden console + job objects on Windows). Core must not depend on wx, so wxExecute is out. |

---

## 4. GUI (wxWidgets) — feature → widget map

| Feature | Options | Recommendation |
|---|---|---|
| Shell/layout | wxSplitter trees · **wxAUI docking** | ✅ wxAUI (TortoiseGit-style dockable panes, saved perspectives) |
| Window layout | **GitX-inspired master–detail (user direction 2026-08-04)** | ✅ Two modes switched from the toolbar, as GitX does. **History:** left dock = repository tree (Branches / Remotes / Tags / Stashes / Submodules); centre = commit log with the graph column; bottom dock = commit detail (message, author, hashes, parents) beside the changed-file list, with the diff opening for the selected file. **Stage:** unstaged and staged file lists side by side, diff with per-hunk/per-line staging below, commit message box with the commit button. GitX itself is GPL-2.0-only, so this is layout inspiration only — no code. |
| Commit log + inline graph | wxDataViewCtrl virtual model · **custom-drawn graph column in `wxDataViewVirtualListModel`** · full custom canvas | ✅ virtual list + custom-render graph column (lane layout computed in core); infinite scroll via batched `git log` |
| Revision-graph view (DAG overview, TortoiseGit parity) | custom `wxScrolled` + wxGraphicsContext (Direct2D/CoreGraphics/Cairo) · wxGLCanvas | ✅ wxGraphicsContext canvas first; wxGLCanvas only if >50k-node profiling demands |
| Diff viewer | **2× wxStyledTextCtrl (Scintilla — permissive!)** side-by-side, synced scroll, custom inter-pane gutter · unified single-STC mode | ✅ both modes; intra-line highlights via Myers/diff-match-patch in core; image diff via wxImage panes |
| Hunk/line staging | STC margin markers + selection → patch → `git apply --cached` | ✅ the flagship-feature; budget real time here |
| 3-way merge | own TortoiseGitMerge-equivalent (3 STCs + result pane) · **delegate to user-configured external tool first** (kdiff3/meld/P4Merge/BC), own tool later | ✅ external-tool integration in M2 (configurable, like TortoiseGit); own merge view is M8 |
| Blame | STC + annotation margin, hover commit popup, re-blame at parent | ✅ |
| Working-copy view | wxDataViewCtrl (checkbox staging, ignores toggle, context menus) | ✅ |
| Commit dialog | STC editor (72-col guide, history, amend, sign-off, GPG sign via gpg CLI) | ✅ |
| Repo/branch/remote/tag/stash managers, sync dialog with streamed progress | standard wx dialogs + core streaming | ✅ |
| Theme | **Light / Dark / System selection** (user requirement 2026-08-04), persisted in settings.json; View ▸ Theme now, Preferences page later | ✅ wx ≥ 3.3: `wxApp::SetAppearance()` — one consistent API on MSW/GTK/macOS (runtime switch on MSW needs 3.3.3+, else restart prompt; apply before first window). wx 3.2 dev fallback: live GTK `gtk-application-prefer-dark-theme` toggle (initial value captured for System restore); unsupported on 3.2 MSW/mac (releases pin 3.3). QA per milestone — MSW dark is fresh upstream |
| HiDPI/icons | **SVG via wxBitmapBundle**; icon pack: **Lucide** (ISC, attributed in NOTICE) | ✅ |
| Integrated title bar | **wxbf** (vendored, wxWindows Licence 3.1) borderless frame + app-drawn caption buttons on Windows/Linux; native frame on macOS | ✅ M1 |
| i18n | see §4.3 | ✅ wxTranslations + gettext from M1 (strings externalized day one) |
| Keyboard-first flow | full accelerator map, command palette (later) | ✅ accelerators from M1 |

### 4.1 Diff & merge engine (all-permissive)

| Layer | Options | Recommendation |
|---|---|---|
| Line-level diff | **dtl — Diff Template Library** (header-only C++, BSD-3, Myers O(ND)) · **got `libdiff`** (ISC, C — Game of Trees' diff library, adopted into FreeBSD base `diff(1)`: strong production pedigree; vendor + build glue needed) · port **JGit HistogramDiff** (JGit is EDL/BSD-3 → legally portable to C++) · kristapsdz/libdiff (ISC, small Myers, less proven) · patience diff (implement from the algorithm — algorithms aren't copyrightable) | ✅ **got libdiff is the engine (user decision 2026-08-04)**: vendor a snapshot FreeBSD-style behind an `ILineDiff` interface, own CMake glue. dtl (header-only) may serve as a stopgap while the vendoring lands, and remains the fallback. HistogramDiff port demoted to an optional M2+ quality experiment — only if code-alignment output needs it. At vendoring time verify the algorithm set (Myers primary; FreeBSD notes a fallback algorithm for unsupported cases — confirm patience availability). Guard pathological inputs: max-D cutoff + size limit → "files too different" fallback |
| Intra-line / word-level | **diff-match-patch** (Google, Apache-2.0; C++/STL port) · custom char-Myers via dtl | ✅ diff-match-patch with semantic cleanup |
| Repo-vs-repo diffs | also available "for free" from the binaries (`git diff -U`, `hg diff`, `svn diff`) | ✅ use CLI output where it suffices; in-app engine is required for unsaved buffers, side-by-side alignment, intra-line, hunk staging math, and 3-way merge |
| 3-way merge core | **libsvn_diff** (Apache-2.0): `svn_diff_mem_string_diff3()` + `svn_diff_mem_string_output_merge2()` — in-memory 3-way merge with real conflict markers, ~20 years of production · own diff3 on top of the line-diff layer | ✅ libsvn_diff — don't hand-write diff3 when an Apache-2.0, battle-tested one exists (the SVN backend accelerator may link libsvn anyway). Cost: APR dependency (Apache-2.0, in vcpkg) — if APR proves painful on some platform, fall back to own diff3 |
| **License traps** | git's xdiff/libxdiff = **LGPL-2.1**, GNU diffutils/diff3 = **GPL** | ⚠ never vendor/copy; our engine stays BSD/Apache-only |

### 4.2 History tree rendering

Two distinct features:

1. **Log view with inline graph column** (the workhorse, TortoiseGit "Log Messages"):
   - Widget: `wxDataViewCtrl` in virtual mode (`wxDataViewVirtualListModel`) +
     **`wxDataViewCustomRenderer`** for the graph column; `wxGraphicsContext` inside the
     renderer for anti-aliased lane curves; ref chips (branch/tag/HEAD) drawn in the subject
     cell. Note: MSW DVC is the generic implementation (full drawing control), GTK/macOS are
     native — verify custom-renderer parity in M1. Fallback if renderer limits bite: fully
     owner-drawn `wxScrolled` canvas + `wxHeaderCtrl` (TortoiseGit's approach; more work,
     total control).
   - Data/algorithm: lane assignment (active-lanes model, gitk/SourceGit family) computed
     incrementally in `core/` from `git log --topo-order` parent lists — the lane state is
     streamable, so batched loading (1–2k commits/fetch) extends the graph without relayout.
     **SourceGit (MIT) has a clean graph painter to study and legally port; TortoiseGit's is
     GPL learn-only.** SQLite cache of (hash, parents, refs) for instant reopen.
   - hg: same lane engine fed by `hg log -T json` parents. svn: linear history + copy points
     from `svn log -v --xml` (copyfrom) — TortoiseSVN-style branch graph, not lanes.
2. **Revision Graph overview** (whole-DAG map, M8): hand-rolled layered layout (rank =
   topo generation, heuristic crossing reduction) on a pan/zoom `wxGraphicsContext` canvas.
   ⚠ OGDF is GPL — avoid; graphviz is EPL-1.0 (weak copyleft) — acceptable only as an
   *optional shell-out to `dot`*, never linked. Permissive layered-layout libraries that could
   serve here: `drag` (BSD-3, header-only, dormant), `dyng` (Apache-2.0), Boost Graph /
   LEMON (Boost, algorithms only).

**Surveyed alternatives (2026-08-04) — no library replaces the lane engine.** General graph
layout libraries solve Sugiyama-style layout where *both* axes are free; a commit log fixes y
(one commit per list row, topological order, aligned to the control's rows) and solves only for
the lane. They are also batch algorithms, while we stream 1–2k commits at a time and must never
relayout earlier rows. Rendering engines (Skia BSD-3, Blend2D zlib, NanoVG zlib, ThorVG MIT)
are all permissive but megabytes of dependency for a few lines and a circle in a ~40×26 px cell.

**Portable references (permissive — may be ported with NOTICE attribution):**
- **serie** (MIT, Rust TUI) — renders each graph row as an *image*, confirming the
  offscreen-bitmap approach below. Offers a **Rounded / Angular** edge-style option worth
  copying. Its proportions are close to ours (cell 50 px: line 5, dot radius 10, ring 13 ⇒
  line ≈10 % of cell, dot ≈20 %). Crucially, its lane lines **stop at the circle's outer
  radius** instead of running under the dot — that is the correct way to give a dot a
  contrasting ring, and the reason our halo attempt notched the lanes.
- **git-graph** (MIT, Rust CLI) — **branching-model-aware lane assignment**: branches are
  classified by name (`^(master|main|trunk)$`, `^(develop|dev)$`, `^feature.*$`, …) and given a
  *persistence* rank and a *column order*, plus a `ShortestFirst`/`LongestFirst` policy. This is
  why its git-flow graphs read so much better than purely structural lane assignment. Candidate
  enhancement once refs are tracked per commit (post-1.0).
- **IntelliJ Community `vcs-log`** (Apache-2.0 sources) — the most complete permissive
  implementation: collapsing, filtering, 100k+ commits.
- **SourceGit** (MIT, C#) and **indigane/git-graph-drawing** (Unlicense, public domain).

**Learn-only (GPL — no code may be copied):** Git Extensions (GPL-3.0), GitX (GPL-2.0-only),
gitk, qgit, tig, gitg, git-cola.

**Rendering quality gap to close:** `wxGraphicsContext` is not obtainable from the DataView
cell DC, so curves use `wxDC::DrawSpline` — antialiased via Cairo on GTK, but **aliased on MSW
under GDI**. Fix without a dependency: paint each row into an offscreen `wxBitmap` through
`wxGraphicsContext::Create(wxMemoryDC&)` (that factory *is* supported everywhere) and blit it;
cache per row shape. Do this before Windows QA.

### 4.3 Localization (i18n/l10n)

| Aspect | Options | Recommendation |
|---|---|---|
| Framework | **wxTranslations/wxLocale + gettext catalogs** · ICU MessageFormat · Qt-style .ts (n/a) | ✅ wxTranslations — wx ships its **own .mo loader**, so no GNU libintl (LGPL) dependency on Win/mac; wx also ships `wxstd.mo` translations for stock dialogs/buttons (free coverage) |
| Workflow | `_()"..."` macro discipline from first commit; `xgettext` extraction in CI → `.pot` artifact; per-language `.po` in `locale/`; `.mo` compiled at build | ✅ |
| Quality gates | **pseudo-locale CI build** (expanded/accented strings) catches truncation + concatenation; no string concatenation — `wxString::Format` positional args; plurals via `wxPLURAL`/ngettext | ✅ |
| Translator platform | **Crowdin** · Weblate · raw PRs | ✅ Crowdin (user choice; **native gettext PO/POT support incl. plurals and `msgctxt`**, GitHub sync, variable validation). ⚠ Crowdin is free only for open-source projects — ties into the app-license decision (§11.1). Weblate as self-hosted alternative; PR fallback always |
| Languages | start **en + pl**, community after | ✅ |
| RTL | wx `SetLayoutDirection` is partial | best-effort flag, post-1.0 |
| Shell components | each has its own mechanism: Windows COM DLL → compiled string tables from the same `.po` source; FinderSync → `.strings`; Nautilus/Dolphin shims → own gettext domains | ✅ one Weblate project, one `.pot` per component, single source of truth in `locale/` |
| Dates/numbers | wxLocale conventions; UTF-8 everywhere internally | ✅ |

---

## 5. Shell integration

Phasing (stack-independent logic lives in statusd; see §2 IPC).

### Tier 0 — days, zero/near-zero code (ship with M2)
- **Windows:** registry static verbs with `SubCommands` cascade ("Repomancer ▸ Commit / Log /
  Sync / Settings") → top-level on Win10, "Show more options" on Win11.
- **Linux:** Dolphin **service menus** (.desktop), Thunar custom actions, Nautilus scripts dir,
  Nemo/Caja actions.
- **macOS:** Quick Actions (Shortcuts) invoking `repomancer --action=... --path=...`.

### Tier 1 — proper context menus (M6)
- **Windows — one DLL, both menu systems** (goal: full experience on old Windows *and* the
  Win11 compressed menu; NanaZip/PowerToys precedent):
  - The single dependency-free C++ COM DLL (static CRT) implements **both**
    `IExplorerCommand` (+`EnumCommands` cascade) **and** classic `IContextMenu`.
  - **Win11 modern menu:** `IExplorerCommand` registered via **sparse MSIX** package identity
    → top-level "Repomancer ▸" with icon in the compressed menu.
  - **Win10 (and older surfaces):** classic `IContextMenu` registration → full cascaded menu
    exactly like TortoiseGit; the same classic registration also covers Win11's
    "Show more options" (Shift+F10) and legacy hosts (Open/Save dialogs, third-party file
    managers).
  - Win10 builds < 2004 can't register sparse identity — classic path simply carries them.
    Icon **overlays use the same registry mechanism on all Windows versions** (no Win11
    difference — and the same 15-slot budget).
  - State-aware items ("Resolve" only during conflict) come from statusd with a hard timeout;
    menu must render instantly from cache or degrade to the static verb set.
  - Support floor decision: **Windows 10 1809+** recommended (OpenSSH default, MSIX tooling);
    Win 7/8 are EOL — classic verbs would technically work but are not a target (§11.1).
- **macOS:** FinderSync extension (Swift) — menu items; embedded in signed+notarized app.
- **Linux — coverage by file manager, priority-ordered by install base:**
  1. **Nautilus** (GNOME): `MenuProvider` via nautilus-python (Python client to statusd over
     UDS/D-Bus); emblems via `InfoProvider` in Tier 2. Mind the libnautilus-extension-4 API
     (Nautilus 43+) vs 3.x split — support both.
  2. **Dolphin** (KDE): service menus (Tier 0) → proper plugin (`KVersionControlPlugin` for
     VCS-aware actions+status, `KOverlayIconPlugin` for overlays) — small Qt/KF6 shim.
  3. **Nemo** (Cinnamon) / **Caja** (MATE): near-free ports of the Nautilus extension
     (nemo-python / caja-python use the same model).
  4. **Thunar** (XFCE): custom actions (UCA) — context menus only, no overlay API.
  5. **PCManFM(-Qt)** (LXDE/LXQt): custom-action .desktop support — menus only, best-effort.
  - Reverse direction (app → FM): "Reveal in file manager" via the **`org.freedesktop.FileManager1`**
    D-Bus interface (works across all of the above).
  - Overlay/emblem reality: only Nautilus-family (emblems) and Dolphin (overlay plugin) can
    badge; Thunar/PCManFM users get menus without badges — document, don't fight it.

### Tier 2 — overlays/badges + statusd hardening (M7)
- **statusd:** FS watching — options: efsw (MIT) · watchman · native
  (ReadDirectoryChangesW / FSEvents / inotify). ✅ native per-OS behind one interface (efsw as
  reference impl if schedule slips). Repo discovery cache (SQLite), debounced invalidation,
  `--no-optional-locks` git status, ignore-aware crawling, hard perf budget: answer < 1 ms from
  cache, never compute in-band.

### 5.1 statusd policy (settled 2026-08-04: opt-in watched roots)

- **How repos become watched (registration, never scanning):**
  1. opening a repo in the GUI auto-registers it;
  2. "Add folder to Repomancer" in Preferences (with a *manual* "Scan this folder for repos…"
     bulk-add convenience — explicit user action, never automatic);
  3. shell context menu "Add to Repomancer" on unregistered repos.
  Registry lives in SQLite with per-root settings (overlays on/off, refresh class, network
  override). **Auto-discovery of the home directory is explicitly rejected** — privacy, IO
  storms on first run, and corporate roaming/network home dirs make it a support-ticket
  machine.
- **Three service classes:**
  - *Active* (open in GUI): full FS watch, immediate refresh, push updates to GUI.
  - *Registered background*: FS watch with debounce (~2 s coalescing) — feeds overlays/badges.
  - *Unregistered*: shell queries get an instant "unknown" (no overlay); context menu still
    works via a cheap is-this-a-repo walk-up (stat calls only, no status computation).
- **Network / removable media:** detected via `GetDriveType` / `statfs`; **no watching and no
  overlays by default on UNC/network/removable paths** (menus still work). Per-root override
  for users who insist; polling class (30 s+) as the only mode there — never FS-watch a
  network share.
- **Resource budgets:** default cap ~100 watched roots (soft warning beyond); Linux inotify
  watch exhaustion handled gracefully (degrade that root to polling + one-time hint about
  `fs.inotify.max_user_watches`); idle exit — no GUI and no shell query for ~10 min → daemon
  exits (relaunched on demand by any client); battery-saver detected → background class drops
  to polling.
- **git fast path (settled 2026-08-04: preferred mechanism):** the **Auto** engine prefers
  git's builtin fsmonitor (`core.fsmonitor=true` + `git fsmonitor--daemon`) where supported —
  statusd rides git's own watcher instead of duplicating it. Caveats Auto must handle: builtin
  daemon exists on Windows/macOS since git 2.36; Linux availability depends on git version
  (statusd's native inotify covers Linux regardless); fsmonitor refuses network filesystems and
  bare repos → Auto falls back per §5.1 classes. Enabling fsmonitor writes the *repo's* config —
  do it only with user consent (first-run prompt per repo or the global preference). Mechanism
  is user-selectable in Preferences → Status & Overlays (§3.1). Always `--no-optional-locks`
  so we never contend with the user's own git commands.
- **Staleness contract:** overlays may lag ≤ 5 s (background class); GUI views are event-fresh;
  explicit "Refresh status" everywhere; if statusd is down, shells degrade to static menus and
  no badges — never block.
- **Security:** runs as the user; IPC endpoint per-user with owner-only ACLs (pipe security
  descriptor / socket dir 0700); no elevation anywhere in the system.
- **Windows overlays:** `IShellIconOverlayIdentifier` — options: own handlers (**≤ 4 states**:
  modified / clean / conflicted / ignored) · piggyback **TortoiseOverlays** (shares slots with
  Tortoise family; GPL component — distributing it obliges source-offer for *it*, not for
  Repomancer). ✅ own ≤4 first; TortoiseOverlays interop as user option.
- **macOS:** FinderSync badges (per-folder registration from repo list).
- **Linux:** Nautilus `InfoProvider` emblems; Dolphin `KOverlayIconPlugin` +
  `KVersionControlPlugin` (study in-tree git/svn/hg dolphin-plugins — LGPL, learn-only).

---

## 6. Forge integration (GitHub / GitLab)

| Aspect | Options | Recommendation |
|---|---|---|
| OAuth | **Device flow** (GitHub; GitLab ≥ 17.9 GA) · loopback redirect + PKCE · PAT paste | ✅ device flow primary; PKCE-loopback fallback for older self-hosted GitLab; PAT always available. Self-hosted base-URL config for both. |
| HTTP client | **wxWebRequest** (WinHTTP/NSURLSession/libcurl backends, TLS by OS) · libcurl direct · cpr | ✅ wxWebRequest in GUI; libcurl in core if headless paths need HTTP (statusd doesn't) |
| Secret storage | **own thin keychain wrapper**: CredMan (Win) / Keychain (mac) / libsecret (Linux) · Qt keychain libs (no) | ✅ own ~300-line abstraction; fall back to encrypted file + master password only on keyring-less Linux setups |
| git credential interop | register as **git credential helper** (`repomancer credential fill/approve/reject`) · defer to installed GCM | ✅ both: act as helper when configured; detect + respect existing GCM |
| API features (phased) | clone-from-forge picker; **SSH-key upload** (`POST /user/keys` GitHub scope `write:public_key`; GitLab `api` scope); PR/MR list+checkout (phase 3); CI status badges on commits (phase 3) | ✅ picker + key upload in M3 |
| Client IDs | embedded public OAuth client IDs (fine for device flow); per-instance app registration docs for self-hosted GitLab | ✅ |

#### Credential storage & memory hygiene (`core/secret`)

What lives where:

| Secret | At rest | In memory |
|---|---|---|
| OAuth access/refresh tokens, PATs | **OS keychain** (CredMan / Keychain / Secret Service) — the OS does at-rest crypto; no KDF of ours involved | fetched on demand, held only for the duration of the API call / helper response, then zeroized |
| Device-flow intermediate codes | never persisted | memory only, short-lived |
| SSH key passphrases | **never persisted by us** — ssh-agent holds the decrypted key; our askpass passes the phrase through | transient in askpass, zeroized immediately |
| Fallback store (keyring-less Linux) | encrypted file: master password → **Argon2id** (libsodium `crypto_pwhash`, moderate params, versioned header for future re-tuning) → XChaCha20-Poly1305 secretbox | key derived on unlock, wrapped in SecureBuffer, dropped on lock/idle |

The toolset (all via **libsodium** — already a dependency, zero new deps):

- **`SecureBuffer` / `SecretString`**: allocated with `sodium_malloc` (guard pages, canary),
  `sodium_mlock`ed (never swapped), `sodium_memzero`ed on destruction; move-only, no copies,
  redacted `operator<<`.
- **`KeyringService`** abstraction with four backends: CredMan · macOS Keychain · Secret
  Service · Argon2id-encrypted file (explicit user opt-in, master password).
- **`TokenVault`** on top: per-account records (forge URL, account, scopes, token, expiry),
  refresh-token rotation, revoke-on-sign-out.
- Process discipline: secrets to child processes via **stdin/credential-helper protocol only,
  never argv or env**; no secret ever logged (redaction at the logging layer, enforced by
  type — `SecretString` cannot reach the formatter); clipboard copies of tokens auto-clear
  after ~30 s; crash dumps configured minidump-without-full-memory so vault pages never land
  in a report; statusd holds **zero** credentials by design (it only reads status).

Argon2's precise role: **only** the KDF for the fallback encrypted file store. OS keychains
don't need it (the OS encrypts at rest), and in-memory protection is mlock/zeroize territory,
not a KDF problem.

---

## 7. SSH key generation & management

| Aspect | Options | Recommendation |
|---|---|---|
| Keygen engine | **shell out to `ssh-keygen`** (OpenSSH — bundled on Win10+ "OpenSSH Client" feature, macOS, Linux) · embedded (libsodium Ed25519 + OpenSSH format writer + vendored bcrypt_pbkdf — BSD/ISC from OpenSSH-portable) · bundle PuTTYgen like TortoiseGit (rejected — PuTTY is MIT so it *would* be license-clean, but OpenSSH is the native standard everywhere now) · Botan/OpenSSL | ✅ **ssh-keygen is the engine** (user decision 2026-08-04). Embedded libsodium path serves two roles: the mouse-entropy "key ceremony" UX and the fallback when Windows lacks the OpenSSH Client feature. **No PuTTYgen bundling** — `.ppk` import/export only via a user-installed PuTTY's puttygen if detected (Tortoise-migrant convenience) |
| Entropy ("key ceremony", TortoiseGit/PuTTYgen-style mouse randomness) | OS CSPRNG only · mouse-movement collection UI **mixed with** OS CSPRNG · mouse-only (never) | ✅ ceremony UI for the embedded path: SHA-512 pool absorbs (a) 64 B OS CSPRNG (BCryptGenRandom / getrandom / SecRandomCopyBytes), (b) ~256 mouse events (x, y, high-res timestamp) while a progress bar fills, (c) a second CSPRNG draw at finalize → HKDF-SHA-512 → seed. **Hard rule: OS entropy is always included; user input only ever adds, never replaces.** On modern OSes mouse entropy is trust UX, not a security necessity — ssh-keygen path (pure OS CSPRNG) remains equally valid |
| Agent | detect ssh-agent / Windows OpenSSH agent service (offer to enable) · Pageant detection | ✅ both; `ssh-add` integration |
| PuTTY interop (Tortoise refugees) | none · **detect PuTTY, offer puttygen conversion .ppk ⇄ OpenSSH, Pageant support** | ✅ Windows-only feature, M3 |
| ssh config | write per-host `IdentityFile` blocks to `~/.ssh/config` (with backup + preview) | ✅ |
| known_hosts | strict prompt UI with fingerprint display · accept-new | ✅ prompt UI (show SHA256 fingerprint; GitHub/GitLab published fingerprints pre-seeded) |
| Flow | generate → passphrase → agent-add → **upload to forge via API** → test connection (`ssh -T`) | ✅ the TortoiseGit-parity wizard, M3 |

#### PuTTY-triad → OpenSSH mapping (what replaces what)

| TortoiseGit component | Role | OpenSSH-stack replacement | Glue Repomancer must ship |
|---|---|---|---|
| **PuTTYgen** | generate keys, manage formats | **ssh-keygen** — Ed25519/RSA/ECDSA, passphrase (`-p`), comment (`-c`), fingerprints (`-l`), RFC4716/PKCS8 import-export (`-i`/`-e`) | none (wizard UI only). Gap: `.ppk` read/write — convert via *user-installed* puttygen when detected |
| **Pageant** | agent holding decrypted keys | **ssh-agent / ssh-add** — Windows: "OpenSSH Authentication Agent" service (keys persist across reboots, DPAPI-protected; pipe `\\.\pipe\openssh-ssh-agent`); macOS: launchd agent + Keychain (`ssh-add --apple-use-keychain`, `UseKeychain yes`); Linux: systemd user unit / gnome-keyring | agent-setup UX: detect state, offer to enable the Windows service (admin), `ssh-add` integration in the wizard. Pageant detected only for migrant interop |
| **TortoiseGitPlink** | the SSH transport git spawns, with GUI prompts and no console window | **ssh (OpenSSH client)** pinned via `core.sshCommand`/`GIT_SSH_COMMAND` | the real glue: (1) `repomancer-askpass` helper + `SSH_ASKPASS` with `SSH_ASKPASS_REQUIRE=force` (OpenSSH ≥ 8.4) for GUI passphrase/confirmation prompts; (2) host-key UX — pre-seeded known_hosts (GitHub/GitLab published fingerprints), fingerprint-confirm dialog, `accept-new` option; (3) spawn git with `CREATE_NO_WINDOW` so no console flashes |
| *(TortoisePlink / TortoiseHg's plink)* | same for svn+ssh / hg over ssh | same OpenSSH via `SVN_SSH` env and hg `ui.ssh` | one unified SSH stack for all three VCS — the Tortoise family needed three patched plinks; we need zero |

**Windows availability / detection order:** OpenSSH on PATH (Win10+ feature, default-on) →
**Git for Windows' bundled `usr/bin/ssh*.exe`** (guaranteed present — git is a prerequisite) →
offer enabling the Windows OpenSSH capability → embedded libsodium generator (generation-only
fallback). Bundling portable OpenSSH (BSD licence, would be clean) is therefore unnecessary.

---

## 8. Packaging, updates, diagnostics

| Platform | Options | Recommendation |
|---|---|---|
| Windows | **Inno Setup** (permissive-ish custom license) · NSIS (zlib) · WiX MSI (⚠ **MS-RL weak copyleft**) · MSIX-only | ✅ Inno (NSIS if scripting preferred; avoid WiX for license purity) + **sparse MSIX** for identity (required for Win11 menu); **default per-user install — no elevation anywhere at runtime (§13.3)**, per-machine optional; winget manifest; code signing: **Azure Trusted Signing** · EV cert ✅ Trusted Signing |
| macOS | .app + **dmg**, Developer ID + notarization (FinderSync requires signing anyway); Homebrew cask | ✅ |
| Linux | **deb + rpm + AUR** (required for shell extensions!) · AppImage (portable GUI, no shell ext) · Flatpak (⚠ sandbox: host binaries via `flatpak-spawn --host`; no FM-extension shipping) | ✅ native pkgs primary, AppImage secondary, Flatpak later with documented caveats |
| wx linkage | static wx into the exe (wxWindows licence exception allows) — note GTK itself stays dynamic on Linux | ✅ static on Win/mac; distro-dynamic on Linux packages |
| Auto-update | **WinSparkle (MIT)** / **Sparkle (MIT)** / repos handle Linux | ✅ |
| Crash reports | none · **sentry-native / Crashpad, opt-in** | ✅ opt-in Crashpad, symbol upload in CI |
| Settings | wxConfig native backends · **JSON file in config dir** | ✅ JSON (portable, diffable, syncable) |

---

## 9. Licensing posture

- **Repomancer license — SETTLED 2026-08-04: Apache-2.0.** Ship with: LICENSE, NOTICE, SPDX
  headers (`Apache-2.0`) on every source file, DCO sign-off policy for contributions.
  Consequences: Crowdin free tier ✔, official distro packaging ✔, corporate-friendly patent
  grant ✔; the Tortoise family (GPL) stays **learn-only** — no code copying, ever.
  Evaluated options kept for the record — the stack imposed **no** constraint —
  wx's static-link exception, permissive deps, dynamically-linked LGPL system libs, and
  IPC-separated GPL-side shell shims leave every option open. Candidate matrix:

  | Option | Type | Pros | Cons / notes |
  |---|---|---|---|
  | **MIT** | permissive | simplest, maximum adoption & contribution ease | no patent grant; competitors may take everything, incl. commercially |
  | **Apache-2.0** | permissive | explicit patent grant + contributor terms; corporate-friendly | heavier NOTICE bookkeeping; same "anyone can take it" property |
  | **MPL-2.0** | file-level weak copyleft | modified *files* must stay open, larger work may stay closed — deters silent proprietary improvement of core files while allowing closed plugins/forks around them | less common in desktop apps; some corporate legal friction |
  | **GPL-2.0-or-later / GPL-3.0** | strong copyleft | whole-app protection from proprietary forks; **uniquely: a GPL-compatible license unlocks *copying* from the Tortoise family** (TortoiseGit/TortoiseSVN are GPL-2+; verify exact terms per project — TortoiseHg is GPL-2-only, so GPLv3 could not mix with it) | contributions/CLA story matters; can't later relicense without contributor consent; some companies won't touch GPL desktop tools |
  | **Source-available (FSL-1.1 / BUSL-1.1) or proprietary + free binaries** | commercial | monetization protection; precedent in this exact space: GitButler is FSL, SmartGit/Fork/Tower are proprietary | not OSI open source → **loses Crowdin free tier**, community contributions, distro packaging (no Debian/Fedora/Flathub official repos) |

  Cross-cutting facts: Crowdin free tier requires OSI-open-source; distro repos (deb/rpm/AUR
  official, Flathub) strongly prefer OSI licenses; the Linux FM shims ship GPL-side as separate
  IPC-connected components under **any** main-app license; the optional libgit2 accelerator's
  linking exception permits even proprietary linking.
- Dependency audit (everything in the app binary is permissive):
  wxWidgets (wxWindows licence = LGPL + static-link exception) · Scintilla via wxSTC
  (permissive) · reproc (MIT) · nlohmann/json (MIT) · spdlog/{fmt} (MIT) · Catch2 (BSL) ·
  SQLite (PD) · xxHash (BSD-2) · efsw (MIT) · **got libdiff (ISC, vendored snapshot — primary
  diff engine)** · dtl (BSD-3, fallback) · **diff-match-patch (Apache-2.0)** · JGit
  HistogramDiff port (EDL/BSD-3 source, optional) · **libsodium (ISC)** +
  vendored bcrypt_pbkdf (BSD/ISC) · WinSparkle/Sparkle (MIT) · libcurl (curl license) ·
  wxTranslations' own .mo loader (**no GNU libintl/LGPL needed**) · pugixml (MIT) · utf8proc
  (MIT) · minisign verification (ISC) · libsvn optional (Apache-2.0).
- **Transitive build dependencies are not app dependencies.** On Linux the vcpkg
  `gui` feature builds the whole GTK stack from source — 78 packages — because
  `wxwidgets → gtk3 → libsystemd → libxcrypt`, and **libxcrypt is LGPL-2.1-or-later**
  (verified upstream; the vcpkg port records no license). None of it is Repomancer
  code and none of it changes Repomancer's license: LGPL never reaches the license of
  code that merely links it. Its obligations attach to *distributing binaries*, and
  dynamic linking discharges them — which is exactly the packaging already specified
  in §8 (static wx on Windows/macOS, **distro-dynamic on Linux**, where GTK and
  libcrypt are system libraries). The one path that would create real work is shipping
  a self-contained Linux binary that statically bundles this stack (an AppImage built
  the vcpkg way): LGPL-2.1 §6 would then require shipping relinkable object files.
  Native distro packages being primary on Linux avoids it. Windows and macOS never
  involve GTK at all, so libxcrypt does not appear there.

- **Maximal-permissiveness knobs** (how permissive "as it could be" is achieved):
  1. **Omit libgit2** (GPLv2 + linking exception — linkable but copyleft-adjacent): the CLI
     path is the reference implementation, so the accelerator is strictly optional.
  2. **Omit TortoiseOverlays** (GPL): own ≤4 overlay handlers instead; TortoiseOverlays only
     as an opt-in interop toggle (separate distributed component, source-offer for it only).
  3. **Installer toolchain:** Inno Setup or NSIS (zlib) — **not WiX (MS-RL weak copyleft)**.
  4. **libsecret (LGPL, dynamic)** is fine even for proprietary shipping; for absolute purity
     speak the Secret Service D-Bus API directly via libdbus (dual **AFL-2.1**/GPL — elect
     AFL-2.1, permissive).
  5. **Diff engine:** never vendor git xdiff (LGPL-2.1) or GNU diffutils/diff3 (GPL) — dtl +
     HistogramDiff port + diff-match-patch keep it BSD/Apache-only.
  6. **Linux shell shims live in GPL-flavored ecosystems** (nautilus-python is GPL;
     Dolphin/KF6 is LGPL/Qt): license those two thin plugins (~200–500 LoC each, zero core IP,
     just IPC calls to statusd) GPL-compatibly as separate mini-components. The app, core, and
     statusd remain untouched. This is the standard industry pattern (every vendor's Nautilus
     extension does this).
  7. GTK (Linux) and Qt (Dolphin shim only) are dynamically linked LGPL system libraries —
     no obligations beyond keeping them dynamic.
- GPL learn-only list (no code copying): TortoiseGit, TortoiseGitMerge, TortoiseHg, RabbitVCS,
  git-cola, hg itself. Permissive mine-able: SourceGit (MIT), git-credential-manager (MIT),
  GitHub Desktop/dugite (MIT), isomorphic-git docs (MIT).

---

## 10. Roadmap (milestones, sequential; sizes are relative)

**1.0 scope (settled 2026-08-04): git-first.** 1.0 = M0–M3 + M6–M8 with git only; the
Preferences → VCS Providers page ships in M1 with SVN/Hg visible but marked "available in 1.x".
M4 (SVN) and M5 (Hg) move to 1.1/1.2 — their design stays in this plan so nothing in core
hardcodes git-only assumptions.

| M | Deliverable | Size |
|---|---|---|
| **M0** | Repo, CI matrix, CMake+vcpkg, wx smoke app w/ AUI shell, core lib skeleton, ProcessRunner, fixture-repo test harness | S |
| **M1** | **Git read-only**: open repo, commit log + inline graph (virtual list), commit details, diff viewer (side-by-side + unified), file history, blame, branches/tags panes. **Preferences window incl. VCS Providers page** (enable/path/probe). i18n + accelerators wired. | L |
| **M2** | **Git write**: stage/unstage incl. hunk/line, commit (amend/sign-off/GPG), branch/tag/stash ops, fetch/pull/push with streamed progress, external merge-tool integration, conflict flow. **Repo trust gate + config neutralization (§13.1) — ship-blocker.** Tier-0 shell menus ship. | L |
| **M3** | **Auth + keys**: device-flow OAuth (GitHub/GitLab + self-hosted), keychain storage, credential-helper mode, clone-from-forge picker, SSH wizard (generate → agent → upload → test), PuTTY interop (Win). | M |
| **M4** *(post-1.0 → 1.1)* | **SVN backend**: checkout/update/commit/log/status/blame/revert, lock/unlock, properties, repo browser; revision-graph adaptation (linear+copies). | M–L |
| **M5** *(post-1.0 → 1.2)* | **Hg backend** via command server: clone/pull/push/commit/log graph/status/diff/blame/bookmarks. | M |
| **M6** | **Shell Tier 1**: Windows IExplorerCommand DLL + sparse MSIX; FinderSync menus; Nautilus/Dolphin proper menu plugins. | M |
| **M7** | **statusd + Tier 2 overlays**: FS-watched status cache; Windows overlay handlers (≤4), FinderSync badges, Nautilus emblems, Dolphin overlay/VCS plugin. | L |
| **M8** | **Polish**: own 3-way merge view, revision-graph view (DAG overview), dark-mode QA, perf passes (100k-commit repos), packaging/signing/updates/crash reporting on all platforms, docs. | L |

Testing per milestone: golden-output tests of every driver against fixture repos ×
{git 2.25/latest, svn 1.10/1.14, hg 5/6}; core stays wx-free so logic tests run headless; a
scripted UI smoke (open repo → commit → push to local remote) per platform in CI.

---

## 11. Risks & mitigations

| Risk | Mitigation |
|---|---|
| wx 3.3.x is the dev line (API churn, fresh dark mode) | pin exact version in vcpkg; wrap wx-version ifdefs in one header; move to 3.4 stable when out |
| Windows overlay slot budget (~15 global) | ≤4 own overlays; optional TortoiseOverlays sharing; overlays are enhancement, not core UX |
| FinderSync long-term ambivalence (Apple) | isolate behind statusd protocol; menus also available via Quick Actions; badges degrade gracefully |
| Flatpak sandbox vs host VCS binaries + FM extensions | native packages are primary on Linux; Flatpak documented as GUI-only with `flatpak-spawn --host` |
| Huge-repo performance | plumbing everywhere, batched log, persistent cat-file, statusd cache budgets, profiling gate in M8 |
| hg relevance declining | capability-gated pluggable backend; M5 can slip without blocking anything |
| Solo/small-team scope | strict milestone order; external merge tool before own; revision-graph *view* deferred to M8 (inline log graph ships M1) |
| Scattered per-platform shell APIs | all shell logic lives in statusd + core; plugins stay < ~500 LoC each |
| statusd on network drives / removable media (TortoiseGit's classic support-ticket source) | overlays **off by default** for UNC/network paths; include/exclude path config; never watch what we can't watch cheaply |
| Sparse-MSIX dev loop: package identity requires signing (or Developer Mode) even to test the Win11 menu | set up Azure Trusted Signing + a dev-cert path in M0–M1, not at ship time |
| Win11 modern menu constraints: items must resolve instantly; misbehaving handlers get evicted | menu state strictly cache-fed from statusd with hard timeout; degrade to static verbs |
| libdiff vendoring: no standalone release cadence — snapshot vendoring (as FreeBSD does), C API, own build glue | pin snapshot + upstream-sync script; `ILineDiff` interface isolates it; dtl fallback compiled in tests |
| libsvn_diff drags in APR (pools, init) | isolate behind `IMergeEngine`; own-diff3 fallback documented |
| wxGTK runs on GTK3 (GTK4 port not production); Wayland quirks (window placement, no global hotkeys) | test X11 + Wayland in CI VMs from M1; avoid APIs Wayland forbids |
| Windows per-monitor DPI v2 + fresh wx dark mode | DPI-awareness manifest from M0; mixed-DPI and dark-mode QA in every milestone |
| hg on Windows comes in flavors (TortoiseHg's, pip, standalone) | discovery probes all + explicit path override; command server amortizes Python startup |
| Crowdin is free only for open-source projects | resolved — Apache-2.0 chosen (§11.1) |
| Security surface: malicious repos, updater, IPC, C++ parsers | dedicated plan §13; the M2 trust gate is a ship-blocker for any repo-watching feature; ~10–15% continuous engineering tax budgeted |

### 11.1 Decisions

1. ✅ **App license (settled 2026-08-04): Apache-2.0** — LICENSE + NOTICE + SPDX headers +
   DCO. Tortoise family stays learn-only. Crowdin free tier and distro packaging unlocked.
2. ✅ **Platform floors (settled 2026-08-04):** Windows 10 1809+, macOS 12+, Linux = current
   LTS distros (glibc floor via oldest supported Ubuntu LTS). Win 7/8 explicitly out.
3. ✅ **wx 3.3.x pinned now**; move to 3.4 stable when released.
4. ✅ **1.0 scope (settled 2026-08-04): git-first.** SVN → 1.1, Hg → 1.2. Preferences exposes
   all three VCS from M1 (git default-on from PATH, path-overridable; others "available in 1.x").
5. ✅ **statusd policy (settled 2026-08-04): opt-in watched roots** — full design in §5.1.
6. **Telemetry/crash reporting** — opt-in Crashpad only; no analytics by default.

---

## 12. First concrete steps (M0 checklist)

1. `git init`; **LICENSE = Apache-2.0** + NOTICE + SPDX headers + DCO note in CONTRIBUTING;
   `README`; this plan committed.
2. CMake preset matrix + vcpkg manifest (`wxwidgets`, `reproc`, `nlohmann-json`, `spdlog`,
   `catch2`, `sqlite3`).
3. CI: 3-OS build + test + artifact upload.
4. `core/`: `ProcessRunner` (reproc++) with streamed output + cancellation; `GitDriver::status`
   + `::log` against a generated fixture repo; Catch2 golden tests.
5. `gui/`: wxAUI main frame, repo-open dialog, virtual-list log view fed by `GitDriver::log`,
   dark-mode enabled on Win.
6. Draft the statusd IPC schema (JSON, versioned) — reviewed before any shell code exists.
7. Security baseline (§13): hardening compiler flags in all presets, ASan/UBSan CI jobs,
   pinned vcpkg baseline, branch protection + pinned-by-SHA actions, `SECURITY.md` with
   private-reporting channel.

---

## 13. Security plan

Principle: Repomancer executes *other programs against attacker-influenced data* (any cloned
repo is untrusted input), injects itself into the OS shell, holds forge tokens, and ships an
auto-updater. Each of those is a real attack surface. Tool-license note: the permissive
constraint binds **shipped/linked components only** — dev-time tools (analyzers, fuzzers) never
ship, so even GPL ones would be usable; the list below is permissive throughout anyway.

### 13.1 Threat #1 — malicious repositories (the VCS-GUI-specific class)

Cloning/opening a repo hands the attacker partial control of `.git/config` and the worktree.
Known attack classes against git GUIs/IDEs:

| Attack | Vector | Control |
|---|---|---|
| **Config-driven command execution** — the big one | repo-local `core.fsmonitor`, `core.hooksPath`, `core.sshCommand`, `credential.helper`, `diff.*.command`, `filter.*.clean/smudge/process` execute attacker commands when *any* git command runs in the repo (IDE advisories exist for exactly this) | **Repo trust gate** (VS Code/JetBrains model): first open of an unknown repo → trust prompt. Untrusted repos run read-only operations with dangerous keys neutralized via `git -c` overrides / `GIT_CONFIG_COUNT` env overrides (`core.fsmonitor=false`, `core.hooksPath=`, `core.sshCommand=false`, `credential.helper=`, no external diff/filter). statusd **never** honors exec-capable config regardless of trust. Respect git's own `safe.directory` semantics |
| Argument injection | ref/branch/path named `--upload-pack=…` or starting with `-` | `--end-of-options` and `--` separators in **every** git invocation (driver-layer rule, enforced by a single command-builder + tests); same discipline for svn/hg |
| Hostname-as-option (ssh) | URL like `ssh://-oProxyCommand=…/` (CVE-2017-1000117 class) | URL parse + validate before anything reaches git/ssh; reject leading `-` in host/user; allowlist schemes |
| Symlink escape | repo symlinks pointing outside the worktree | statusd/crawlers never follow symlinks; canonicalize + verify paths stay under the root before any read/write; `O_NOFOLLOW`-style opens where available |
| Display spoofing | RTLO/bidi and control chars in filenames, refs, commit messages (Trojan-Source-style UI confusion); homoglyph ref names | render layer sanitization: strip/visualize C0/C1 controls, wrap untrusted strings in bidi isolates, flag mixed-script refs in security-relevant dialogs (push/pull targets) |
| Resource exhaustion | gigantic lines/files/refs counts | hard limits in every parser (max line, max field, max entries) + streaming; "file too large to diff" fallbacks |

### 13.2 Untrusted-data parsing (C++ tax)

Every parser that touches repo-derived bytes gets: strict limits, fuzz target, sanitizer CI.

- Parsers in scope: porcelain-v2, `log --format` records, `cat-file --batch` framing, svn XML,
  hg JSON, unified-diff/patch, libdiff inputs, IPC schema decoder, `.po` catalogs.
- **XML: pugixml (MIT)** — no DTD/external-entity processing by design → XXE structurally
  impossible (never use a DTD-capable parser for `svn --xml`). JSON: nlohmann strict mode,
  reject on error, depth/size caps. UTF-8: validate at ingest (utf8proc, MIT), replace
  invalid sequences before strings reach UI.
- **Fuzzing: libFuzzer + AFL++ (Apache-2.0)** harnesses per parser, corpora from fixture
  repos; **OSS-Fuzz** onboarding once public (free for OSS). Sanitizers: ASan+UBSan jobs in CI
  from M0; TSan for statusd.
- **Static analysis:** clang-tidy (LLVM, Apache-2.0 w/ exception) with bugprone-*/cert-*
  checks, MSVC `/analyze`, **CodeQL** (free for public repos). Banned-API list enforced by
  tidy config (sprintf/strcpy/system/...).
- **Hardening flags everywhere (incl. vcpkg-built wx):** MSVC `/GS /guard:cf /DYNAMICBASE
  /HIGHENTROPYVA /CETCOMPAT`; GCC/Clang `-fstack-protector-strong -D_FORTIFY_SOURCE=3 -fPIE`,
  full RELRO + now. Ship-blocker CI check that flags are present in artifacts.

### 13.3 Process execution

- Absolute binary paths from Preferences only; **argv-exec, never a shell** (no `system`,
  no `cmd /c`); one audited Windows command-line quoting function (MSVCRT parsing rules —
  classic injection pitfall on Windows).
- DLL-planting defense in all our processes: `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32…)`,
  never load from CWD; delay-load hygiene; child processes get scrubbed env (drop inherited
  `GIT_*`/`SVN_*`/`HG*` except what we set).
- Installer: default **per-user install** (no elevation at all — also simplifies the shell-ext
  story via HKCU registration); per-machine optional. Elevated code paths minimized to that
  optional installer.

### 13.4 IPC (statusd ⇄ GUI/shell plugins)

- Endpoint hygiene: per-user path, owner-only ACL/0700 dir; Windows pipe created with
  `FILE_FLAG_FIRST_PIPE_INSTANCE` (anti-squatting) + explicit security descriptor.
- **Peer authentication:** verify same-UID via `SO_PEERCRED`/`LOCAL_PEERCRED`; on Windows
  `GetNamedPipeClientProcessId` + token/user SID comparison.
- Protocol defense: versioned schema, strict validation, size caps, unknown-field rejection,
  per-client rate limiting; requests carry paths → canonicalize and require them under
  registered roots (no traversal to arbitrary FS queries).
- The explorer.exe-resident COM DLL parses **nothing** from repos — it only consumes validated
  IPC responses, with hard timeouts and static-menu degradation.

### 13.5 Credentials, crypto, network (extends §6)

- Already specced: OS keychains, libsodium SecureBuffer/mlock/zeroize, Argon2id fallback
  store, stdin-only secret passing, minidump-without-memory. Additions:
  constant-time token comparison (`sodium_memcmp`); TLS = OS stacks via wxWebRequest with
  system trust store, forge APIs HTTPS-only (no downgrade path in code); loopback-PKCE
  listener binds 127.0.0.1 ephemeral with exact `state` match; device-flow codes never
  logged/persisted.
- SSH host keys: pre-seed GitHub/GitLab fingerprints, refresh over TLS from their metadata
  endpoints (e.g. GitHub `/meta`); unknown-host = explicit fingerprint dialog (§7).

### 13.6 Update channel & supply chain (highest-impact vector after 13.1)

- **Updates:** HTTPS-only feeds + **signed appcasts/binaries** — Sparkle 2 EdDSA (Ed25519);
  WinSparkle signature support verified at implementation (EdDSA if available, else detached
  **minisign (ISC)** verification step in our updater glue before install); plus OS-level
  Authenticode (Azure Trusted Signing) and macOS notarization; Linux via repo signing
  (deb/rpm GPG — infrastructure, not linked code).
- **Dependencies:** vcpkg baseline pinning; vendored snapshots (libdiff) recorded with
  upstream commit + SHA-256; **SBOM** (CycloneDX via syft, Apache-2.0) published per release;
  **osv-scanner** (Apache-2.0) + Dependabot in CI; **OpenSSF Scorecard** (Apache-2.0) +
  Best Practices badge as public hygiene signal.
- **CI/repo:** branch protection, actions pinned by SHA, least-privilege `GITHUB_TOKEN`,
  DCO; release artifacts signed with **cosign/sigstore (Apache-2.0)** or minisign.
- System-git dependency: our floor check doubles as a security control — warn when the
  detected git/svn/hg is EOL/known-vulnerable (data table shipped with releases).

### 13.7 What we deliberately DON'T have (surface avoided by design)

No webview/embedded browser (native wx UI) · no network servers (only outbound + local IPC) ·
statusd holds no credentials · no elevation at runtime · commit text rendered as plain text
only (never markup) · external links open via default browser after scheme allowlist
(`http(s)` only, confirmation otherwise).

### 13.8 Process & effort

- **SECURITY.md** + GitHub private vulnerability reporting from M0; lightweight STRIDE pass
  over the §2 architecture diagram at M2 and again at M6 (before shell code ships);
  third-party review/pen-test of trust gate + IPC + updater before 1.0.
- Effort model: a continuous **~10–15% engineering tax** (limits, fuzz targets, reviews as
  code lands) plus three focused chunks: **repo trust gate** (M2, size M — the centerpiece),
  **IPC authentication** (M6/M7, size S–M), **signed update pipeline** (M8, size M).

| Milestone | Security deliverables |
|---|---|
| M0 | flags, sanitizers, SECURITY.md, pinned CI, per-user installer decision |
| M1 | parser limits + first fuzz targets (porcelain, log records); display sanitization |
| M2 | **repo trust gate + config neutralization**; `--end-of-options` discipline; URL validation; STRIDE pass #1 |
| M3 | TLS/OAuth hardening items above; host-key seeding |
| M6–M7 | IPC peer auth + anti-squatting; COM DLL minimalism review; STRIDE pass #2; symlink policy in statusd |
| M8 | signed updates end-to-end; SBOM + scanners + Scorecard; OSS-Fuzz onboarding; external review |

Permissive security-component summary (shipped): libsodium (ISC) · pugixml (MIT) · utf8proc
(MIT) · minisign verify (ISC) · everything else is OS APIs. Dev-time (any license OK, chosen
permissive anyway): LLVM/clang-tidy, libFuzzer, AFL++, osv-scanner, syft, Scorecard, cosign —
all Apache-2.0; CodeQL free for public repos.
