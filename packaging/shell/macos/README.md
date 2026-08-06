# Repomancer — macOS Tier-0 shell integration (Quick Actions)

macOS surfaces third-party folder actions through **Quick Actions** in Finder's
right-click menu. Create one per verb in **Shortcuts.app** (or Automator):

1. New Shortcut → add **Receive** *Folders* from **Quick Actions**.
2. Add **Run Shell Script**, input *as arguments*:

   ```sh
   /Applications/Repomancer.app/Contents/MacOS/repomancer --action=log "$1"
   ```

   (swap `--action=log` for `commit` / `sync` / `settings`.)
3. Name it e.g. **Repomancer: Log** and save.

The action then appears under **Quick Actions** when right-clicking a folder.
A FinderSync extension for a proper top-level cascade is the Tier-1 upgrade
(M6) — see docs/implementation-plan.md.
