#!/usr/bin/env python3
"""Generate the V1 and V2 front-LED position tables from assets/*.svg.

The SVGs are the authoritative record of where the LEDs physically sit. This
script flattens nested group transforms, picks out the red LED dies, maps each
die to its slot on the WS2812 chain and prints the table used by
src/Display/LedDisplay/Animation/LedSlotPositions.h.

Coordinates are raw SVG map units (V2: origin at the e-paper centre; V1: origin
at the colon midpoint), +x right, +y down. V1 and V2 share the same ~16
units/mm die pitch (197 units), so one `band()` constant in LedSweepAnimation.h
covers both. Slots with no front die are emitted as SKIP: V2's two back
indicators (4, 9) and the dead slot 2 of each module (12, 28, 44, 60); V1's two
back indicators (2, 3) - V1 has no dead chain positions, every other slot is a
real front die.

V1 die -> index mapping: the physical dies carry no index markings, only their
(x, y) and which of the 7 seven-segment positions they occupy (found by
bounding box within each digit). Which of the 3 dies in a segment gets which of
its 3 known pixel indices (SevenSegmentProfile.h) is the one remaining
unknown, resolved by finding the assignment that minimises total physical wire
length across the ascending-index chain (an LED strip's buffer index is its
physical chain position, so consecutive indices should be physically close).
The colon is the one exception: its 2 dies are equidistant from the origin, so
index 0 vs 1 is immaterial and fixed by convention (0 = the upper die).

Usage:  python helpers/led_positions.py [v1|v2] [--check] [--preview out.png]

  v1|v2     which board to generate/check. Omit for both.
  --check   recompute and compare against the table currently in the header,
            exiting non-zero if they differ.
  --preview draw the V1 dies labelled with their resolved index (Pillow), for
            eyeball review. Not committed.
"""

import math
import os
import re
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SVG_V2 = os.path.join(ROOT, "assets", "led-map.svg")
SVG_V1 = os.path.join(ROOT, "assets", "v1_led_map.svg")
PROFILE_V1 = os.path.join(ROOT, "src", "Display", "LedDisplay", "Profiles", "SevenSegmentProfile.h")
HEADER = os.path.join(ROOT, "src", "Display", "LedDisplay", "Animation", "LedSlotPositions.h")

NS = "{http://www.w3.org/2000/svg}"
DIE_FILL = "rgb(209,36,36)"      # the LED dies
PANEL_FILL = "rgb(58,87,113)"    # the e-paper (V2 only)

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


def build_v2():
    rects = collect_rects(SVG_V2)
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


def labels_v2():
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


def render_table(pos, note, led_count):
    lines = []
    for slot in range(led_count):
        text = note.get(slot, "")
        if slot in pos:
            lines.append("        {%6d, %5d},  // %2d  %s"
                         % (round(pos[slot][0]), round(pos[slot][1]), slot, text))
        else:
            lines.append("        {  SKIP,     0},  // %2d  %s" % (slot, text))
    return [line.rstrip() for line in lines]


# ---------------------------------------------------------------------- V1 ---
# Board.h (BOARD_REV == 1)
LED_COUNT_V1 = 112
BAR_FIRST_PIXEL = 88
BAR_PIXEL_COUNT = 24
BIT_NAMES = ["bottom", "lower-left", "lower-right", "middle", "upper-left", "upper-right", "top"]
DIE_PITCH = 197.0   # shared with V2; see LedSweepAnimation.h band()


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def parse_seven_segment_profile():
    """Regex over SevenSegmentProfile.h: {name -> [7 pixel-index triples in bit order]}."""
    text = open(PROFILE_V1, encoding="utf-8").read()
    seg_re = re.compile(r"glyph([A-D])\[7\]\s*=\s*\{(.*?)\};", re.S)
    out = {}
    for m in seg_re.finditer(text):
        triples = re.findall(r"\{3,\s*\{(\d+),\s*(\d+),\s*(\d+)\}\}", m.group(2))
        if len(triples) != 7:
            sys.exit("SevenSegmentProfile.h: glyph%s has %d segments, expected 7" % (m.group(1), len(triples)))
        out[m.group(1)] = [tuple(int(x) for x in t) for t in triples]
    if sorted(out) != list("ABCD"):
        sys.exit("SevenSegmentProfile.h: expected glyphs A-D, found %s" % sorted(out))
    return out


