#!/usr/bin/env python3
"""Generate the V2 front-LED position table from assets/led-map.svg.

The SVG is the authoritative record of where the LEDs physically sit. This script
flattens its nested group transforms, picks out the red LED dies, maps each die to
its slot on the WS2812 chain and prints the table used by
src/Display/LedDisplay/LedStartupAnimation.h.

Coordinates are raw SVG map units with the origin at the e-paper centre, +x right
and +y down (~16 units per mm). Slots with no front die - the two back indicators
and the dead slot 2 of each module - are emitted as SKIP.

Usage:  python helpers/led_positions.py [--check]

  --check   recompute and compare against the table currently in the header,
            exiting non-zero if they differ.
"""

import math
import os
import re
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SVG = os.path.join(ROOT, "assets", "led-map.svg")
HEADER = os.path.join(ROOT, "src", "Display", "LedDisplay", "LedStartupAnimation.h")

NS = "{http://www.w3.org/2000/svg}"
DIE_FILL = "rgb(209,36,36)"      # the LED dies
PANEL_FILL = "rgb(58,87,113)"    # the e-paper

# Chain layout of one 9-segment module, in module-local SVG units.
# The chain is a serpentine: up the right column, across the top right-to-left,
# down the left column, across the bottom left-to-right, then mid-right, then centre.
# Local slot 2 is a chain position at (594, 589) with no die fitted - hence dead.
# Five slots drive two dies wired in parallel; those use the centroid of the pair.
LOCAL_SLOTS = {
    0: [(594, 983)],                  # bottom-right, lower
    1: [(594, 786)],                  # bottom-right, upper
    3: [(594, 392)],                  # top-right, lower
    4: [(594, 195)],                  # top-right, upper
    5: [(411, 0)],                    # top, right
    6: [(214, 0)],                    # top, middle
    7: [(17, 0)],                     # top, left
    8: [(0, 392), (107, 392)],        # upper-left  (pair)
    9: [(0, 589), (107, 589)],        # mid-left    (pair)
    10: [(0, 786), (107, 786)],       # bottom-left (pair)
    11: [(17, 1179)],                 # bottom, left
    12: [(214, 1179)],                # bottom, middle
    13: [(411, 1179)],                # bottom, right
    14: [(488, 539), (488, 642)],     # mid-right   (pair)
    15: [(291, 539), (291, 642)],     # centre      (pair)
}
DEAD_LOCAL_SLOT = 2

# Board.h (BOARD_REV == 2)
LED_COUNT = 74
DIGIT_BASE = 10
PIXELS_PER_MODULE = 16
MODULE_COUNT = 4

SKIP = -32768


def mat_mul(a, b):
    a0, a1, a2, a3, a4, a5 = a
    b0, b1, b2, b3, b4, b5 = b
    return (a0 * b0 + a2 * b1, a1 * b0 + a3 * b1,
            a0 * b2 + a2 * b3, a1 * b2 + a3 * b3,
            a0 * b4 + a2 * b5 + a4, a1 * b4 + a3 * b5 + a5)


def parse_transform(text):
    m = (1, 0, 0, 1, 0, 0)
    if not text:
        return m
    for name, args in re.findall(r"(\w+)\s*\(([^)]*)\)", text):
        v = [float(x) for x in re.split(r"[,\s]+", args.strip()) if x]
        if name == "matrix":
            cur = tuple(v)
        elif name == "translate":
            cur = (1, 0, 0, 1, v[0], v[1] if len(v) > 1 else 0.0)
        elif name == "scale":
            sx = v[0]
            cur = (sx, 0, 0, v[1] if len(v) > 1 else sx, 0, 0)
        else:
            continue
        m = mat_mul(m, cur)
    return m


def fill_of(el):
    style = el.get("style") or ""
    found = re.search(r"fill:\s*([^;]+)", style)
    return found.group(1).strip() if found else (el.get("fill") or "").strip()


def collect_rects(path):
    rects = []

    def walk(el, m):
        m = mat_mul(m, parse_transform(el.get("transform")))
        if el.tag == NS + "rect":
            x = float(el.get("x", 0))
            y = float(el.get("y", 0))
            w = float(el.get("width", 0))
            h = float(el.get("height", 0))
            cx, cy = x + w / 2.0, y + h / 2.0
            rects.append((m[0] * cx + m[2] * cy + m[4],
                          m[1] * cx + m[3] * cy + m[5],
                          fill_of(el)))
        for child in el:
            walk(child, m)

    walk(ET.parse(path).getroot(), (1, 0, 0, 1, 0, 0))
    return rects


def dedup(points, tol=5.0):
    """The artwork stacks a duplicate rect on one die per module."""
    out = []
    for p in points:
        if not any(abs(p[0] - q[0]) < tol and abs(p[1] - q[1]) < tol for q in out):
            out.append(p)
    return out


