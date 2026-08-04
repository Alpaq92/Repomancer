# Security Policy

## Reporting a vulnerability

Please **do not** open a public issue for security problems.

Use GitHub's private vulnerability reporting:
<https://github.com/Alpaq92/Repomancer/security/advisories/new>
(enable it under Settings → Code security if the link 404s), or email the
maintainer directly. You should receive an acknowledgement within 7 days.

Please include: affected version/commit, platform, reproduction steps, and
impact assessment. Coordinated disclosure is appreciated; we aim to ship fixes
before details are published.

## Scope notes

Repomancer executes locally installed VCS binaries against repository data
that may be attacker-controlled (any cloned repository). The threat model and
mitigations (repository trust gate, config neutralization, parser hardening,
IPC authentication, signed updates) are documented in
[docs/implementation-plan.md §13](docs/implementation-plan.md).

Reports about the behavior of git/svn/hg themselves should go to those
projects; reports about how Repomancer *invokes* them belong here.
