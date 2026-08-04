# statusd IPC schema — v1 draft

Contract between `repomancer-statusd` and its clients (GUI, Windows shell DLL,
FinderSync extension, Linux FM plugins). Reviewed before any shell code exists
(implementation-plan.md §2, §12.6); shell plugins are clients of this protocol
forever, so changes after M6 require version negotiation.

## Transport

| OS | Endpoint |
|---|---|
| Windows | named pipe `\\.\pipe\repomancer-statusd-<user-sid>` — created with `FILE_FLAG_FIRST_PIPE_INSTANCE` and an owner-only security descriptor |
| macOS / Linux | Unix domain socket `$XDG_RUNTIME_DIR/repomancer/statusd.sock` (fallback `~/.cache/repomancer/statusd.sock`), socket directory mode `0700` |

Peer authentication (§13.4): same-UID check via `SO_PEERCRED` /
`LOCAL_PEERCRED`; on Windows, `GetNamedPipeClientProcessId` + token SID
comparison. Non-matching peers are disconnected without a response.

## Framing

Length-prefixed JSON: 4-byte little-endian unsigned frame length, then UTF-8
JSON. **Max frame: 1 MiB** in either direction; oversized frames close the
connection. No batching; one request per frame, one response per frame.
Server-initiated event frames are permitted on subscribed connections.

## Envelope

Request:

```json
{ "v": 1, "id": 42, "method": "status.query", "params": { "path": "/abs/path" } }
```

Response (exactly one per request; `ok` xor `error`):

```json
{ "v": 1, "id": 42, "ok": { "state": "modified" } }
{ "v": 1, "id": 42, "error": { "code": "not_registered", "message": "…" } }
```

Event (no `id`; only after a successful `status.subscribe`):

```json
{ "v": 1, "event": "status.changed", "params": { "root": "/abs/repo", "paths": ["a.txt"] } }
```

Unknown fields are rejected (strict schema, §13.4). Unknown *methods* return
`error.code = "unknown_method"` — clients newer than the daemon must degrade.

## Methods

| Method | Params | Result | Notes |
|---|---|---|---|
| `daemon.hello` | `{ "client": "gui\|shell-win\|shell-mac\|shell-linux", "version": "0.1.0" }` | `{ "protocol": 1, "daemon_version": "0.1.0" }` | must be first call on a connection |
| `daemon.ping` | `{}` | `{}` | liveness; shell plugins use a hard 50 ms deadline |
| `daemon.shutdown` | `{}` | `{}` | GUI-only (peer check + client kind) |
| `status.query` | `{ "path": "/abs/path" }` | `{ "state": "clean\|modified\|conflicted\|ignored\|untracked\|unknown", "repo": "/abs/root\|null" }` | answered from cache only — never computes in-band; unregistered path ⇒ `state:"unknown"` instantly |
| `status.subscribe` | `{ "root": "/abs/repo" }` | `{}` | connection receives `status.changed` events for that root |
| `roots.list` | `{}` | `{ "roots": [ { "path": "…", "vcs": "git", "watch": "fsmonitor\|native\|poll\|off" } ] }` | |
| `roots.add` | `{ "path": "/abs/repo" }` | `{ "root": {…} }` | registration is explicit (§5.1) — the daemon never scans on its own |
| `roots.remove` | `{ "path": "/abs/repo" }` | `{}` | |
| `menu.state` | `{ "path": "/abs/path" }` | `{ "verbs": ["commit","log","sync","resolve"] }` | drives state-aware context menus; cache-fed, same 50 ms budget |

## Error codes

`unknown_method` · `bad_request` (schema violation) · `not_registered` ·
`limit_exceeded` · `internal`. Paths in requests are canonicalized by the
daemon; requests resolving outside registered roots answer `state:"unknown"`
(query) or `not_registered` (mutating methods) — the daemon never becomes an
arbitrary-filesystem oracle (§13.4).

## Degradation contract

If connect or any call exceeds the client's deadline (shell plugins: 50 ms),
the client renders **nothing** (no overlay, static menu verbs) and retries
lazily. The daemon exits after ~10 idle minutes; any client may relaunch it
on demand (§5.1).

## Security invariants

- statusd holds **no credentials** (§13.5) and performs **no network I/O**.
- Exec-capable VCS config is never honored, trusted repo or not (§13.1).
- Per-connection rate limit; the shell DLL parses no repo data in-proc.
