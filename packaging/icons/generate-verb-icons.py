#!/usr/bin/env python3
# Regenerate the per-verb shell-menu icons from the app's Lucide set so they
# stay identical to the in-window menu glyphs. Run from the repo root after
# changing gui/src/icons.h:  python3 packaging/icons/generate-verb-icons.py
import re, pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = (ROOT / "gui/src/icons.h").read_text()
OUT = ROOT / "packaging/icons"
STROKE = "#4c6ef5"  # the app-icon indigo; visible on light and dark menus

# shell verb -> the same Lucide constant the app's menus use for that concept
MAPPING = {
    "log": "kGitPullRequestArrow",  # the commit graph
    "commit": "kArrowUpFromLine",   # app's Commit glyph
    "sync": "kArrowDownToLine",     # app's Fetch glyph
    "settings": "kSettings",        # app's Preferences glyph
}

for verb, const in MAPPING.items():
    m = re.search(r'const char\* ' + const + r'\s*=\s*R"svg\((.*?)\)svg"', SRC, re.S)
    if not m:
        raise SystemExit(f"icon {const} not found in icons.h")
    svg = m.group(1).strip()
    svg = svg.replace('stroke="currentColor"', f'stroke="{STROKE}"')
    svg = svg.replace('fill="currentColor"', f'fill="{STROKE}"')
    (OUT / f"repomancer-{verb}.svg").write_text(
        '<?xml version="1.0" encoding="UTF-8"?>\n' + svg + "\n")
    print(f"wrote repomancer-{verb}.svg  ({const})")
