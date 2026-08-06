#!/usr/bin/env python3
# Regenerate the per-verb shell-menu icons from the app's Lucide set so they
# stay identical to the in-window menu glyphs. Run from the repo root after
# changing gui/src/icons.h:  python3 packaging/icons/generate-verb-icons.py
#
# Output per verb: repomancer-<verb>-symbolic.svg — a FILL-based icon so GTK /
# Nemo recolour it to the menu's text colour (black on light, white on dark).
# Lucide icons are stroke-based and GTK's symbolic recolouring only affects
# fill, so every stroke is outlined into filled shapes here (a quad per
# segment plus a round dot per vertex — overlapping same-colour fills read as
# one continuous round-capped stroke).
import math
import re
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = (ROOT / "gui/src/icons.h").read_text()
OUT = ROOT / "packaging/icons"

# shell verb -> the same Lucide constant the app's menus use for that concept
MAPPING = {
    "log": "kSquareChevronRight",   # a console/prompt chevron: the log
    "history": "kClock4",           # a clock: this file's history over time
    "blame": "kSearch",             # the app's Blame glyph (a magnifier)
    "commit": "kArrowUpFromLine",   # app's Commit glyph
    "sync": "kArrowDownToLine",     # app's Fetch glyph
    "settings": "kSettings",        # app's Preferences glyph
}

STROKE_W = 2.0     # Lucide stroke-width
STEPS = 18         # samples per curve/arc


def flatten_cubic(p0, p1, p2, p3):
    for i in range(1, STEPS + 1):
        t = i / STEPS
        u = 1 - t
        yield (u*u*u*p0[0] + 3*u*u*t*p1[0] + 3*u*t*t*p2[0] + t*t*t*p3[0],
               u*u*u*p0[1] + 3*u*u*t*p1[1] + 3*u*t*t*p2[1] + t*t*t*p3[1])


def flatten_arc(p0, rx, ry, phi, large, sweep, p1):
    if rx == 0 or ry == 0 or p0 == p1:
        yield p1
        return
    phi = math.radians(phi)
    cs, sn = math.cos(phi), math.sin(phi)
    dx, dy = (p0[0]-p1[0])/2, (p0[1]-p1[1])/2
    x1p, y1p = cs*dx + sn*dy, -sn*dx + cs*dy
    rx, ry = abs(rx), abs(ry)
    lam = x1p*x1p/(rx*rx) + y1p*y1p/(ry*ry)
    if lam > 1:
        s = math.sqrt(lam)
        rx *= s
        ry *= s
    den = rx*rx*y1p*y1p + ry*ry*x1p*x1p
    num = rx*rx*ry*ry - rx*rx*y1p*y1p - ry*ry*x1p*x1p
    co = math.sqrt(max(0.0, num/den)) if den else 0.0
    if large == sweep:
        co = -co
    cxp, cyp = co*rx*y1p/ry, -co*ry*x1p/rx
    cx = cs*cxp - sn*cyp + (p0[0]+p1[0])/2
    cy = sn*cxp + cs*cyp + (p0[1]+p1[1])/2

    def ang(ux, uy, vx, vy):
        d = ux*vx + uy*vy
        ln = math.hypot(ux, uy)*math.hypot(vx, vy)
        a = math.acos(max(-1.0, min(1.0, d/ln))) if ln else 0.0
        return -a if ux*vy - uy*vx < 0 else a

    t1 = ang(1, 0, (x1p-cxp)/rx, (y1p-cyp)/ry)
    dt = ang((x1p-cxp)/rx, (y1p-cyp)/ry, (-x1p-cxp)/rx, (-y1p-cyp)/ry)
    if not sweep and dt > 0:
        dt -= 2*math.pi
    elif sweep and dt < 0:
        dt += 2*math.pi
    for i in range(1, STEPS + 1):
        th = t1 + dt*(i/STEPS)
        yield (cs*rx*math.cos(th) - sn*ry*math.sin(th) + cx,
               sn*rx*math.cos(th) + cs*ry*math.sin(th) + cy)


_NUM = r"[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?"


