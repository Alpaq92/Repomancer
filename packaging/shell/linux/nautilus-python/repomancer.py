# SPDX-License-Identifier: Apache-2.0
# Repomancer — Nautilus (GNOME) context-menu extension (Tier 1).
#
# A proper cascading "Repomancer" submenu that appears only on Git working
# trees, with state-aware items (conflict resolution shows only during a
# merge). Menu building does filesystem stats ONLY — never a git subprocess —
# so the menu renders instantly; the app itself does the real work when an
# item is activated (repomancer --action=<verb> <path>).
import os
import shutil
import subprocess

import gi
gi.require_version("Nautilus", "4.0")
from gi.repository import GObject, Nautilus as Nemo  # noqa: E402

# The installer substitutes the resolved binary path; fall back to PATH.
BIN = "@REPOMANCER_BIN@"
if BIN.startswith("@") or not os.path.exists(BIN):
    BIN = shutil.which("repomancer") or "repomancer"


def _git_dir(path):
    """The .git directory for a working tree at `path`, or None. Handles the
    common case (a .git directory) and a .git *file* (worktrees/submodules)."""
    dot = os.path.join(path, ".git")
    if os.path.isdir(dot):
        return dot
    if os.path.isfile(dot):
        try:
            with open(dot, "r", encoding="utf-8", errors="replace") as fh:
                line = fh.readline().strip()
            if line.startswith("gitdir:"):
                return os.path.normpath(
                    os.path.join(path, line.split(":", 1)[1].strip()))
        except OSError:
            return None
    return None


def _repo_root(path):
    """The working-tree root containing `path` (a file or directory), or
    None. Walks up until a .git is found — so files and subfolders resolve
    to their repository, not just the top-level folder."""
    p = path if os.path.isdir(path) else os.path.dirname(path)
    p = os.path.abspath(p)
    while True:
        if _git_dir(p) is not None:
            return p
        parent = os.path.dirname(p)
        if parent == p:
            return None
        p = parent


class RepomancerMenuProvider(GObject.GObject, Nemo.MenuProvider):
    def _run(self, _item, action, path):
        try:
            subprocess.Popen([BIN, "--action=" + action, path],
                             start_new_session=True)
        except OSError:
            pass

    def _menu_for(self, path):
        root = _repo_root(path)
        if root is None:
            return None  # not inside a repo → no Repomancer menu
        gitdir = _git_dir(root)
        is_file = os.path.isfile(path)

        top = Nemo.MenuItem(name="Repomancer::Top", label="Repomancer",
                            tip="Version control with Repomancer")
        menu = Nemo.Menu()
        top.set_submenu(menu)

        seq = [0]

        # Icons are the app's Lucide glyphs, outlined into fill-based
        # -symbolic icons so the file manager recolours them to its own text
        # colour (black on light, white on dark).
        def add(verb, label, icon, tip):
            seq[0] += 1
            item = Nemo.MenuItem(name="Repomancer::%d" % seq[0], label=label,
                                 tip=tip, icon=icon)
            item.connect("activate", self._run, verb, path)
            menu.append_item(item)

        def sep():
            seq[0] += 1
            menu.append_item(Nemo.MenuItem(
                name="Repomancer::sep%d" % seq[0], label="─" * 12,
                sensitive=False))

        if is_file:
            # File-scoped: this file's history and line attribution.
            add("history", "File History…", "repomancer-history-symbolic",
                "Commits that touched this file")
            add("blame", "Blame…", "repomancer-blame-symbolic",
                "Line-by-line attribution for this file")
            sep()

        add("log", "Log…", "repomancer-log-symbolic", "Open the commit history")
        add("commit", "Commit…", "repomancer-commit-symbolic",
            "Stage and commit changes")
        # State-aware: a resolve entry only while a merge is in progress.
        if os.path.exists(os.path.join(gitdir, "MERGE_HEAD")):
            add("commit", "Resolve Conflicts…", "repomancer-commit-symbolic",
                "Resolve the in-progress merge")
        add("sync", "Sync (Fetch)…", "repomancer-sync-symbolic",
            "Fetch from remotes")
        add("settings", "Settings…", "repomancer-settings-symbolic",
            "Open Repomancer preferences")
        return [top]

    # Nemo dropped the `window` arg across versions; accept both shapes.
    def get_file_items(self, *args):
        files = args[-1]
        if len(files) != 1:
            return []  # a single target keeps the action unambiguous
        path = files[0].get_location().get_path()
        if not path:
            return []
        # Files, subfolders and the repo root alike: the menu resolves the
        # enclosing repository.
        return self._menu_for(path) or []

    def get_background_items(self, *args):
        folder = args[-1]
        path = folder.get_location().get_path()
        if not path:
            return []
        return self._menu_for(path) or []