def classify_v1_digit(pts):
    """21 dies of one digit -> the 7 physical seven-segment groups, each ordered
    canonically (horizontal bars left->right, verticals top->bottom)."""
    ys = sorted(set(round(y, 1) for _, y in pts))
    if len(ys) != 9:
        sys.exit("v1 digit: expected 9 distinct die rows, found %d" % len(ys))
    top_y, upper_ys, mid_y, lower_ys, bottom_y = ys[0], ys[1:4], ys[4], ys[5:8], ys[8]

    def near(yset, tol=5):
        return [p for p in pts if any(abs(p[1] - y) < tol for y in yset)]

    top = sorted(near([top_y]), key=lambda p: p[0])
    mid = sorted(near([mid_y]), key=lambda p: p[0])
    bottom = sorted(near([bottom_y]), key=lambda p: p[0])
    upper = near(upper_ys)
    lower = near(lower_ys)
    if len(top) != 3 or len(mid) != 3 or len(bottom) != 3 or len(upper) != 6 or len(lower) != 6:
        sys.exit("v1 digit: unexpected row sizes (top=%d mid=%d bottom=%d upper=%d lower=%d)"
                  % (len(top), len(mid), len(bottom), len(upper), len(lower)))

    ux = sorted(set(round(p[0], 1) for p in upper))
    lx = sorted(set(round(p[0], 1) for p in lower))
    upper_left = sorted([p for p in upper if abs(p[0] - ux[0]) < 5], key=lambda p: p[1])
    upper_right = sorted([p for p in upper if abs(p[0] - ux[1]) < 5], key=lambda p: p[1])
    lower_left = sorted([p for p in lower if abs(p[0] - lx[0]) < 5], key=lambda p: p[1])
    lower_right = sorted([p for p in lower if abs(p[0] - lx[1]) < 5], key=lambda p: p[1])
    return {
        "bottom": bottom, "lower-left": lower_left, "lower-right": lower_right,
        "middle": mid, "upper-left": upper_left, "upper-right": upper_right, "top": top,
    }


def solve_chain_flips(groups):
    """groups: ascending-index-ordered list of (min_idx, name, bit, idxset, canonical_pts[3]).
    Each triple may run forward (its canonical order) or reversed; choose per-triple to
    minimise the sum of physical distances between consecutive triples' facing ends - a
    real LED chain does not zigzag further than the geometry forces it to. Returns
    (flips, margins) where margins[i] is cost(worse flip) - cost(chosen flip) at stage i."""
    n = len(groups)
    INF = float("inf")
    dp = [[0.0, 0.0] for _ in range(n)]
    back = [[None, None] for _ in range(n)]
    for i in range(1, n):
        prev_pts = groups[i - 1][4]
        cur_pts = groups[i][4]
        for flip in (0, 1):
            entry = cur_pts[2] if flip else cur_pts[0]
            best, bestf = INF, None
            for pflip in (0, 1):
                pexit = prev_pts[0] if pflip else prev_pts[2]
                c = dp[i - 1][pflip] + dist(pexit, entry)
                if c < best:
                    best, bestf = c, pflip
            dp[i][flip] = best
            back[i][flip] = bestf
    endflip = 0 if dp[n - 1][0] <= dp[n - 1][1] else 1
    flips = [0] * n
    flips[n - 1] = endflip
    for i in range(n - 1, 0, -1):
        flips[i - 1] = back[i][flips[i]]
    margin = abs(dp[n - 1][0] - dp[n - 1][1])
    return flips, margin


