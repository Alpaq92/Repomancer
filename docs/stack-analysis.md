# Repomancer — GUI Stack Analysis

Goal: a cross-platform GUI frontend for **locally installed** git / SVN / Mercurial, deeply
inspired by TortoiseGit — revision graphs, full visual flow, SSH key generation, GitHub/GitLab
auth — **plus Tortoise-style integration with Explorer / Finder / Linux file managers**
(context menus and status icon overlays).

Companion doc: [permissive-vcs-implementations.md](permissive-vcs-implementations.md)
(the from-scratch VCS implementation landscape; relevant here for optional fast-read layers).

Analysis date: 2026-08-04.

---

## 1. What this app is, architecturally

Since the VCS binaries are already installed, Repomancer is an **orchestrator + visualizer**,
not a VCS implementation. Licensing consequence: executing `git`/`hg`/`svn` as subprocesses is
arm's-length process separation — GPL does not propagate to the app. Each backend has a
designed-for-machines integration surface:

| VCS | Integration surface | Notes |
|---|---|---|
| git | plumbing: `status --porcelain=v2 -z`, `log --format=<custom>` (NUL separators), `for-each-ref`, `cat-file --batch` | What GitHub Desktop (dugite) and IntelliJ git4idea do. Never scrape human porcelain. |
| hg | **command server** (persistent process, versioned protocol) | Created by Mercurial devs *specifically* so non-GPL clients can exist. MIT precedents: python-hglib, JavaHg. Protocol is simple to implement in any language. |
| svn | CLI `--xml` output: `log`, `info`, `status`, `blame` | Stable, schema'd machine interface. |

Optional hybrid: use a permissive native library for hot **read** paths (instant graph/log)
while mutations go through the CLI — gitoxide (Rust), JGit (JVM), go-git (Go) slot in here.

### The four hard UI problems that actually pick the stack

1. **Commit / revision graph** — virtualized custom-drawn canvas, must handle 100k+ commits.
2. **Diff/merge viewer** — the biggest stack differentiator. Web-based UIs get Monaco/CodeMirror
   (side-by-side diff, syntax highlight, inline edit) ~free; native toolkits build or bolt on.
   A hunk-level staging editor is a serious widget.
3. **Forge auth** — OAuth **device flow**: GitHub supports it; GitLab shipped it in 17.2, GA
   since 17.9 (older self-hosted GitLab → loopback PKCE or PAT fallback). Store secrets in the
   OS keychain (Keychain / Credential Manager / libsecret). Interop with git credential helpers
   so CLI and GUI share credentials (git-credential-manager, MIT/.NET, is a reference codebase).
4. **SSH key generation** — Ed25519 default + RSA fallback in OpenSSH format, then **upload via
   forge API** after OAuth (generate → install → works: the TortoiseGit-parity flow). On
   Windows, PuTTY/Pageant/.ppk interop wins over Tortoise refugees.

### Precedents

| Tool | Stack | Notes |
|---|---|---|
| GitHub Desktop | Electron + dugite (git CLI) | MIT; canonical CLI-driver architecture |
| GitKraken | Electron | commercial |
| GitButler | Tauri + Svelte + gitoxide/git2 | the Tauri existence proof |
| **SourceGit** | **C# + Avalonia**, MIT | cross-platform git GUI — Avalonia existence proof |
| TortoiseHg | Python + PyQt5 + hg command server | GPL; ships its own Windows shell extension (C++) |
| Gittyup, Guitar | Qt/C++ | |
| SmartGit | JVM/Swing | commercial; git + svn |
| Sublime Merge, Fork | native C++/Cocoa/WPF | |
| RabbitVCS | Python + GTK | GPL; the Linux Tortoise — Nautilus/Nemo/Caja/Thunar |
| GitFinder | native macOS + **FinderSync** | proof FinderSync works for a VCS GUI |

---

## 2. Shell integration deep dive (Explorer / Finder / Linux FMs)

**The key fact: no GUI stack covers this.** Shell extensions are native plugin binaries loaded
*into the file manager's process* (or extension host), with hard constraints on size, startup
and dependencies. Whatever the GUI stack, this subsystem is per-OS native code. Also note:
**no cross-platform VCS GUI ships full Tortoise-style integration on all three platforms today**
— GitKraken/GitHub Desktop/SourceGit all skip it. This is Repomancer's differentiator, and it is
genuinely the long pole.

### 2.1 Windows