def module_offset(digit_index):
    """digit_index 0 = A = leftmost. Modules are wired right-to-left."""
    return DIGIT_BASE + (MODULE_COUNT - 1 - digit_index) * PIXELS_PER_MODULE


def build():
    rects = collect_rects(SVG)
    dies = [(x, y) for x, y, f in rects if f == DIE_FILL]
    panels = [(x, y) for x, y, f in rects if f == PANEL_FILL]
    if len(panels) != 1:
        sys.exit("expected exactly one e-paper rect, found %d" % len(panels))
    ox, oy = panels[0]

    # Split into the four digit modules and the two border columns by x.
    xs = sorted(d[0] for d in dies)
    groups, cur = [], [xs[0]]
    for v in xs[1:]:
        if v - cur[-1] > 200:
            groups.append(cur)
            cur = [v]
        else:
            cur.append(v)
    groups.append(cur)
    if len(groups) != 6:
        sys.exit("expected 4 modules + 2 border columns, found %d clusters" % len(groups))

    bounds = [(min(g) - 1, max(g) + 1) for g in groups]
    buckets = [[] for _ in groups]
    for d in dies:
        for i, (lo, hi) in enumerate(bounds):
            if lo <= d[0] <= hi:
                buckets[i].append(d)
                break

    modules = [dedup(buckets[0]), dedup(buckets[1]), dedup(buckets[4]), dedup(buckets[5])]
    left_border, right_border = buckets[2], buckets[3]

    pos = {}

    for digit_index, pts in enumerate(modules):
        if len(pts) != 20:
            sys.exit("module %d has %d dies, expected 20" % (digit_index, len(pts)))
        mx = min(p[0] for p in pts)
        my = min(p[1] for p in pts)
        base = module_offset(digit_index)
        claimed = []
        for local, dice in LOCAL_SLOTS.items():
            cx = sum(mx + dx for dx, _ in dice) / len(dice)
            cy = sum(my + dy for _, dy in dice) / len(dice)
            pos[base + local] = (cx - ox, cy - oy)
            claimed.extend((mx + dx, my + dy) for dx, dy in dice)
        # Every die must be claimed by exactly one slot.
        for p in pts:
            hits = sum(1 for c in claimed
                       if abs(c[0] - p[0]) < 2 and abs(c[1] - p[1]) < 2)
            if hits != 1:
                sys.exit("module %d: die %s claimed %d times" % (digit_index, p, hits))

    # Border columns, both running bottom -> top (LedCentralScreenBorder.h).
    for slots, column in ((( 0, 1, 2, 3), left_border), ((5, 6, 7, 8), right_border)):
        if len(column) != 4:
            sys.exit("border column has %d dies, expected 4" % len(column))
        for slot, p in zip(slots, sorted(column, key=lambda q: -q[1])):
            pos[slot] = (p[0] - ox, p[1] - oy)

    return pos


def labels():
    out = {
        0: "border left,  bottom outer", 1: "border left,  bottom inner",
        2: "border left,  top inner", 3: "border left,  top outer",
        4: "back indicator B",
        5: "border right, bottom outer", 6: "border right, bottom inner",
        7: "border right, top inner", 8: "border right, top outer",
        9: "back indicator A",
    }
    for digit_index, name in enumerate("ABCD"):
        out[module_offset(digit_index)] = "digit %s" % name
        out[module_offset(digit_index) + DEAD_LOCAL_SLOT] = "dead slot 2"
    return out


def render_table(pos):
    note = labels()
    lines = []
    for slot in range(LED_COUNT):
        text = note.get(slot, "")
        if slot in pos:
            lines.append("        {%6d, %5d},  // %2d  %s"
                         % (round(pos[slot][0]), round(pos[slot][1]), slot, text))
        else:
            lines.append("        {  SKIP,     0},  // %2d  %s" % (slot, text))
    return [line.rstrip() for line in lines]


def main():
    pos = build()
    radii = {s: math.hypot(*p) for s, p in pos.items()}
    farthest = max(radii, key=radii.get)
    nearest = min(radii, key=radii.get)
    table = render_table(pos)

    if "--check" in sys.argv:
        with open(HEADER, "r", encoding="utf-8") as fh:
            header = fh.read()
        missing = [line for line in table if line not in header]
        if missing:
            print("MISMATCH: %d table rows are not in %s" % (len(missing), HEADER))
            for line in missing[:10]:
                print("  " + line.strip())
            return 1
        print("OK: all %d rows match %s" % (len(table), os.path.basename(HEADER)))
        return 0

    print("// Generated by helpers/led_positions.py from assets/led-map.svg - do not hand-edit.")
    print("// Map units, origin = e-paper centre, +x right, +y down (~16 units/mm).")
    print("// MAX_RADIUS = %d (slot %d); nearest lit slot = %d (slot %d)."
          % (round(radii[farthest]), farthest, round(radii[nearest]), nearest))
    print("\n".join(table))
    return 0


if __name__ == "__main__":
    sys.exit(main())