def build_v1():
    rects = collect_rects(SVG_V1)
    dies = [(x, y) for x, y, f in rects if f == DIE_FILL]
    if len(dies) != 110:
        sys.exit("v1_led_map.svg: expected 110 dies (84 digit + 2 colon + 24 bar), found %d" % len(dies))

    bar = sorted([d for d in dies if abs(d[1] - 1725) < 5], key=lambda p: p[0])
    rest = [d for d in dies if abs(d[1] - 1725) >= 5]
    colon = sorted([d for d in rest if abs(d[1] - 507) < 5 or abs(d[1] - 900) < 5], key=lambda p: p[1])
    digit_dies = [d for d in rest if d not in colon]

    if len(bar) != BAR_PIXEL_COUNT:
        sys.exit("v1 bar: expected %d dies, found %d" % (BAR_PIXEL_COUNT, len(bar)))
    if len(colon) != 2:
        sys.exit("v1 colon: expected 2 dies, found %d" % len(colon))

    clusters = []
    for d in sorted(digit_dies, key=lambda p: p[0]):
        if not clusters or d[0] - clusters[-1][-1][0] > 200:
            clusters.append([d])
        else:
            clusters[-1].append(d)
    if len(clusters) != 4 or any(len(c) != 21 for c in clusters):
        sys.exit("v1 digits: expected 4 clusters of 21 dies, found %s" % [len(c) for c in clusters])
    digits = dict(zip("ABCD", clusters))
    digit_groups = {name: classify_v1_digit(pts) for name, pts in digits.items()}

    profile = parse_seven_segment_profile()
    all_groups = []
    for name in "ABCD":
        for bit, idxs in zip(BIT_NAMES, profile[name]):
            all_groups.append((min(idxs), name, bit, set(idxs), digit_groups[name][bit]))
    all_groups.sort(key=lambda g: g[0])

    # Two independent physical runs: the low half (C, D - interleaved) and the high
    # half (A, B - interleaved), split at the point their index ranges are disjoint.
    cd_groups = [g for g in all_groups if g[0] < 46]
    ab_groups = [g for g in all_groups if g[0] >= 46]
    cd_flips, cd_margin = solve_chain_flips(cd_groups)
    ab_flips, ab_margin = solve_chain_flips(ab_groups)
    for label, margin in (("C/D", cd_margin), ("A/B", ab_margin)):
        # Fatal, not a warning: a coin-flip orientation would be committed as ground
        # truth, and --check can only ever confirm it afterwards.
        if margin < DIE_PITCH:
            sys.exit("%s chain flip margin %.0f is under one die pitch (%.0f) - orientation is ambiguous"
                     % (label, margin, DIE_PITCH))

    pos = {}
    # Colon: both dies equidistant from the origin (their own midpoint), so which
    # physical die is index 0 vs 1 is immaterial - pin 0 = the upper die by convention.
    pos[0], pos[1] = colon[0], colon[1]

    def assign(groups, flips):
        for (min_idx, name, bit, idxset, pts), flip in zip(groups, flips):
            order = sorted(idxset) if flip == 0 else sorted(idxset, reverse=True)
            seq = pts if flip == 0 else list(reversed(pts))
            for idx, p in zip(order, seq):
                pos[idx] = p

    assign(cd_groups, cd_flips)
    assign(ab_groups, ab_flips)

    # Bar: a single strip below all 4 digits. Index 88 lands at the left end, next to
    # digit A's bottom-left die (87) - the chain hands off there, not at the right.
    bar_sorted = sorted(bar, key=lambda p: p[0])
    for i, p in enumerate(bar_sorted):
        pos[BAR_FIRST_PIXEL + i] = p

    if sorted(pos) != [i for i in range(LED_COUNT_V1) if i not in (2, 3)]:
        sys.exit("v1: index assignment incomplete or overlapping")

    ox, oy = pos[0][0], (pos[0][1] + pos[1][1]) / 2.0   # colon midpoint; both dies share x
    return {i: (x - ox, y - oy) for i, (x, y) in pos.items()}


def labels_v1():
    out = {0: "colon", 1: "colon", 2: "back indicator B", 3: "back indicator A"}
    for i in range(BAR_PIXEL_COUNT):
        out[BAR_FIRST_PIXEL + i] = "bar %d" % i
    profile = parse_seven_segment_profile()
    for name in "ABCD":
        for bit, idxs in zip(BIT_NAMES, profile[name]):
            for idx in idxs:
                out[idx] = "digit %s %s" % (name, bit)
    return out