- **Context menus.** Windows 11's new context menu only shows entries from
  **`IExplorerCommand` handlers registered by an app with package identity** — for a classically
  installed app that means registering a **sparse MSIX package** pointing at your install
  directory. Classic registry verbs / `IContextMenu` handlers still work but are relegated to
  "Show more options" (Shift+F10). ([MS Learn: sparse packages / legacy context menus](https://learn.microsoft.com/en-us/windows/msix/packaging-tool/support-legacy-context-menus),
  [overview of the two systems](https://shellex.info/guide/shell-extensions-vs-windows-11-menu))
  - **MVP path (no DLL):** registry static verbs with `SubCommands` cascades — a
    "Repomancer ▸ Commit / Log / Sync…" submenu launching the exe with args. Lands in
    "Show more options" on Win11, top-level on Win10.
  - **Full path:** in-proc COM DLL implementing `IExplorerCommand` (+ `EnumCommands` for the
    cascade), packaged identity via sparse MSIX. The DLL loads into explorer.exe → must be tiny,
    dependency-free, instant: **C++ or Rust (windows-rs implements COM interfaces well)**.
    Not a managed runtime (see .NET note below).
- **Icon overlays.** Still the legacy `IShellIconOverlayIdentifier` API (no modern replacement;
  the cloud-files badge API is only for sync engines). Registry-registered, in-proc, and subject
  to the infamous **global ~15-slot budget** that OneDrive/Dropbox/Tortoise* fight over with
  leading-space name tricks. Strategy: register **few** overlays (e.g. modified / clean /
  conflict, not nine), or piggyback the shared **TortoiseOverlays** component the Tortoise
  family uses to split slots (GPL, but it's a separate distributed binary — doesn't infect the
  app; source-offer obligation only for it).
- **Status cache daemon.** Overlays must answer in microseconds — never run VCS commands from
  the shell thread. TortoiseGit ships **TGitCache**; TortoiseHg ships a C++ shellext talking to
  a background process. Same pattern applies here (see §2.4).
- **.NET note:** with NativeAOT, C# COM shell DLLs without CLR injection are now feasible but
  frontier practice; PowerToys keeps most shell pieces in C++. If the main app is .NET, plan a
  small C++/Rust shim for the shell DLL anyway.

### 2.2 macOS

- **FinderSync** is the one API that gives **badges + context menu items + toolbar items** for
  local folders. Written in Swift/ObjC as an app extension embedded in the signed & notarized
  .app bundle; talks to the main app via XPC/socket. Precedent: **GitFinder** is literally a git
  client built around FinderSync.
- **Status/risk:** FinderSync still works. macOS 15.0 Sequoia removed its management UI from
  System Settings (deprecation scare), **15.2 restored it** ([mjtsai summary](https://mjtsai.com/blog/2024/10/03/finder-sync-extensions-removed-from-system-settings-in-sequoia/),
  [Apple dev forums thread](https://forums.developer.apple.com/forums/thread/756711?page=2)).
  Apple's official replacement — File Provider / FileProviderUI — targets **cloud-sync** apps
  and cannot represent "decorate an arbitrary local folder," which is our case. Plan on
  FinderSync, isolate it behind the daemon interface, and accept Apple-ambivalence risk.
- **Ultra-MVP:** Services menu / Quick Actions (Shortcuts/Automator) give context-menu verbs
  with zero extension code (no badges).

### 2.3 Linux

No single standard — per-file-manager plugins, but two pleasant surprises:

- **KDE Dolphin — the best story.** `KVersionControlPlugin` is a **purpose-built VCS
  integration API** (in-tree `dolphin-plugins` already ship Git/SVN/Hg integrations — study
  them); `KOverlayIconPlugin` covers generic overlays (used by Insync/ownCloud —
  [API](https://api.kde.org/koverlayiconplugin.html), [Insync example](https://github.com/insynchq/dolphin-insync-plugin)).
  Both are C++/Qt. Zero-code context actions: **service menus** (`.desktop` files).
- **GNOME Nautilus.** `libnautilus-extension` (C) or **nautilus-python**:
  `NautilusMenuProvider` for menus, `NautilusInfoProvider` for **emblems** (overlay equivalent).
  Precedent: [git-nautilus-icons](https://github.com/chrisjbillington/git-nautilus-icons).
  Mind the GTK4 / libnautilus-extension-4 API break (Nautilus 43+). Zero-code MVP: the Nautilus
  **scripts** folder (`~/.local/share/nautilus/scripts`).
- **Nemo / Caja** (Cinnamon/MATE): forks sharing the Nautilus extension model — cheap ports
  (RabbitVCS covers them). **Thunar** (XFCE): custom actions = config-only context menus, no
  overlay API.
- Hack worth knowing: GIO `metadata::emblems` can badge files without any plugin
  (`gio set`) in the Nautilus family — but it pollutes user metadata; prefer InfoProvider.

### 2.4 The required architecture (the Tortoise pattern)

```
                    ┌────────────────────────────┐
                    │        repomancer-core     │  VCS drivers (git plumbing /
                    │   (native shared library)  │  hg cmdserver / svn --xml),
                    └─────┬────────────────┬─────┘  status model, config
                          │                │
             ┌────────────▼───┐      ┌─────▼──────────┐
             │ repomancer GUI │      │ repomancer-statusd │  daemon: FS watcher,
             │  (any stack)   │◄────►│  (native, autostart) │  repo discovery, status
             └────────────────┘ IPC  └─────┬──────────┘  cache, sub-ms answers
                                           │ named pipe / UDS / XPC / DBus
        ┌──────────────────┬───────────────┼────────────────────┬─────────────┐
        │ Windows COM DLL  │ FinderSync ext│ Dolphin C++ plugin │ Nautilus ext │
        │ IExplorerCommand │ (Swift)       │ KVersionControl /  │ (C or Python)│
        │ + overlay handler│               │ KOverlayIcon       │ + Nemo/Caja  │
        └──────────────────┴───────────────┴────────────────────┴─────────────┘
```

Rules: shell plugins are dumb, dependency-free, query the daemon with a timeout, render cached
state, and never block the shell; menu verbs launch the GUI with `(path, action)`; if the
daemon is down, degrade to no overlays and static menus.

**Consequence for stack choice:** the dominant criterion becomes *"can the core VCS/status
logic live in a native shared library reused by the GUI backend, the daemon, and (on Windows)
the COM DLL?"* Languages that compile to small self-contained native libs (Rust, C++) win;
runtime-hosted languages (JS, Python, JVM, Dart) can power the GUI and even the daemon, but
the shell-side pieces are foreign code for them regardless.

---

## 3. The 10 stacks (with shell-integration fit)

| # | Stack | Diff viewer | Footprint | Shell-int fit | Precedent |
|---|---|---|---|---|---|
| 1 | **Tauri 2 + Rust + Svelte/React** | Monaco (free) | ~10–20 MB | **A** — Rust core shared by daemon + windows-rs COM DLL | GitButler |
| 2 | Electron + TypeScript/React | Monaco (free) | 150–250 MB | **C+** — entire shell subsystem is foreign C++/Rust/Swift | GitHub Desktop, GitKraken |
| 3 | **Qt 6 Widgets + C++** | build/bolt-on | 30–50 MB | **A** — same language as Dolphin plugins *and* Windows COM | Gittyup, Guitar, TortoiseGit lineage |
| 4 | PySide6 + Python | build/bolt-on | 60–120 MB | **B+** — TortoiseHg proves the shape (C++ shellext + Python daemon); nautilus-python native | TortoiseHg, git-cola |
| 5 | Avalonia 11 + C#/.NET | AvaloniaEdit | 40–80 MB | **B** — .NET daemon fine; Win DLL via NativeAOT (frontier) or C++ shim | SourceGit |
| 6 | Compose Multiplatform + Kotlin/JVM | custom | 80–150 MB | **C+** — JVM daemon heavy; all shims native | IntelliJ git4idea, SmartGit |
| 7 | Wails + Go + web UI | Monaco (free) | 15–30 MB | **B-** — Go daemon great; COM-in-Go painful → shim | — |
| 8 | Flutter Desktop + Dart | **build it** | 30–60 MB | **C+** | — |
| 9 | egui / Iced pure Rust | **build it** | 10–20 MB | **A-** — same Rust core benefit | rerun.io (data-dense egui) |
| 10 | Slint + Rust | **build it** | ~15 MB | **A-** | — |

### Stack details

**1. Tauri 2 + Rust core + TypeScript frontend (Svelte/React) — top pick.**
Rust backend spawns/streams CLI drivers (tokio); **gitoxide** for instant log/graph/diff reads.
Frontend: Monaco diffs + WebGL/canvas commit graph. `ssh-key` crate (keygen), `keyring`
(secrets), device-flow OAuth in Rust. **Shell integration:** `repomancer-core` and `statusd`
are Rust crates; the Windows COM DLL (IExplorerCommand + overlay handler) is windows-rs against
the same core; only the Swift FinderSync ext and thin Linux FM shims are foreign. Risks: two
languages; WebKitGTK quirks on Linux; keep IPC streaming on huge repos.

**2. Electron + TypeScript.** Fastest GUI ship, largest ecosystem, pixel-identical Chromium.
dugite for git; safeStorage; electron-builder/updater. Risks: 200 MB / RAM; and now the whole
shell subsystem is a bolted-on native project in different languages — the reason GitHub
Desktop never shipped Explorer integration.

**3. Qt 6 Widgets + C++20 — co-favorite once shell integration is a requirement.**
The most TortoiseGit-native feel (dense widgets, dockable panes, model/view virtualization for
million-row logs). Qt 6.9+ has a device-flow OAuth class; QtKeychain for secrets. **Unique
bonus:** Dolphin's KVersionControlPlugin/KOverlayIconPlugin are C++/Qt — one language across
GUI, daemon, Windows COM DLL, and KDE plugins. Risks: slowest iteration; diff viewer DIY
(QScintilla is GPL/commercial — avoid; KSyntaxHighlighting or custom); LGPL discipline.

**4. PySide6 + Python.** TortoiseHg modernized (PySide6 is LGPL; PyQt is GPL/commercial —
don't mix up). `hglib` (MIT) exists; Dulwich (Apache branch) for git reads; fastest native-UI
iteration. TortoiseHg literally proves the polyglot shell shape: C++ Windows shellext + Python
status daemon + nautilus-python. Risks: packaging (PyInstaller/Nuitka) friction; perf ceilings
→ push heavy lifting to plumbing; GPL precedents are learn-only, not copy.

**5. Avalonia 11 + C#/.NET.** SourceGit (MIT) proves the GUI; git-credential-manager (.NET)
is a ready auth/keychain reference; SshNet.Keygen; Skia canvas for the DAG; AvaloniaEdit diffs.
Shell side: .NET daemon is fine; the explorer-loaded DLL should be NativeAOT (frontier) or a
C++ shim; Linux FM shims C/C++. Solid, but now inherently polyglot.

**6. Compose Multiplatform (Kotlin/JVM).** Superpower: **JGit** (BSD-3 — push, merge, rebase)
in-process, zero subprocess for git; IntelliJ's Apache-2.0 VCS code as reference; JavaHg
precedent for hg; avoid SVNKit (TMate license) — drive svn CLI. Risks: JVM footprint/startup,
no Monaco-class diff widget, shell shims all native.

**7. Wails (Go) + web UI.** Tauri's shape in Go: go-git for reads, `x/oauth2` device flow,
go-keyring, `x/crypto/ssh` keygen; v2 stable, **v3 just reached beta** — plan v2 or accept
churn. Go daemon is excellent; COM in Go is painful → C++/Rust shim on Windows. Same WebKitGTK
caveat.

**8. Flutter Desktop.** Best canvas for a beautiful custom graph (Skia/Impeller), future mobile
companion for free, flutter_secure_storage. Fatal-ish risk: the diff/merge editor is a
from-scratch build — desktop text-editing ecosystem is thin. Shell shims all foreign.

**9. egui / Iced (pure Rust).** One ~15 MB static binary, no webview; immediate-mode rendering
eats 100k-node DAGs; gitoxide in-process; same Rust shell-integration benefits as Tauri. Risks:
hand-build every rich widget (diff editor, find, IME/a11y via AccessKit); never native-looking.

**10. Slint + Rust.** Declarative UI, live preview, embedded-grade footprint; license trio
(GPLv3 **or** royalty-free **or** commercial — royalty-free allows proprietary desktop). Same
Rust core benefits. Youngest widget ecosystem; text-heavy views DIY.

**Disqualified / honorable mentions:** .NET MAUI (no Linux desktop). GTK4/libadwaita (superb on
Linux — RabbitVCS lineage — alien elsewhere). JavaFX/SWT (viable; #6 covers the JVM better —
SWT is EGit's home).

### 3.1 Also evaluated on request (2026-08-04)

**C++ + wxWidgets — upgraded from footnote to credible dark horse (B+/A-, just below Qt).**
- Truly native widgets per platform (Win32 / Cocoa / GTK) — closest to the Tortoise "feels like
  the OS" ideal. Alive and addressing its weak spots: 3.3.0 (June 2025) finally brought
  **Windows dark mode**, 3.3.3 (July 2026) added runtime light/dark switching; 3.4.0 will be
  the first *stable* line with it — so today you ride the 3.3.x development line.
- **Two genuine advantages over Qt:**
  1. **License:** the wxWindows licence is LGPL **with a static-linking exception** —
     effectively permissive for proprietary distribution, no LGPLv3 dynamic-link discipline,
     no commercial-license question.
  2. **Diff viewer:** `wxStyledTextCtrl` wraps **Scintilla** under a permissive license —
     unlike Qt's QScintilla (GPL/commercial). A serious code/diff editor widget out of the box
     (side-by-side diff = two STCs + custom gutter, a well-trodden path).
- Shell integration: same A-tier as Qt — one C++ core across GUI, statusd, and the Windows COM
  DLL. (Dolphin plugin shims are Qt-based regardless of app framework; they're thin statusd
  clients, so it barely matters.)
- Precedents for dense cross-platform desktop tools: FileZilla, Audacity, KiCad, Code::Blocks.
- Weaknesses vs Qt: weaker tooling (no designer/declarative layer of Qt's caliber), dated API
  ergonomics, `wxDataViewCtrl` virtualization less polished than Qt's model/view for
  million-row logs (workable), three native backends = triple rendering-quirk testing, smaller
  talent pool, HiDPI/dark-mode polish still newer than Qt's.
- **Verdict:** if C++ is the direction and Qt's LGPLv3/commercial posture or QScintilla's GPL
  bothers you, wxWidgets is the answer; otherwise Qt keeps the edge on tooling and polish.

**Swift + Shaft — not viable as the foundation today (C- for this product).**
- [Shaft](https://github.com/ShaftUI/Shaft) is a BSD-3-Clause cross-platform GUI framework —
  essentially **a port of Flutter's framework architecture to Swift** (own Skia-style
  rendering, own widget tree, hot reload, multi-window). Genuinely interesting engineering.
- But the numbers say experiment, not foundation: ~706 stars, ~160 commits, 5 releases, ~1
  maintainer; issue/PR activity has months-long gaps. For a TortoiseGit-class app it inherits
  **all of Flutter-desktop's weaknesses** (diff/merge editor from scratch, text-editing/IME/
  accessibility maturity) **without Flutter's thousands of engineer-years** on exactly those
  stacks. Bus factor ≈ 1 on your most foundational dependency.
- Swift itself cross-platform: the toolchain works on Windows/Linux (Swift 6.x), but GUI
  ecosystem, distribution (runtime libs), and debugging outside macOS remain rough; a COM shell
  DLL in Swift is exotic — you'd write C++/Rust shims anyway.
- **Swift's real role in Repomancer is guaranteed regardless of stack: the FinderSync
  extension.** If you love Swift, spend it there, not on the app shell.

**Better Swift alternatives to Shaft (evaluated 2026-08-04):**
1. **SwiftCrossUI** (moreSwift org, ex-stackotter) — the better *framework* bet: SwiftUI-like
   API, 4 years of development, 831 commits, 9 releases (v0.7.0 current), community org with
   Open Collective funding, and — crucially — **native backends** instead of Shaft's one custom
   renderer: GTK4 (primary), WinUI-based Windows backend (via swift-winrt), experimental
   AppKit, experimental Qt5. Still v0.x with 174 open issues; widget depth nowhere near a
   diff-viewer/virtualized-log app. Grade for Repomancer: **C+** (vs Shaft's C-).
2. **The "Arc pattern" — shared Swift core + per-platform native UI** — the better *shipping*
   bet and the only Swift route with production evidence at scale: The Browser Company built
   Arc for Windows in Swift using their open-sourced
   [swift-winrt](https://github.com/thebrowsercompany/swift-winrt) projection over **WinUI 3**;
   macOS UI in SwiftUI/AppKit; Linux via Adwaita-for-Swift/SwiftGtk. Costs three UI
   implementations, but each is genuinely native, and it composes perfectly with the
   shell-integration architecture (Swift statusd; COM DLL still a C++/Rust shim). Grade: **B-**
   on feasibility, highest UI effort of any option in this document.
3. Not viable for this app: Adwaita-for-Swift alone (Linux-only), Tokamak (SwiftUI-for-Wasm,
   web-target), SwiftWin32 (experiment), Skip (mobile Swift→Kotlin), Qlift (dormant Qt
   bindings).

---

## 4. Recommendation

With shell integration as a first-class requirement:

1. **Tauri + Rust (#1)** — strongest overall: one Rust core powers the GUI backend, the status
   daemon, and the Windows COM DLL; web frontend solves the diff viewer; continuity with the
   gitoxide research. Foreign code is limited to the Swift FinderSync extension and thin Linux
   FM shims (unavoidable in every stack).
2. **Qt C++ (#3)** — co-favorite if TortoiseGit-grade native density is the product's soul and
   slower shipping is acceptable; uniquely, one language spans GUI + daemon + Windows COM +
   KDE plugins.
3. **PySide6 (#4)** — the pragmatic prototyping path with the TortoiseHg precedent; accept
   polyglot shell pieces from day one.
4. **Avalonia (#5)** — still very good (SourceGit + GCM references), now minus points for the
   NativeAOT-or-shim shell story.
5. **Electron (#2)** drops: the shell requirement removes its "one stack, ship fast" advantage.

### Phased shell-integration rollout (stack-independent)

- **Tier 0 — days, zero/near-zero code:** Windows registry `SubCommands` cascade (lands in
  "Show more options" on Win11, top-level on Win10); Nautilus scripts folder; Dolphin service
  menus; Thunar custom actions; macOS Quick Actions.
- **Tier 1 — weeks:** proper menus — Windows `IExplorerCommand` DLL + sparse MSIX (top-level
  Win11 menu); FinderSync menu items; Nautilus/Nemo/Caja MenuProvider extensions; Dolphin
  proper plugin.
- **Tier 2 — the long pole:** `statusd` daemon (FS watching, repo discovery, status cache) +
  icon overlays/badges/emblems everywhere: `IShellIconOverlayIdentifier` (≤4 overlays or
  TortoiseOverlays slot-sharing), FinderSync badges, `NautilusInfoProvider` emblems,
  `KOverlayIconPlugin`/`KVersionControlPlugin`.

### Stack-independent decisions to lock early

- `VcsProvider` abstraction with per-backend capability flags (IntelliJ's model).
- hg via command server; svn via `--xml`; git via plumbing with `-z` — never scrape human output.
- Device-flow OAuth (GitHub; GitLab ≥17.9) + loopback-PKCE/PAT fallback; OS keychain storage;
  git-credential-helper interop.
- Keygen: Ed25519 default, upload via forge API post-OAuth; Pageant/.ppk interop on Windows.
- Design `statusd`'s IPC protocol before Tier 1 — the shell plugins are its clients forever.

---

## Sources

- Windows: [MS Learn — support legacy context menus / sparse packages](https://learn.microsoft.com/en-us/windows/msix/packaging-tool/support-legacy-context-menus) ·
  [shellex.info — Win11 menu vs legacy shell extensions](https://shellex.info/guide/shell-extensions-vs-windows-11-menu) ·
  [shellex.info — why extensions don't show on Win11](https://shellex.info/dev/shell-extension-not-showing-windows-11) ·
  [xplorer² — packaged DLL for Win11 menu](https://www.zabkat.com/blog/win11-explorer-menu-package.htm)
- macOS: [mjtsai — FinderSync removed from System Settings in Sequoia (and restored in 15.2)](https://mjtsai.com/blog/2024/10/03/finder-sync-extensions-removed-from-system-settings-in-sequoia/) ·
  [Apple Developer Forums — FinderSync in macOS 15](https://forums.developer.apple.com/forums/thread/756711?page=2) ·
  [GitFinder — FinderSync-based git client](https://gitfinder.com/blog/sync-my-finder/39)
- Linux: [KDE KOverlayIconPlugin API](https://api.kde.org/koverlayiconplugin.html) ·
  [Insync Dolphin plugin (KVersionControlPlugin)](https://github.com/insynchq/dolphin-insync-plugin) ·
  [git-nautilus-icons](https://github.com/chrisjbillington/git-nautilus-icons)
- Frameworks/precedents: [SourceGit (C#/Avalonia, MIT)](https://github.com/sourcegit-scm/sourcegit) ·
  [GitLab OAuth2 device grant docs](https://gitlab.com/gitlab-org/gitlab/-/blob/master/doc/api/oauth2.md) ·
  [Wails releases](https://github.com/wailsapp/wails/releases) · [Wails v3 status](https://v3.wails.io/whats-new/)
