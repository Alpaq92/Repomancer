# Permissive From-Scratch VCS Client Implementations — Deep Dive

Research into permissively-licensed (MIT / Apache-2.0 / BSD / ISC — **not** GPL) from-scratch
implementations of **Git**, **Subversion**, and **Mercurial** clients, in the spirit of
[gitoxide](https://github.com/GitoxideLabs/gitoxide) and [svn-rs](https://github.com/lvillis/svn-rs).

Scope rules: native reimplementations of the on-disk formats and wire protocols count;
wrappers that shell out to the official binaries and bindings to GPL cores do not
(they are listed only where relevant, clearly marked). Library vs. standalone CLI: both accepted.

**Research date:** 2026-08-03/04.
**Method:** two rounds of multi-agent research (115 agents total). Round 1: 5-angle web sweep →
21 sources fetched → 105 claims extracted → 25 top claims put through 3-vote adversarial
verification (21 confirmed, 4 refuted). Round 2: 8 targeted verifiers re-checking every
SVN/Hg/licensing claim against primary sources (LICENSE files, registry metadata, live repos —
svn-rs was cloned and inspected), plus 3 landscape sweeps for missed implementations and a
completeness critic. Every license below was verified from the actual LICENSE file or package
registry metadata unless explicitly noted.

---

## TL;DR

| VCS | Best permissive options | State of the field |
|---|---|---|
| **Git** | gitoxide (Rust), go-git (Go), JGit (Java), Dulwich (Python), isomorphic-git (JS), Game of Trees (C) | Rich, healthy, multiple production-grade choices |
| **SVN** | svn-rs (Rust, MIT) — the **only** permissive from-scratch client in any language; libsvn bindings are license-clean (Apache-2.0 core) | One young native client; the reference impl is already permissive |
| **Mercurial** | **None.** Verified negative — no permissive from-scratch write-capable or wire-protocol implementation exists in any language | GPL provenance contaminates the entire ecosystem; only a read-only BSD revlog reader (archived) and MIT wrappers around the GPL `hg` binary escape it |

---

## 1. Git

Git's reference implementation is GPLv2, which is exactly why the permissive reimplementation
field is so crowded and mature.

### 1.1 gitoxide — the Rust flagship

- **Repo:** https://github.com/GitoxideLabs/gitoxide — crate: [`gix`](https://crates.io/crates/gix)
- **License:** **MIT OR Apache-2.0** (both LICENSE files at repo root; crates.io SPDX
  `MIT OR Apache-2.0` for gitoxide v0.56.0 / gix v0.86.0). ⚠ GitHub's automatic license badge
  shows only Apache-2.0 — the README/LICENSE pair is authoritative.
- **Nature:** genuine from-scratch pure Rust across ~70 `gix-*` crates (`gix-odb`, `gix-ref`,
  `gix-protocol`, `gix-transport`, `gix-diff`, `gix-merge`, …). Zero libgit2/git2 dependency
  (verified from Cargo.toml). One nuance: CVE-2024-40644 showed `gix-path` on Windows could
  execute a discovered `git.exe` for *config-path discovery* — not delegation of any VCS operation.
- **Implemented natively** (verified against live `crate-status.md`, 2026-08-03):
  - clone, fetch, ls-refs, **shallow** variants — protocol v1/v2 over smart HTTP (curl and reqwest backends)
  - status / worktree-vs-index comparison; blob and tree diffing
  - commit creation and low-level ref/object/index mutation
  - worktree checkout "just as fast as git"
- **Missing** (explicit unchecked items in `crate-status.md`; two broader claims were refuted 0-3
  in verification, so these gaps are solid):
  - **push** — send-pack / receive-pack client plumbing not implemented
  - merge is **partial**: three-way blob/tree/commit merge (merge/diff3/zdiff styles, recursive
    commit merge) is done, but cherry-pick / revert / sequencer / MERGE_HEAD orchestration is not
  - `ssh://` shells out to the external `ssh` binary; `file://` launches `git-upload-pack` —
    no self-contained ssh/file transports
- **Maturity:** 11,750 stars, ~16k commits, pushed 2026-08-03. `gix` crate self-classified
  "initial development / usable"; the `gix`/`ein` CLI binaries are explicitly unstable.
- **Production users:** **Cargo** (`-Zgitoxide` nightly feature replaces git2 for fetches of git
  dependencies and the crates index, incl. shallow — tracking issue
  [rust-lang/cargo#11813](https://github.com/rust-lang/cargo/issues/11813)), Helix, GitButler.
- **Verdict:** best Rust option for read/fetch/diff/commit workloads today; pair with something
  else for push until send-pack lands.

### 1.2 go-git — the mature Go option

- **Repo:** https://github.com/go-git/go-git
- **License:** **Apache-2.0** (LICENSE file, Sourced Technologies 2018).
- **Nature:** from-scratch pure Go, no native deps, plumbing + porcelain APIs. (Its historical
  multi_ack protocol gap is itself proof of genuine reimplementation.)
- **Capability matrix** (re-verified against `COMPATIBILITY.md` on main, 2026-08-03 — an earlier,
  looser version of this claim was refuted, so this is the precise state):
  - Transports: smart HTTP, `ssh://`, `git://`, `file://` **fully supported**; dumb HTTP
    **partial** (requires filesystem-backed storage, no shallow fetch); pack protocol **v2
    unsupported**
  - Fully supported porcelain: clone (incl. `--depth` shallow), fetch, **push**, commit, status,
    diff, tag
  - Partial: **merge — fast-forward only** (no three-way/recursive strategy); pull resolves only
    ff-able merges
  - Unsupported: **rebase**, **stash**
- **Maturity:** stable v5.19.2 (2026-07-29); **v6 still alpha** (v6.0.0-alpha.5) even though the
  main-branch README already documents the `/v6` import path. Pushed 2026-08-03.
- **Production users:** Gitea, Keybase, Pulumi, Kubernetes Prow, Flux.

### 1.3 JGit — the most complete permissive implementation, period

- **Repo:** https://github.com/eclipse-jgit/jgit (Eclipse Foundation)
- **License:** **Eclipse Distribution License 1.0 — textually BSD-3-Clause.**
  ⚠ Do not confuse EDL with Eclipse's copyleft **EPL**. The project states: "Absolutely no GPL,
  LGPL or EPL contributions are accepted within this package." Minor caveat from the docs: audit
  individual distribution artifacts, as "some source code and binaries may be distributed under
  different terms."
- **Nature:** from-scratch pure Java, standalone, no native libraries.
- **Capabilities:** full read/write client — fetch **and push** over ssh, `git://`, smart HTTP,
  Amazon S3, and bundles; read/update/write of the index; **merge and rebase implemented**.
- **Staleness trap (caught in verification):** JGit's own README still lists "shallow and partial
  cloning" as missing, but that list hasn't been substantively revised since ~2021 —
  **shallow clone/fetch has been supported since JGit 6.3 (Sept 2022)**
  (`CloneCommand`/`FetchCommand` `setDepth`, `setShallowSince`, `addShallowExclude`; CLI in 6.5).
- **Genuinely missing as of 7.x:** client-side partial clone, SHA-256 object IDs, multiple working
  trees (git-worktree), multi-pack index, split index, remote/credential helpers, push signing;
  pushed packs are not deltified (larger than C-git packs).
- **Maturity:** Eclipse maturity rating "Mature"; 11k+ commits; 7.7.1 released 2026-07-24, 7.8.0
  planned Sept 2026; maintained 5.x/6.x/7.x branches; Java 17+.
- **Production users:** Gerrit, Eclipse EGit, much of the Java ecosystem.

### 1.4 Dulwich — the Python option

- **Repo:** https://github.com/jelmer/dulwich — PyPI: `dulwich` (1.2.12, July 2026)
- **License:** **Apache-2.0 OR GPL-2.0-or-later** (per-file dual licensing; COPYING + PyPI SPDX
  confirm). **Elect the Apache-2.0 branch** and it is permissive.
- **Nature:** from-scratch pure Python — no `git` binary needed (unlike GitPython), no libgit2
  (unlike pygit2). "Pure Python" now means "pure-Python core with optional self-contained Rust
  accelerators" (its own code, default-built, skippable via `--pure`).
- **Capabilities:** targets full wire-format and repository-format compatibility with C Git,
  including **protocol v2** and **SHA-256 repos**; smart-protocol client *and* server. Per-command
  porcelain coverage has gaps vs. C git (documented by the project).
- **Production users:** **Poetry** (in-process git client since 1.2.0).

### 1.5 isomorphic-git — the JavaScript option

- **Repo:** https://github.com/isomorphic-git/isomorphic-git — npm `isomorphic-git` (MIT, v1.40.0)
- **License:** **MIT** (LICENSE.md; all deps — pako, sha.js, crc-32, diff3 — pure permissive JS).
- **Nature:** from-scratch pure JS, runs in **Node and browsers**, no native modules. (The project
  formally rejected a proposal to build on libgit2 — issue #268.)
- **Capabilities:** ~69 documented commands including clone, fetch, **push**, pull, commit,
  checkout, branch, merge, tag, status, log, stash — full read/write over the wire.
  Caveats: merge throws `MergeNotSupportedError` on unresolvable conflicts (no recursive
  strategy); browser clone/push requires a CORS proxy.
- **Maturity:** 8,303 stars, pushed 2026-08-02. Production user: Logseq git sync.
- ⚠ Adjacent anti-find: **es-git** (toss/es-git, MIT, active) looks like a competitor but its
  Cargo.toml depends on `git2` with `vendored-libgit2` — it is **libgit2 bindings**, not
  from-scratch.

### 1.6 Game of Trees (got) — the ISC-licensed C implementation

- **Site:** https://gameoftrees.org — portable: https://github.com/ThomasAdam/got-portable
- **License:** **ISC** (LICENCE verified: © 2017–2026 Stefan Sperling). FAQ verbatim: "Most of
  Got's code was written from scratch, carries the copyright of its authors, and is released
  under the ISC licence."
- **Nature:** from-scratch C, by OpenBSD developers, since 2017. Uses the **standard Git
  repository format** ("Git can be used for any functionality which has not yet been implemented
  in Got"). Deliberately its own CLI design, not a git-CLI clone.
- **Capabilities:** full VCS — clone/fetch over `git://`, ssh, and **smart** HTTP(S) (dumb HTTP
  unsupported); commit, branch, merge, **rebase, histedit**; **push via `got send`** (ssh/git
  only — not over HTTP). Ships its own server stack (`gotd`/`gotsh`), which serves the standard
  Git wire protocol over SSH; `gotd` does not yet support sha256 repositories.
- **Maturity:** version 0.127 (2026-07-20; portable 2026-06-20), packaged in OpenBSD ports,
  self-hosts its own repos. Mature and very active.
- **Relevance:** the answer to "permissive libgit2-style C implementation" — because libgit2
  itself is not permissive (see §1.8).

### 1.7 Jujutsu — great tool, wrong category

- **Repo:** https://github.com/jj-vcs/jj — **Apache-2.0** (verified).
- Its Git backend stores commits as **real Git objects** in a regular Git repo — colocated
  workspaces are directly usable by the git CLI, with jj-only metadata kept separately in
  `.jj/repo/store/extra/`.
- But jj is **not a wire-protocol reimplementation**: docs state "git is used for remote
  operations under the hood." `git.subprocess=true` became the default (~v0.25) and the libgit2
  fetch/push path was **removed entirely in v0.30** — the git subprocess is the only transport.
  (A claim that jj's Git-format handling comes entirely from gitoxide was refuted 1-2 — its exact
  internal library split should not be asserted.)
- No submodules/hooks/LFS; conflicted commits use a `.jjconflict-*` tree encoding.

### 1.8 libgit2 — the licensing caveat everyone trips on

- **Repo:** https://github.com/libgit2/libgit2
- **Nature:** genuine from-scratch portable C implementation of Git core methods.
- **License:** **GPLv2 with Linking Exception** — *not* permissive. Semantics (verified from
  COPYING and maintainer statements):
  - You may link libgit2 (statically or dynamically) into an application of **any** license,
    proprietary included, with **no** source-disclosure obligation for your application.
  - **Modifications to libgit2 itself remain GPLv2.**
  - **Distributing libgit2** (e.g. bundled with your app) obliges you to make *libgit2's own*
    source available on request.
  - Net: safely *embeddable*, but not *vendorable/relicensable* as permissive code — the key
    distinction vs. gitoxide/go-git/JGit.
- **Maturity:** powers GitHub.com, Azure DevOps, Plastic SCM; bindings for 40+ languages
  (git2-rs, pygit2, LibGit2Sharp, …); v1.9.6 released 2026-07-18; pushed 2026-08-03.
- **wasm-git** (libgit2 compiled to WebAssembly) inherits exactly these terms — verified from its
  composite COPYING; it is a wrapper, not a reimplementation.

### 1.9 Other from-scratch Git implementations (footnotes)

| Project | Lang | License | Notes |
|---|---|---|---|
| [ocaml-git](https://github.com/mirage/ocaml-git) | OCaml | ISC ✔ | Pure OCaml (MirageOS); read/write objects/packs; clone/fetch over git://, smart HTTP, ssh; **push experimental**; no server side. Storage layer for Irmin. ~2.7k commits, maintained. |
| [git9](https://github.com/oridb/git9) | Plan 9 C | MIT ✔ | The 9front git. Read/write incl. push over git:// and git+ssh; exposes repo as read-only 9P filesystem; no index/staging by design. Maintained niche. |
| [js-git](https://github.com/creationix/js-git) | JS | MIT ✔ | Historical (Kickstarter 2013); modular pure-JS object/packfile toolkit; dormant since 2019; superseded by isomorphic-git. |
| [dart-git](https://github.com/GitJournal/dart-git) | Dart | Apache-2.0 ✔ | "A Git implementation in pure Dart"; experimental; powers the GitJournal notes app. Alive (pushed 2026-05). |
| hs-git (Hackage `git`) | Haskell | BSD-3 ✔ | "Reimplementation of git storage and protocol in pure Haskell" — in practice store-only (no index/worktree/checkout/fetch). Repo archived 2021. (Haskell's better-known `gitlib` is an abstract API over **libgit2 bindings**, not a reimplementation.) |
| [xgit](https://github.com/elixir-git/xgit) | Elixir | Apache-2.0 ✔ | "Pure Elixir native implementation of git"; archived 2020. |
| Ziggit | Zig | **GPL-2.0 ✘** | The only complete Zig implementation — claims full porcelain incl. push, built-in server, WASM build, 4-10x speed (self-reported). Young (created 2026-03). **Fails the permissive criterion.** |
| NGit / GitSharp | C# | undeclared / EDL-derived | Both are **JGit ports** (machine-translated / manual), both dead (NGit archived, last push 2020). No maintained from-scratch C# implementation exists — LibGit2Sharp (bindings) dominates. |

**Verified negatives:** no notable from-scratch pure-Swift or pure-Kotlin implementation exists
(ecosystems are libgit2 bindings and JGit wrappers/ports respectively).

---

## 2. Subversion

### 2.0 The structural insight

**Apache Subversion itself is Apache-2.0** (verified from the ASF LICENSE on trunk; bundled
subcomponents are all BSD/MIT-style — no GPL/LGPL). Git's reference impl is GPLv2 and
Mercurial's is GPLv2+, but SVN's is permissive — so the usual licensing motivation for a
from-scratch rewrite does not exist for SVN. **Binding the official `libsvn_*` libraries is
already license-clean.** A from-scratch SVN client buys you purity (no C deps, async, memory
safety), not license freedom.

Maintained bindings (bindings, not reimplementations — listed for the pragmatic path):

| Binding | Lang | License | Notes |
|---|---|---|---|
| pysvn | Python | Apache 1.1 ✔ (verified from `LICENSE.txt` in the SourceForge tree) | Full client-level API; 1.9.25 supports SVN ≤ 1.14.5; maintained. |
| SharpSvn | C#/.NET | Apache-2.0 ✔ | C++/CLI compilation of libsvn; full client; minimally maintained since ~2017. |
| subversion-rs | Rust | Apache-2.0 ✔ | Early-stage libsvn bindings by jelmer (subvertpy author); active 2026. **Distinct from lvillis/svn-rs.** |
| subvertpy | Python | LGPL-2.1+ (its own code) | libsvn bindings incl. a hookable server-side ra_svn; maintained (0.11.1, 2026-03). |

### 2.1 svn-rs — the only permissive from-scratch SVN client, in any language

- **Repo:** https://github.com/lvillis/svn-rs — crate: [`svn`](https://crates.io/crates/svn)
- **License:** **MIT** (LICENSE verified: © 2025 lvillis; Cargo.toml + crates.io agree).
- **Nature:** verified by **cloning and inspecting the source**: genuine from-scratch async Rust
  implementation of the **ra_svn wire protocol** (native `src/rasvn/` module). Zero
  libsvn/-sys dependencies; no shelling out to `svn` (grep-verified — the only external-command
  call in the tree is Windows test scaffolding). Even `svn+ssh://` tunneling is pure Rust via
  `russh` 0.60 — no OpenSSH dependency.
- **Capabilities** (README + source):
  - Protocols: `svn://` and `svn+ssh://`, talking directly to `svnserve`; auth: ANONYMOUS /
    PLAIN / CRAM-MD5 (+ optional Cyrus SASL)
  - Read: revisions, files, directories, logs, locations, mergeinfo, properties, file revs, locks
  - Report/editor flows: update, switch, status, diff, replay, replay-range
  - **Write: commit editor commands, lock/unlock, revprop changes**
  - Interop-tested against a real `svnserve` (opt-in: `SVN_INTEROP=1 cargo test`, 504-line fixture
    spawning svnadmin/svnserve)
- **Explicitly not included:** working-copy management, native TLS for `svn://`, full OpenSSH
  parity. **HTTP/WebDAV (DeltaV) is absent** (implicit — the library speaks ra_svn exclusively;
  the README never mentions WebDAV).
- **Maturity — the honest picture:** first commit 2025-12-28; v0.1.13 released 2026-06-11 (also
  the last commit; 5 open PRs pending); ~25.5k lines of Rust in src/ (~28.8k with tests);
  3 GitHub stars, ~260 lifetime crates.io downloads; MSRV 1.96. Serious, well-tested code — but
  pre-1.0, single-author, and unproven in production.

### 2.2 SVNKit — complete but disqualified

- **Site:** https://svnkit.com
- **Nature:** the most complete pure-Java from-scratch SVN reimplementation anywhere — ra_svn
  **and** HTTP (WebDAV/DeltaV) **and** FILE protocols, working-copy management, full read/write.
- **License:** **TMate Open Source License — NOT permissive.** Verified verbatim from
  https://svnkit.com/license.html: "Redistributions in any form must be accompanied by
  information on how to obtain complete source code for the software that uses SVNKit" — the
  disclosure obligation reaches your **entire application** (broader than LGPL, GPL-like in
  effect). Escape hatch: commercial licenses at **EUR 5,000 (Basic) / 7,000 (Standard) /
  15,000 (Enterprise)**, each with 12 months of updates/support.

### 2.3 SVN dump-format tooling (adjacent, useful)

- **[eduardosm/svn2git](https://github.com/eduardosm/svn2git)** — Rust, **Apache-2.0 OR MIT** ✔,
  actively maintained (pushed 2026-07). Native parser of `svnadmin dump` format v2/v3 including
  deltas; needs neither git nor svn installed. Documented converting GCC's 280,157-revision repo
  in ~50 min — the most production-grade non-libsvn SVN-format code outside SVNKit. If the goal
  is *reading SVN repository history* rather than live server access, dump parsing is a proven
  permissive path.
- Dormant/copyleft dump parsers, for completeness: jwiegley/svndump (Haskell, BSD-3, dormant),
  cstroe/svndumpapi (Java, **AGPL-3.0**), erijo/py-svndump (Python, **GPL-3.0**).

### 2.4 Verified negative — the rest of the field

Beyond svn-rs and SVNKit, **no mature from-scratch SVN client implementation exists in any
language**, and **no native WebDAV/DeltaV (ra_serf-equivalent) client library exists at all.**
Fragments found and rejected: cespedes/svn (Go, MIT, client+server ra_svn WIP — "some operations
already work, but not completely", dormant since 2024-05, 3 stars); myelin/pure-php-subversion
(MIT, checkout/update over HTTP, dead since ~2006); mattrobenolt/python-svnserve (toy faux
server, **no license file**); Nvx.Svn (C#, 1 commit, 2012, unlicensed); @massapi/svn-mcp
(Node, MIT, claims "native svn://" but implements only log listing; single release 2026-05).
Go's visible `svn` packages (jhinrichsen/svn) are CLI wrappers.
Search angles: per-language web queries, GitHub API repo searches (ra_svn, svnserve,
svn protocol), crates.io and npm registry searches.

---

## 3. Mercurial

### 3.0 The verified negative

**As of 2026-08-03, no permissively-licensed (MIT/Apache/BSD/ISC) from-scratch Mercurial
implementation with write capability or native wire-protocol support exists in any language.**
Two independent sweeps across GitHub, crates.io, npm, PyPI, Hackage, and the histories of
Sourcegraph/Mozilla/Octobus tooling confirmed it.

The cause is **provenance, not protocol difficulty**: Mercurial is GPLv2+, and nearly everything
hg-compatible was built *from Mercurial's code* rather than from format documentation, so the
GPL propagates through the entire ecosystem.

### 3.1 The GPL cluster (what you cannot use permissively)

| Project | License | Why |
|---|---|---|
| **hg-core** (crates.io / in-tree `rust/hg-core`) | GPL-2.0-or-later ✘ | Mercurial's official pure-Rust core (revlog, dirstate, status, ancestry, discovery). Actively developed **in-tree** (0.1.0, edition 2024); the crates.io release is a stale 0.0.1 from 2019. Licensed via Mercurial's repo-wide COPYING. |
| **rhg** | GPLv2+ ✘ | The Rust hg CLI shipped with Mercurial; subset of commands (status, cat, files, root, annotate, config, …) with Python fallback; "used in production successfully for years" per the project, still labeled experimental. |
| **hg-parser** | GPL-2.0-or-later ✘ | Standalone Rust revlog/changelog parser — but derived from Mononoke's parse code, which derives from Mercurial. |
| **Sapling** (facebook/sapling) | **GPL-2.0** ✘ (root LICENSE) | The `sl` CLI is a Mercurial fork — massive Rust rewriting does not cleanse provenance. MIT applies only to the website and ISL web UI (+ some vendored crates). Functions today as a **Git-compatible** client; the vanilla-Mercurial-server path is effectively dead for external users (docs describe an EdenAPI protocol "very different from the original wireprotocols"; hg-style clones fail per issue #172). Mononoke (a from-scratch server rewrite, still GPL) and EdenFS are "not yet supported for external usage." |
| hg-git | GPL-2 ✘ | An hg extension importing `mercurial` modules. |
| Hg4J | GPL-2 (dual, commercial option) ✘ | Pure-Java revlog implementation. |

### 3.2 What escapes the GPL

**hgo — the only permissive from-scratch implementation (read-only, archived)**
- **Repo:** https://github.com/knieriem/hgo — **BSD 3-Clause** ✔ (© 2013 The hgo Authors)
- Pure Go, "implemented from scratch, based on information found in Mercurial's wiki" — i.e. a
  clean-room build from format docs, which is what makes the permissive license possible.
- **Read-only access to local repos**: revlogs, changelog, manifest, tags. Repo requirements
  revlogv1/store/fncache/dotencode only (fncache partial — no hash-encoded names). No commit,
  no push, no wire protocol. README: "should be considered unstable."
- **Archived 2024-05-02.** Fork lineage: beyang/hgo (last push 2015) fed Sourcegraph's go-vcs
  native hg package (BSD-2), which Sourcegraph dropped ~2019.
- Its historical value: **proof that clean-room-from-format-docs is legally and technically
  feasible** for Mercurial.

**git-cinnabar — the nearest non-GPL wire-protocol option (weak copyleft)**
- **Repo:** https://github.com/glandium/git-cinnabar — **MPL-2.0** for the git-cinnabar source ✘
  (not permissive, but not GPL); distributed binaries are GPL-2.0 due to the vendored git-core.
- From-scratch Rust implementation of the **Mercurial wire protocol** (bundle2, changegroups) as
  a git remote helper — "doesn't use a local mercurial clone under the hood." **Full clone/pull
  AND push** against real hg servers, mapped to/from the git object model (no native .hg store).
- Battle-tested for years by Mozilla developers against hg.mozilla.org; 3,600+ commits, active.

**Command-server wrappers — permissive code, GPL runtime dependency**
Mercurial's developers designed the command-server protocol *specifically* so non-GPL clients
could exist. These are wrappers (the excluded category), listed because for Hg they are the only
maintained option:

| Wrapper | Lang | License | State |
|---|---|---|---|
| python-hglib | Python | MIT ✔ | Semi-official (mercurial-scm.org); last release 2.6.2 (2020); widely used |
| JavaHg | Java | MIT ✔ | Mature but dormant (~2013); powered MercurialEclipse |
| hglib (gem) | Ruby | BSD-3 ✔ | v0.11.1 (2020) |
| node-hg | JS | MIT ✔ | Dormant (2018) |
| hglib (crate) | Rust | MIT ✔ | Abandoned prototype (~241 LoC, 2017); hglib-rs is MPL-2.0 |
| hg.net | C# | MIT ✔ | Dormant (last push 2015) |

All require a GPL `hg` installation at runtime; they implement the command-server protocol, not
the wire protocol or revlog format.

### 3.3 Practical options for Mercurial support

1. **Read-only**: parse revlogs à la hgo (resurrect/port it, or clean-room from
   https://wiki.mercurial-scm.org/ format docs — the only license-safe source material).
2. **Full read/write, permissive code**: command-server wrapper with `hg` as a runtime dep.
3. **Wire protocol without GPL**: accept MPL-2.0 and study/use git-cinnabar.
4. **Write a permissive implementation**: it would be the first in existence. Work only from the
   wiki/format documentation, never from Mercurial/Sapling/Mononoke source.

---

## 4. License gotchas — quick reference

| Project | License | The catch |
|---|---|---|
| **git (C reference)** | GPLv2 | Why the reimplementation field exists |
| **Mercurial** | GPLv2+ | Provenance contaminates nearly every hg-adjacent project |
| **Apache Subversion** | Apache-2.0 | The reference impl is already permissive — bindings are fine |
| gitoxide | MIT OR Apache-2.0 | GitHub's badge shows Apache-only; the repo is dual — cite the README/LICENSE pair |
| JGit | EDL 1.0 | It *is* BSD-3 — never confuse EDL with the copyleft EPL; audit distribution artifacts |
| Dulwich | Apache-2.0 OR GPL-2.0+ | Dual — explicitly elect the Apache branch |
| libgit2 | GPLv2 + linking exception | Embeddable in anything; modifications and redistribution of libgit2 itself stay GPL; not vendorable as permissive code |
| SVNKit | TMate | "Open source" branding, copyleft effect (entire app must open source); EUR 5–15k to escape |
| Sapling | GPL-2.0 | The Rust rewrite doesn't cleanse Mercurial provenance; MIT parts are UI/website only |
| Jujutsu | Apache-2.0 | Fine — but it's not a wire-protocol implementation (shells out to git for transport) |
| es-git, wasm-git | MIT wrapper / libgit2 terms | Both sit on libgit2 — not from-scratch |

---

## 5. Recommendations

**Git**
- Rust: **gitoxide** — production-ready for clone/fetch/read/diff/commit (Cargo uses it); no push
  yet, partial merge, external-ssh transport. For push today: shell out to git, or accept
  libgit2's linking exception via `git2-rs`.
- Need full read/write from-scratch now: **go-git** (Go — mind ff-only merge, no rebase),
  **JGit** (Java — the most complete), **isomorphic-git** (JS/browser), **got** (C/CLI, ISC).
- Python: **Dulwich** (Apache branch).

**SVN**
- Pragmatic/mature: bind Apache-2.0 **libsvn** (subversion-rs for Rust; pysvn for Python) —
  license-clean because SVN's core is permissive.
- Pure-Rust, no C deps: **svn-rs** — the only native permissive client anywhere; ra_svn only
  (no WebDAV, no working copy), pre-1.0, single-author. Treat as promising-but-unproven;
  budget for contributing fixes upstream.
- History-mining alternative: **eduardosm/svn2git**'s dump parsing (Apache-2.0/MIT).

**Mercurial**
- Accept the verified negative: there is nothing permissive to adopt with write or wire-protocol
  capability. Choose between read-only revlog parsing (hgo lineage / clean-room from wiki docs),
  MIT command-server wrappers with a GPL `hg` runtime dep, or MPL-2.0 git-cinnabar. A permissive
  native implementation would have to be written from format documentation — hgo proves the
  clean-room path is viable.

---

## Appendix: primary sources

- gitoxide: https://github.com/GitoxideLabs/gitoxide · [crate-status.md](https://github.com/GitoxideLabs/gitoxide/blob/main/crate-status.md) · Cargo tracking [rust-lang/cargo#11813](https://github.com/rust-lang/cargo/issues/11813) · [Cargo unstable docs](https://doc.rust-lang.org/cargo/reference/unstable.html)
- go-git: https://github.com/go-git/go-git · [COMPATIBILITY.md](https://github.com/go-git/go-git/blob/main/COMPATIBILITY.md)
- JGit: https://github.com/eclipse-jgit/jgit · https://www.eclipse.org/jgit/ · [EDL 1.0 text](https://www.eclipse.org/org/documents/edl-v10.php)
- Dulwich: https://github.com/jelmer/dulwich · https://pypi.org/project/dulwich/
- isomorphic-git: https://github.com/isomorphic-git/isomorphic-git · [command index](https://isomorphic-git.org/docs/en/alphabetic)
- Game of Trees: https://gameoftrees.org · https://github.com/ThomasAdam/got-portable
- Jujutsu: https://github.com/jj-vcs/jj · [git-compatibility](https://docs.jj-vcs.dev/latest/git-compatibility/) · [jj#5548](https://github.com/jj-vcs/jj/issues/5548)
- libgit2: https://github.com/libgit2/libgit2 · [maintainer licensing answer](https://github.com/libgit2/discussions/issues/12)
- svn-rs: https://github.com/lvillis/svn-rs · https://crates.io/crates/svn
- SVNKit: https://svnkit.com/licensing.html · https://svnkit.com/license.html
- Apache Subversion LICENSE: https://raw.githubusercontent.com/apache/subversion/trunk/LICENSE
- svn2git: https://github.com/eduardosm/svn2git
- hgo: https://github.com/knieriem/hgo
- Sapling: https://github.com/facebook/sapling · [LWN analysis](https://lwn.net/Articles/915136/)
- hg-core: https://docs.rs/hg-core · hg-parser: https://github.com/kilork/hg-parser
- git-cinnabar: https://github.com/glandium/git-cinnabar
- python-hglib: https://pypi.org/project/python-hglib/

*Generated from a two-round verified research run (2026-08-03/04): 115 agents, 21 sources fully
fetched in round 1, 25 claims through 3-vote adversarial verification, 8 targeted re-verifications
and 3 landscape sweeps in round 2. Claims that failed verification were dropped or corrected;
capability statements reflect repo state on the research date and will age — gitoxide and jj in
particular move fast.*