def preview_v1(pos, out_path):
    from PIL import Image, ImageDraw

    xs = [p[0] for p in pos.values()]
    ys = [p[1] for p in pos.values()]
    margin = 60
    w = int(max(xs) - min(xs)) + 2 * margin
    h = int(max(ys) - min(ys)) + 2 * margin
    ox, oy = min(xs) - margin, min(ys) - margin
    img = Image.new("RGB", (w, h), "white")
    draw = ImageDraw.Draw(img)
    for i, (x, y) in pos.items():
        px, py = x - ox, y - oy
        draw.ellipse((px - 10, py - 10, px + 10, py + 10), outline="red", width=2)
        draw.text((px + 12, py - 6), str(i), fill="black")
    img.save(out_path)
    print("preview written: %s" % out_path)


# ---------------------------------------------------------------------- CLI ---

def header_rows(header, board):
    """The table rows of one board's branch, in array order."""
    lines = header.splitlines()
    marks = {}
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped == "#if BOARD_REV == 2":
            marks["if"] = i
        elif stripped == "#else" and "if" in marks and "else" not in marks:
            marks["else"] = i
        elif stripped.startswith("#endif") and "else" in marks and "endif" not in marks:
            marks["endif"] = i
    if len(marks) != 3:
        sys.exit("%s: cannot find the BOARD_REV #if/#else/#endif around the tables" % os.path.basename(HEADER))
    lo, hi = (marks["if"], marks["else"]) if board == "v2" else (marks["else"], marks["endif"])
    return [line.strip() for line in lines[lo + 1:hi] if line.lstrip().startswith("{")]


def check(board, table, led_count):
    with open(HEADER, "r", encoding="utf-8") as fh:
        header = fh.read()
    # Positional, within this board's own branch: POS[] is filled by initializer
    # order, so a transposed row or one pasted under the other board would still be
    # "somewhere in the file" while putting a slot at the wrong place.
    rows = header_rows(header, board)
    bad = [i for i in range(max(len(rows), len(table)))
           if i >= len(rows) or i >= len(table) or rows[i] != table[i].strip()]
    if bad:
        print("MISMATCH (%s): %d of %d rows differ from %s (header has %d rows)"
              % (board, len(bad), led_count, os.path.basename(HEADER), len(rows)))
        for i in bad[:10]:
            print("  slot %d: header %r" % (i, rows[i].strip() if i < len(rows) else None))
            print("  slot %d: wanted %r" % (i, table[i].strip() if i < len(table) else None))
        return False
    print("OK (%s): all %d rows match %s, in order" % (board, len(table), os.path.basename(HEADER)))
    return True


def main():
    args = sys.argv[1:]
    do_check = "--check" in args
    preview_path = None
    if "--preview" in args:
        preview_path = args[args.index("--preview") + 1]
    boards = [a for a in args if a in ("v1", "v2")]
    if not boards:
        boards = ["v1", "v2"]

    ok = True

    if "v2" in boards:
        pos2 = build_v2()
        radii2 = {s: math.hypot(*p) for s, p in pos2.items()}
        table2 = render_table(pos2, labels_v2(), LED_COUNT)
        if do_check:
            ok = check("v2", table2, LED_COUNT) and ok
        else:
            farthest = max(radii2, key=radii2.get)
            nearest = min(radii2, key=radii2.get)
            print("// V2 - generated by helpers/led_positions.py from assets/led-map.svg - do not hand-edit.")
            print("// Map units, origin = e-paper centre, +x right, +y down (~16 units/mm).")
            print("// MAX_RADIUS = %d (slot %d); nearest lit slot = %d (slot %d)."
                  % (round(radii2[farthest]), farthest, round(radii2[nearest]), nearest))
            print("\n".join(table2))

    if "v1" in boards:
        pos1 = build_v1()
        radii1 = {s: math.hypot(*p) for s, p in pos1.items()}
        table1 = render_table(pos1, labels_v1(), LED_COUNT_V1)
        if do_check:
            ok = check("v1", table1, LED_COUNT_V1) and ok
        else:
            farthest = max(radii1, key=radii1.get)
            nearest = min(radii1, key=radii1.get)
            print("// V1 - generated by helpers/led_positions.py from assets/v1_led_map.svg - do not hand-edit.")
            print("// Map units, origin = colon midpoint, +x right, +y down (~16 units/mm).")
            print("// MAX_RADIUS = %d (slot %d); nearest lit slot = %d (slot %d)."
                  % (round(radii1[farthest]), farthest, round(radii1[nearest]), nearest))
            print("\n".join(table1))
        if preview_path:
            preview_v1(pos1, preview_path)

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