def polylines_from_path(d):
    toks = re.findall(r"[A-Za-z]|" + _NUM, d)
    i = 0
    cur = start = (0.0, 0.0)
    cmd = None
    polys, poly = [], []

    def n():
        nonlocal i
        v = float(toks[i]); i += 1; return v

    while i < len(toks):
        if toks[i].isalpha():
            cmd = toks[i]; i += 1
        rel = cmd.islower(); c = cmd.upper()
        if c == "M":
            x, y = n(), n()
            if rel:
                x, y = cur[0]+x, cur[1]+y
            if poly:
                polys.append(poly)
            cur = start = (x, y); poly = [cur]
            cmd = "l" if rel else "L"
        elif c == "L":
            x, y = n(), n()
            if rel:
                x, y = cur[0]+x, cur[1]+y
            cur = (x, y); poly.append(cur)
        elif c == "H":
            x = n(); cur = (cur[0]+x if rel else x, cur[1]); poly.append(cur)
        elif c == "V":
            y = n(); cur = (cur[0], cur[1]+y if rel else y); poly.append(cur)
        elif c == "C":
            a, b, cc, dd, x, y = n(), n(), n(), n(), n(), n()
            if rel:
                a, b = cur[0]+a, cur[1]+b
                cc, dd = cur[0]+cc, cur[1]+dd
                x, y = cur[0]+x, cur[1]+y
            poly += list(flatten_cubic(cur, (a, b), (cc, dd), (x, y)))
            cur = (x, y)
        elif c == "A":
            rx, ry, rot, lg, sw, x, y = (n(), n(), n(), n(), n(), n(), n())
            if rel:
                x, y = cur[0]+x, cur[1]+y
            poly += list(flatten_arc(cur, rx, ry, rot, int(lg), int(sw), (x, y)))
            cur = (x, y)
        elif c == "Z":
            poly.append(start); cur = start
        else:  # unsupported command -> consume one number to avoid a loop
            n()
    if poly:
        polys.append(poly)
    return polys


def circle_polyline(cx, cy, r, sides=48):
    return [(cx + r*math.cos(2*math.pi*k/sides),
             cy + r*math.sin(2*math.pi*k/sides)) for k in range(sides + 1)]


def rect_polyline(x, y, w, h, rx):
    """A (rounded) rectangle border traced clockwise as a closed polyline."""
    rx = min(rx, w / 2, h / 2)

    def arc(cx, cy, a0, a1, steps=8):
        return [(cx + rx*math.cos(math.radians(a0 + (a1-a0)*i/steps)),
                 cy + rx*math.sin(math.radians(a0 + (a1-a0)*i/steps)))
                for i in range(steps + 1)]

    pts = [(x+rx, y), (x+w-rx, y)]
    pts += arc(x+w-rx, y+rx, 270, 360)     # top-right corner
    pts += [(x+w, y+h-rx)]
    pts += arc(x+w-rx, y+h-rx, 0, 90)      # bottom-right
    pts += [(x+rx, y+h)]
    pts += arc(x+rx, y+h-rx, 90, 180)      # bottom-left
    pts += [(x, y+rx)]
    pts += arc(x+rx, y+rx, 180, 270)       # top-left
    return pts


def icon_polylines(svg):
    polys = []
    for d in re.findall(r'<path[^>]*\bd="([^"]+)"', svg):
        polys += polylines_from_path(d)
    for m in re.finditer(r'<rect\b[^>]*>', svg):
        tag = m.group(0)
        def attr(name, default=0.0):
            found = re.search(name + r'="([-\d.]+)"', tag)
            return float(found.group(1)) if found else default
        polys.append(rect_polyline(attr("x"), attr("y"), attr("width"),
                                   attr("height"), attr("rx")))
    for m in re.finditer(r'<circle\b[^>]*>', svg):
        tag = m.group(0)
        cx = float(re.search(r'cx="([-\d.]+)"', tag).group(1))
        cy = float(re.search(r'cy="([-\d.]+)"', tag).group(1))
        r = float(re.search(r'r="([-\d.]+)"', tag).group(1))
        if 'fill="currentColor"' in tag or r < 1.0:
            polys.append(circle_polyline(cx, cy, r, 24))   # solid dot
        else:
            polys.append(circle_polyline(cx, cy, r))       # stroked ring
    return polys


def outline_shapes(polys, w):
    r = w/2
    out = []
    for poly in polys:
        for (x0, y0), (x1, y1) in zip(poly, poly[1:]):
            dx, dy = x1-x0, y1-y0
            ln = math.hypot(dx, dy)
            if ln < 1e-6:
                continue
            nx, ny = -dy/ln*r, dx/ln*r
            pts = [(x0+nx, y0+ny), (x1+nx, y1+ny), (x1-nx, y1-ny), (x0-nx, y0-ny)]
            out.append('<polygon points="' +
                       " ".join(f"{px:.2f},{py:.2f}" for px, py in pts) + '"/>')
        for (x, y) in poly:
            out.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{r:.2f}"/>')
    return out


def extract(const):
    m = re.search(r'const char\* ' + const + r'\s*=\s*R"svg\((.*?)\)svg"',
                  SRC, re.S)
    if not m:
        raise SystemExit(f"{const} not found in icons.h")
    return m.group(1)


for verb, const in MAPPING.items():
    shapes = outline_shapes(icon_polylines(extract(const)), STROKE_W)
    svg = ('<?xml version="1.0" encoding="UTF-8"?>\n'
           '<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" '
           'viewBox="0 0 24 24">\n<g fill="#2e3436" stroke="none">\n  ' +
           "\n  ".join(shapes) + "\n</g>\n</svg>\n")
    (OUT / f"repomancer-{verb}-symbolic.svg").write_text(svg)
    print(f"wrote repomancer-{verb}-symbolic.svg  ({const}, {len(shapes)} shapes)")
