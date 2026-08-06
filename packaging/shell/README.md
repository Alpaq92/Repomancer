# Repomancer shell integration (Tier 0)

"Near-zero code" file-manager entries that launch Repomancer with an action
against the right-clicked folder:

    repomancer --action=<log|commit|sync|settings> <path>

`log` (the default) opens the history; `commit` opens the working-tree view;
`sync` fetches from the remotes; `settings` opens Preferences.

## Linux

    packaging/shell/linux/install.sh [--bin /path/to/repomancer]
    packaging/shell/linux/install.sh --uninstall

Per-user, no root. Installs an application entry + icon and context-menu items
for whichever of **Nemo**, **Nautilus**, **Dolphin** and **Thunar** are present
(Thunar gets a ready-to-paste snippet, since its actions live in one XML file).

## Windows

    powershell -ExecutionPolicy Bypass -File packaging\shell\windows\install.ps1
    powershell ... install.ps1 -Uninstall

Per-user registry verbs — a cascading **Repomancer** entry on folders and
folder backgrounds (top-level on Win10, under "Show more options" on Win11).

## macOS

See `packaging/shell/macos/README.md` — Quick Actions created in Shortcuts.app.

## Tier 1 (later, M6)

Proper native context menus with state-aware items and icon overlays: a COM
DLL on Windows, a FinderSync extension on macOS, and MenuProvider/service
plugins on Linux. See `docs/implementation-plan.md` §"Shell integration".
