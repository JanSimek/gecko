#!/usr/bin/env python3
"""
map_trim.py - trim a map to a clean edge: floor cut straight along the tile
grid, a tidy exit band just inside it, and a camera ring.

    python tools/map_trim.py Clairemont.map --preview          # measure + render only
    python tools/map_trim.py Clairemont.map --box 0,18,90,90    # force the box (squares x0,y0,x1,y1)
    python tools/map_trim.py Clairemont.map --write            # write both copies (backups kept)
    python tools/map_trim.py Clairemont.map --fill --write     # straighten by ADDING ground, cut nothing

--fill (owner, 2026-10-03: the cut version lost too much) keeps every square
and object: the box is the floor's own extent and the floor is filled out to
it. A road that reaches the old edge runs on along its own line to the new
edge (its last few squares repeated); the rest of the new ground copies the
map's ground near the nearest old edge, picked at random so it doesn't
streak. Exits and the camera limit are then rebuilt at the new edge as usual.

WHY
    Floor squares are drawn as isometric tiles, so an edge only reads as a
    straight line when it runs along one of the grid's two axes. A floor
    painted as a rectangle on screen (how gecko paints, and how every vanilla
    town is laid out) ends in a sawtooth. Vanilla hides it behind a
    scroll-blocker ring; the owner prefers the edge showing as a clean black
    cut (the drive-in, 2026-09-30). So the floor is trimmed to a box in
    square coordinates: its four edges are straight diagonal lines.

WHAT IT DOES (elevation 0)
    box      squares x0..x1, y0..y1 (inclusive). Default: the extent of the
             map's content (objects other than weeds, grass, blockers, exit
             grids) plus MARGIN squares, clamped to the grid.
    floor    squares outside the box are voided; void squares inside it are
             filled with the map's own commonest ground tiles.
    roof     squares outside the box are voided.
    objects  objects outside the box are dropped (reported by kind; a
             scripted object or a critter stops the run unless --force).
             Existing exit grids and scroll blockers are dropped.
    exits    a band BAND hexes deep, INSET hexes in from the box edge, on
             all four sides. Each old exit destination other than the map's
             commonest one keeps its stretch: the band hexes on the side
             nearest to where those exits were, within their span along it
             (the road into the next map). Everything else gets the
             commonest destination (usually the world map).
    camera   two scroll-blocker loops CAMERA hexes inside the exit band, so
             the view stops with the black edge in sight but the player can
             still walk onto every exit.

Checks before writing: the start hex and every arrival hex the map is known
to receive are inside the box and inside the exit band; walking from the
start can reach an exit on every side; the two map copies are identical (no
unsynced gecko save). Writes mod/data/maps/<name> and base/mods/foc/maps/<name>,
each after a <name>.<timestamp>.bak copy.
"""

from __future__ import annotations

import argparse
import math
import random
import re
import shutil
import struct
import sys
import time
from collections import Counter, deque
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import render  # noqa: E402
from foclib.paths import bare_name, inside  # noqa: E402
from foclib import GameIndex, parse_lst  # noqa: E402
from foclib.formats import MapFile, hex_xy, is_exit_grid, pid_type  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
AUTHORED = ROOT / "mod" / "data" / "maps"
DEPLOYED = ROOT / "base" / "mods" / "foc" / "maps"
MARGIN = 3          # squares of ground round the content
INSET = 2           # hexes between the box edge and the exit band
BAND = 2            # exit band depth, hexes
REACH = 16          # (unused)
# exit patches, measured on 163 vanilla and TC town maps (tools/exit_survey.py):
# a patch about 25 hexes, ~310 px long, ~110 px deep, ~85 px outside the
# camera limit; 1-4 patches a map, where a road or path leaves
PATCH_LEN = 312     # px along the edge
STRIP = 2           # hexes deep
ROAD_TILES = re.compile(r"^(road|street|strt|asph|hwy)", re.I)    # road surface, not curbs or pavement
ROAD_REACH = 16     # hexes in from the exit strip a street may stop and still count as leaving
ROAD_MIN = 30       # road hexes near the edge for a street to count (smaller is a stub or a verge)
ROAD_AXIS_R = 480   # px along a candidate line counted when finding a street's direction
ROAD_HALF_W = 48    # px either side of that line
# the directions a street runs on screen: across, and along each floor axis
# (a floor square steps 48 px left/12 down one way and 32 right/24 down the other)
ROAD_AXES = [(1.0, 0.0), (48 / math.hypot(48, 12), -12 / math.hypot(48, 12)),
             (32 / math.hypot(32, 24), 24 / math.hypot(32, 24))]
ROAD_RAY = 900      # px a street's line is followed to the edge
ROAD_TOUCH = 1      # hexes short of the strip a street may stop and still count as running into the edge
CAM_INSET = 64      # px the camera rectangle sits inside the content
CELL = 32           # px grid for finding the largest on-floor screen rectangle
FLOOR_MARGIN = 6    # hexes of floor kept between that rectangle and the void (room for the exit strips)
# the exit-grid variant vanilla uses on each side of the screen
SIDE_PID = {"bottom": 0x05000016, "top": 0x05000017, "right": 0x05000015, "left": 0x05000014}
CAMERA = 10         # hexes from the exit band in to the first blocker loop
SCROLL_BLOCKER = 0x0500000C
EXIT_TEMPLATE_MAP = "maps\\city2.map"
NOTCH_MIN = 0.05    # a corner notch is cut when it is at least this share of the box
NOTCH_SIDE = 10     # and at least this many squares each way
SCATTER = re.compile(r"^s?(weed|grass|block|bush|cactus|cac[0-9])", re.I)
OBJ_ID_OFF, OBJ_TILE_OFF, OBJ_PID_OFF = 0x00, 0x04, 0x2C
START_TILE_OFF = 0x14   # map header: player start hex (formats.py MapFile.default_tile)


# ---------------------------------------------------------------- map bytes

def spans_e0(raw: bytes, m: MapFile, gi):
    first = m.objects[0].off
    total_off, e0_off = first - 8, first - 4
    n0 = struct.unpack_from(">i", raw, e0_off)[0]
    out, p = [], first
    for i in range(n0):
        _obj, end = MapFile._read_object(raw, p, gi.proto)
        out.append((m.objects[i], p, end))
        p = end
    return out, total_off, e0_off, p


def template_records(gi):
    """An exit-grid record and a scroll-blocker record from vanilla city2.map."""
    raw = gi.vfs.read(EXIT_TEMPLATE_MAP)
    m = MapFile.parse(raw, gi.proto)
    spans, *_ = spans_e0(raw, m, gi)
    ex = next(raw[s:e] for o, s, e in spans if is_exit_grid(o.pid))
    sb = next(raw[s:e] for o, s, e in spans if o.pid == SCROLL_BLOCKER)
    return ex, sb


# ------------------------------------------------------------------ geometry

def rect_loop(x0, y0, x1, y1):
    pts = [(x, y0) for x in range(x0, x1 + 1)] + [(x, y1) for x in range(x0, x1 + 1)]
    pts += [(x0, y) for y in range(y0 + 1, y1)] + [(x1, y) for y in range(y0 + 1, y1)]
    return pts


def neighbours(t: int):
    x, y = t % 200, t // 200
    d = [(-1, 0), (-1, 1), (0, 1), (1, 1), (1, 0), (0, -1)] if x % 2 == 0 else \
        [(-1, -1), (-1, 0), (0, 1), (1, 0), (1, -1), (0, -1)]
    for a, b in d:
        nx, ny = x + a, y + b
        if 0 <= nx < 200 and 0 <= ny < 200:
            yield ny * 200 + nx


def content_squares(m: MapFile, gi) -> set[tuple[int, int]]:
    art = {t: parse_lst(gi.vfs.read(f"art\\{d}\\{d}.lst"))
           for t, d in ((0, "items"), (1, "critters"), (2, "scenery"), (3, "walls"))}
    xs, ys = [], []
    for o in m.objects:
        if o.elevation != 0 or not 0 <= o.tile < 40000 or pid_type(o.pid) == 5:
            continue
        lst = art.get((o.fid >> 24) & 0xF, [])
        a = lst[o.fid & 0xFFF] if (o.fid & 0xFFF) < len(lst) else ""
        if SCATTER.match(a):
            continue
        x, y = hex_xy(o.tile)
        xs.append((x // 2, y // 2))
    return set(xs)


def content_box(sq: set) -> tuple[int, int, int, int]:
    return (max(0, min(x for x, _ in sq) - MARGIN), max(0, min(y for _, y in sq) - MARGIN),
            min(99, max(x for x, _ in sq) + MARGIN), min(99, max(y for _, y in sq) + MARGIN))


def shape(box, content: set, keep_hexes) -> set[tuple[int, int]]:
    """The squares to keep: the box, less an empty notch in each corner when
    the notch is big (NOTCH_MIN of the box, NOTCH_SIDE squares each way).
    A notch's edges run along the grid axes too, so every edge stays straight.
    Nothing within MARGIN squares of content, and no hex in keep_hexes, is cut."""
    x0, y0, x1, y1 = box
    grown = {(x + dx, y + dy) for x, y in content for dx in range(-MARGIN, MARGIN + 1)
             for dy in range(-MARGIN, MARGIN + 1)}
    grown |= {(hx // 2 + dx, hy // 2 + dy) for hx, hy in keep_hexes
              for dx in range(-MARGIN - 3, MARGIN + 4) for dy in range(-MARGIN - 3, MARGIN + 4)}
    keep = {(x, y) for x in range(x0, x1 + 1) for y in range(y0, y1 + 1)}
    area = len(keep)
    for sx, sy in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
        cx0 = x1 if sx > 0 else x0
        cy0 = y1 if sy > 0 else y0
        best, bestn = None, 0
        for w in range(NOTCH_SIDE, x1 - x0):
            for h in range(NOTCH_SIDE, y1 - y0):
                xs = range(cx0 - sx * (w - 1), cx0 + sx, sx) if sx > 0 else range(cx0, cx0 + w)
                ys = range(cy0 - sy * (h - 1), cy0 + sy, sy) if sy > 0 else range(cy0, cy0 + h)
                cells = {(x, y) for x in (range(cx0 - w + 1, cx0 + 1) if sx > 0 else range(cx0, cx0 + w))
                         for y in (range(cy0 - h + 1, cy0 + 1) if sy > 0 else range(cy0, cy0 + h))}
                if cells & grown:
                    break
                if len(cells) > bestn:
                    best, bestn = cells, len(cells)
        if best and bestn >= NOTCH_MIN * area:
            keep -= best
    return keep


# ------------------------------------------------------------------- trim

def floor_rect(dist: dict, margin: int) -> list[int]:
    """Largest axis-aligned screen rectangle whose every CELL px cell is
    floor at least `margin` hexes from the void (max rectangle in a 0/1
    grid, by histograms)."""
    pts = {}
    for t, d in dist.items():
        if d >= margin:
            x, y = render.hex_xy_screen(t)
            pts[(x // CELL, y // CELL)] = True
    xs = [k[0] for k in pts]; ys = [k[1] for k in pts]
    gx0, gy0 = min(xs), min(ys)
    W, H = max(xs) - gx0 + 1, max(ys) - gy0 + 1
    heights = [0] * W
    best, rect = 0, None
    for j in range(H):
        for i in range(W):
            heights[i] = heights[i] + 1 if (gx0 + i, gy0 + j) in pts else 0
        stack = []
        for i in range(W + 1):
            h = heights[i] if i < W else 0
            start = i
            while stack and stack[-1][1] >= h:
                st, sh = stack.pop()
                if sh * (i - st) > best:
                    best, rect = sh * (i - st), (st, j - sh + 1, i - 1, j)
                start = st
            stack.append((start, h))
    x0, y0, x1, y1 = rect
    return [(gx0 + x0) * CELL, (gy0 + y0) * CELL, (gx0 + x1 + 1) * CELL, (gy0 + y1 + 1) * CELL]


def hex_patches(pts):
    pts, seen, out = set(pts), set(), []
    for p0 in pts:
        if p0 in seen:
            continue
        stack, comp = [p0], []
        seen.add(p0)
        while stack:
            x, y = stack.pop()
            comp.append((x, y))
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    q = (x + dx, y + dy)
                    if q in pts and q not in seen:
                        seen.add(q)
                        stack.append(q)
        out.append(comp)
    return out


FILL_ROAD_RUN = 10    # squares of street behind the edge for it to count as running off the map
FILL_ROAD_WIDE = 14   # a street wider than this across is lying along the edge, not leaving it
# and it must run back at least twice as far as it is wide, and be 3 or more across (not a lone curb line)
FILL_PATTERN = 4      # street squares repeated when it is carried on
FILL_PICK = 4         # squares round the nearest old edge square that new ground copies
STREET_TILES = re.compile(r"^(road|street|strt|asph|hwy|curb|crbm)", re.I)
GROUND_TILES = re.compile(r"^edg", re.I)   # plain desert ground


def fill_floor(raw: bytes, m: MapFile, gi, carry_all: bool = False) -> tuple[bytes, tuple[int, int, int, int], dict]:
    """Fill elevation 0's floor out to its own extent (a box in squares, so
    every edge is straight). A street (road and curbs) that runs off the old
    edge carries on along its line; every other new square is plain ground
    (edg*) picked at random near the closest old edge square. carry_all:
    every non-ground square on the old edge (side streets, pavement) carries
    on too, so nothing paved stops short in the new sand (owner, Redwell)."""
    pairs = list(struct.unpack_from(">20000H", raw, m.tiles_offset))
    roofs, floors = pairs[0::2], pairs[1::2]
    old = {(i % 100, i // 100) for i in range(10000) if (floors[i] & 0xFFF) > 1}
    x0, x1 = min(x for x, _ in old), max(x for x, _ in old)
    y0, y1 = min(y for _, y in old), max(y for _, y in old)
    box = (x0, y0, x1, y1)
    inbox = lambda q: x0 <= q[0] <= x1 and y0 <= q[1] <= y1
    names = [n.lower() for n in parse_lst(gi.vfs.read("art/tiles/tiles.lst"))]
    name = lambda q: names[floors[q[1] * 100 + q[0]] & 0xFFF] if (floors[q[1] * 100 + q[0]] & 0xFFF) < len(names) else ""
    street = lambda q: q in old and bool(STREET_TILES.match(name(q)))
    ground = lambda q: q in old and bool(GROUND_TILES.match(name(q)))
    new = {}

    carried = 0
    for q in sorted(old):
        if not street(q):
            continue
        # only toward the side of the box this edge square faces (the nearest
        # side), so a street near a corner isn't run along the other side
        gaps = {(1, 0): x1 - q[0], (-1, 0): q[0] - x0, (0, 1): y1 - q[1], (0, -1): q[1] - y0}
        facing = min(gaps, key=gaps.get)
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            out = (q[0] + dx, q[1] + dy)
            if out in old or not inbox(out) or (dx, dy) != facing:
                continue
            run = 0
            while street((q[0] - run * dx, q[1] - run * dy)):
                run += 1
            if run < FILL_ROAD_RUN:
                continue
            # the street's width across the direction it leaves in
            px, py = dy, dx
            wide = 1
            for s in (1, -1):
                k = 1
                while street((q[0] + s * k * px, q[1] + s * k * py)):
                    k += 1
                wide += k - 1
            if wide < 3 or wide > FILL_ROAD_WIDE or run < 2 * wide:
                continue    # lying along the edge, or this is the street's width, not its length
            pattern = [floors[(q[1] - k * dy) * 100 + q[0] - k * dx] for k in range(FILL_PATTERN)]
            k, cur = 0, out
            while inbox(cur) and cur not in old:
                new.setdefault(cur, pattern[k % len(pattern)])
                k += 1
                cur = (cur[0] + dx, cur[1] + dy)
            carried += 1

    # carry_all: every stretch of pavement on the old edge is run out to the
    # new edge as one solid block (its teeth on the old jagged edge would
    # otherwise come out as a comb of stripes)
    if carry_all:
        sides = {}
        for q in old:
            if ground(q):
                continue
            gaps = {(1, 0): x1 - q[0], (-1, 0): q[0] - x0, (0, 1): y1 - q[1], (0, -1): q[1] - y0}
            d = min(gaps, key=gaps.get)
            if (q[0] + d[0], q[1] + d[1]) in old or not inbox((q[0] + d[0], q[1] + d[1])):
                continue
            lat = q[1] if d[0] else q[0]
            sides.setdefault(d, {}).setdefault(lat, []).append(q)
        for (dx, dy), by_lat in sides.items():
            lats = sorted(by_lat)
            runs, cur = [], [lats[0]]
            for v in lats[1:]:
                if v - cur[-1] <= 2:
                    cur.append(v)
                else:
                    runs.append(cur)
                    cur = [v]
            runs.append(cur)
            for run in runs:
                # one plain paving tile for the whole block: the commonest
                # paved, non-road tile just behind this stretch of edge
                # (copying each row's own squares staggers curb edges and
                # lane marks into stripes)
                behind = Counter()
                for v in run:
                    for q in by_lat[v]:
                        for k in range(FILL_PICK + 1):
                            b = (q[0] - k * dx, q[1] - k * dy)
                            if b in old and not ground(b) and not ROAD_TILES.match(name(b)):
                                behind[floors[b[1] * 100 + b[0]]] += 1
                if not behind:
                    for v in run:
                        for q in by_lat[v]:
                            behind[floors[q[1] * 100 + q[0]]] += 1
                paving = [behind.most_common(1)[0][0]]
                for lat in range(run[0], run[-1] + 1):
                    # a line that is road running outward stays road (its own
                    # last squares repeated); the rest of the block is paving
                    on_line = [sq for sq in old if (sq[1] if dx else sq[0]) == lat]
                    if not on_line:
                        continue
                    along_c = [(sq[0] if dx else sq[1]) for sq in on_line]
                    ec = max(along_c) if (dx + dy) > 0 else min(along_c)
                    edge_sq = (ec, lat) if dx else (lat, ec)
                    back = [(edge_sq[0] - k * dx, edge_sq[1] - k * dy) for k in range(FILL_PATTERN)]
                    if all(b in old and ROAD_TILES.match(name(b)) for b in back):
                        pattern = [floors[b[1] * 100 + b[0]] for b in back]
                    else:
                        pattern = paving
                    # start just past the old floor on this line
                    line = [(c, lat) if dx else (lat, c) for c in range(100)]
                    along = [(sq[0] if dx else sq[1]) for sq in line if sq in old]
                    if not along:
                        continue
                    edge = max(along) if (dx + dy) > 0 else min(along)
                    k, c = 0, edge + (dx + dy)
                    while True:
                        sq = (c, lat) if dx else (lat, c)
                        if not inbox(sq):
                            break
                        if sq not in old:
                            new.setdefault(sq, pattern[k % len(pattern)])
                            k += 1
                        c += dx + dy
                carried += 1

    # plain ground: breadth first from the old edge
    rng = random.Random(1003)
    pool = [floors[q[1] * 100 + q[0]] for q in old if ground(q)] or \
        [floors[q[1] * 100 + q[0]] for q in old]
    src = {}
    qd = deque()
    for q in old:
        if any((q[0] + dx, q[1] + dy) not in old and inbox((q[0] + dx, q[1] + dy))
               for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
            src[q] = q
            qd.append(q)
    while qd:
        q = qd.popleft()
        for n in ((q[0] + 1, q[1]), (q[0] - 1, q[1]), (q[0], q[1] + 1), (q[0], q[1] - 1)):
            if inbox(n) and n not in old and n not in src:
                src[n] = src[q]
                qd.append(n)
    filled = 0
    for n, b in src.items():
        if n in old or n in new:
            continue
        near = [(b[0] + dx, b[1] + dy) for dx in range(-FILL_PICK, FILL_PICK + 1)
                for dy in range(-FILL_PICK, FILL_PICK + 1)]
        near = [c for c in near if ground(c)]
        if near:
            pick = rng.choice(near)
            new[n] = floors[pick[1] * 100 + pick[0]]
        else:
            new[n] = rng.choice(pool)
        filled += 1
    for (x, y), v in new.items():
        floors[y * 100 + x] = v
    out = bytearray(raw)
    out[m.tiles_offset:m.tiles_offset + 40000] = struct.pack(
        ">20000H", *[v for pair in zip(roofs, floors) for v in pair])
    return bytes(out), box, dict(added=len(new), street_squares=len(new) - filled, streets_carried=carried)


def trim(raw: bytes, m: MapFile, gi, keep_sq: set, content: set, arrivals, force: bool, explicit=()):
    spans, total_off, e0_off, e0_end = spans_e0(raw, m, gi)

    def in_keep(t):
        return ((t % 200) // 2, (t // 200) // 2) in keep_sq

    # --- hex distance from the void (steps over the engine's own neighbours)
    dist = {}
    q = deque()
    for t in range(40000):
        if in_keep(t) and any(not in_keep(n) for n in neighbours(t)):
            dist[t] = 0
            q.append(t)
        elif in_keep(t) and ((t % 200) in (0, 199) or (t // 200) in (0, 199)):
            dist[t] = 0
            q.append(t)
    while q:
        t = q.popleft()
        for n in neighbours(t):
            if in_keep(n) and n not in dist:
                dist[n] = dist[t] + 1
                q.append(n)

    # --- exit patches, the way vanilla and the TCs place them: a few short
    # strips, each ~PATCH_LEN px along the edge and STRIP hexes deep, where a
    # road leaves (toward the next map) or mid-edge (to the world map)
    strip = {t for t, dd in dist.items() if INSET <= dd < INSET + STRIP}
    scr = {t: render.hex_xy_screen(t) for t in dist}
    old = {}
    for o, s_, e in spans:
        if is_exit_grid(o.pid):
            old[o.tile] = struct.unpack_from(">4i", raw, e - 16)
    by_dest = {}
    for t, d in old.items():
        by_dest.setdefault(d, []).append(t)
    fx = [scr[t][0] for t in strip]; fy = [scr[t][1] for t in strip]
    mid_x, mid_y = (min(fx) + max(fx)) / 2, (min(fy) + max(fy)) / 2
    anchors = {"top": (mid_x, min(fy)), "bottom": (mid_x, max(fy)),
               "left": (min(fx), mid_y), "right": (max(fx), mid_y)}
    wanted = []            # (label, anchor screen point, dest)
    traced = []            # (from, to) screen points: streets followed to the edge
    tile_names = [n.lower() for n in parse_lst(gi.vfs.read("art\\tiles\\tiles.lst"))]
    floors0 = m.floors[0]

    def on_road(t):
        v = floors0[((t // 200) // 2) * 100 + (t % 200) // 2] & 0xFFF
        return v < len(tile_names) and ROAD_TILES.match(tile_names[v])
    for spec in explicit:
        parts = spec.split(":")
        where, mid = parts[0], int(parts[1])
        dest = next((dd for dd in by_dest if dd[0] == mid), (mid, -1, 0, 0))
        if where == "roads":
            # a strip wherever a street runs off the map: each street near
            # the edge (hexes connected on road), followed along its own
            # direction from its outer end until it meets the strip
            road = {t for t in dist if on_road(t)}
            near = {t for t in road if dist[t] < INSET + STRIP + ROAD_REACH}
            seen = set()
            for t0 in sorted(near):
                if t0 in seen:
                    continue
                comp, stack = [], [t0]
                seen.add(t0)
                while stack:
                    t = stack.pop()
                    comp.append(t)
                    for n in neighbours(t):
                        if n in near and n not in seen:
                            seen.add(n)
                            stack.append(n)
                if len(comp) < ROAD_MIN:
                    continue
                # the street's outer end: its road hexes nearest the void
                dmin = min(dist[t] for t in comp)
                face = [t for t in comp if dist[t] <= dmin + 2]
                fx0 = sum(scr[t][0] for t in face) / len(face)
                fy0 = sum(scr[t][1] for t in face) / len(face)
                if dmin <= INSET + STRIP + ROAD_TOUCH:
                    # it runs into the edge: the strip goes across its end
                    pt = scr[min(strip, key=lambda t: (scr[t][0] - fx0) ** 2 + (scr[t][1] - fy0) ** 2)]
                else:
                    # it stops short: follow its line on to the edge. Its
                    # direction is whichever of the ways a street can run
                    # (across the screen, or along either floor axis) puts
                    # the most road on a line through it
                    cx = sum(scr[t][0] for t in comp) / len(comp)
                    cy = sum(scr[t][1] for t in comp) / len(comp)
                    best = None
                    for ux, uy in ROAD_AXES:
                        n = sum(1 for t in road
                                if abs((scr[t][0] - cx) * uy - (scr[t][1] - cy) * ux) <= ROAD_HALF_W
                                and abs((scr[t][0] - cx) * ux + (scr[t][1] - cy) * uy) <= ROAD_AXIS_R)
                        if best is None or n > best[0]:
                            best = (n, ux, uy)
                    _n, ux, uy = best
                    # start from the street's centre line, not the middle
                    # of whatever part of it lies near the edge
                    offs = sorted((scr[t][0] - cx) * -uy + (scr[t][1] - cy) * ux for t in road
                                  if abs((scr[t][0] - cx) * ux + (scr[t][1] - cy) * uy) <= ROAD_AXIS_R / 2
                                  and abs((scr[t][0] - cx) * -uy + (scr[t][1] - cy) * ux) <= 200)
                    off = offs[len(offs) // 2]
                    cx, cy = cx - uy * off, cy + ux * off
                    # where the line crosses the strip: the middle of the strip
                    # hexes on it, taken the way that meets the strip first
                    hit = None
                    for sg in (1, -1):
                        on = sorted(sg * ((scr[t][0] - cx) * ux + (scr[t][1] - cy) * uy) for t in strip
                                    if abs((scr[t][0] - cx) * uy - (scr[t][1] - cy) * ux) <= 24)
                        on = [v for v in on if 0 <= v <= ROAD_RAY]
                        if on and (hit is None or on[0] < hit[0]):
                            cross = [v for v in on if v <= on[0] + 96]
                            hit = (on[0], sg * (cross[0] + cross[-1]) / 2)
                    if hit is None:
                        continue        # runs alongside the edge, not off it
                    pt = (cx + ux * hit[1], cy + uy * hit[1])
                    traced.append(((cx, cy), pt))
                if any((pt[0] - w[1][0]) ** 2 + (pt[1] - w[1][1]) ** 2 < PATCH_LEN ** 2 for w in wanted):
                    continue
                wanted.append((f"street to {mid} ({len(comp)} hexes)", pt, dest))
            continue
        if where == "road" or (where in anchors and mid != -2 and mid in {d[0] for d in by_dest}):
            ts = [t for t in old if old[t][0] == mid]
            pt = (sum(render.hex_xy_screen(t)[0] for t in ts) / len(ts),
                  sum(render.hex_xy_screen(t)[1] for t in ts) / len(ts))
            label = f"road to {mid}"
        else:
            pt = anchors[where]
            label = where
        wanted.append((label, pt, dest))
    if not explicit:
        for d, ts in by_dest.items():
            pt = (sum(render.hex_xy_screen(t)[0] for t in ts) / len(ts),
                  sum(render.hex_xy_screen(t)[1] for t in ts) / len(ts))
            wanted.append((f"old exits to {d[0]}", pt, d))

    def facing(t):
        """Which grid direction the nearest void lies in from hex t."""
        sx, sy = (t % 200) // 2, (t // 200) // 2
        for k in range(1, 12):
            for dname, (dx, dy) in (("+x", (1, 0)), ("-x", (-1, 0)), ("+y", (0, 1)), ("-y", (0, -1))):
                if (sx + dx * k, sy + dy * k) not in keep_sq:
                    return dname
        return None

    band = {}              # (x, y) -> (dest, pid)
    arrivals_out = {}      # map id -> arrival hex on this map
    deep = {t for t, dd in dist.items() if dd == INSET + STRIP + CAMERA + 6}
    for label, (ax, ay), dest in wanted:
        p0 = min(strip, key=lambda t: (scr[t][0] - ax) ** 2 + (scr[t][1] - ay) ** 2)
        px, py = scr[p0]
        # which way the edge runs here: the side of the floor the strip faces
        g = {"left": min(fx) - px, "right": px - max(fx), "top": min(fy) - py, "bottom": py - max(fy)}
        side = max(g, key=g.get)
        # straight along one edge: only strip hexes facing the void the same way
        face0 = facing(p0)
        chosen = [t for t in strip if (scr[t][0] - px) ** 2 + (scr[t][1] - py) ** 2 <= (PATCH_LEN / 2) ** 2
                  and (t % 200, t // 200) not in band and facing(t) == face0]
        for t in chosen:
            band[(t % 200, t // 200)] = (dest, SIDE_PID[side])
        if dest[0] >= 0:
            arrivals_out[dest[0]] = min(deep, key=lambda t: (scr[t][0] - px) ** 2 + (scr[t][1] - py) ** 2)
    blockers = {(t % 200, t // 200) for t, dd in dist.items()
                if dd in (INSET + STRIP + CAMERA, INSET + STRIP + CAMERA + 3)}
    inside_band = {t for t, dd in dist.items() if dd > INSET + STRIP + CAMERA + 3}
    wanted = [(lab, (round(x), round(y)), d) for lab, (x, y), d in wanted]

    # --- objects
    keep, dropped = [], Counter()
    stop = []
    for o, s, e in spans:
        if is_exit_grid(o.pid) or o.pid == SCROLL_BLOCKER:
            continue
        if not 0 <= o.tile < 40000:
            keep.append(raw[s:e])
            continue
        if in_keep(o.tile):
            keep.append(raw[s:e])
            continue
        t = pid_type(o.pid)
        dropped[{0: "items", 1: "critters", 2: "scenery", 3: "walls", 5: "misc"}.get(t, str(t))] += 1
        if t == 1 or o.script_index not in (-1, 0xFFFFFFFF):
            stop.append((hex_xy(o.tile), hex(o.pid)))
    if stop and not force:
        sys.exit(f"critters or scripted objects outside the box: {stop} - move them, or pass --force")

    # --- a start outside the camera limit (gecko's default on a map only
    # entered by exit grids) moves to the first arrival point
    start = m.default_tile
    if start not in inside_band and arrivals_out:
        start = next(iter(arrivals_out.values()))
        moved_start = True
    else:
        moved_start = False

    # --- checks
    problems = []
    for name, t in [("start", start)] + arrivals:
        if not in_keep(t):
            problems.append(f"{name} hex ({t % 200},{t // 200}) is off the floor")
    if start not in inside_band:
        problems.append("the start is outside the camera limit")
    ring = {y * 200 + x for x, y in band}
    seen, q, sides = {start}, deque([start]), set()
    while q:
        t = q.popleft()
        for n in neighbours(t):
            if n in seen or not in_keep(n):
                continue
            if n in ring:
                sides.add(band[(n % 200, n // 200)][0])
            seen.add(n)
            q.append(n)
    for d in {v[0] for v in band.values()} - sides:
        problems.append(f"no walkable way to the exits to {d}")

    # --- tiles
    pairs = list(struct.unpack_from(">20000H", raw, m.tiles_offset))
    roofs, floors = pairs[0::2], pairs[1::2]
    ground = Counter(v & 0xFFF for v in floors if (v & 0xFFF) > 1 and re.match(r"^edg", tiles_name(gi, v)))
    pool = [t for t, _n in ground.most_common(8)] or [Counter(v for v in floors if v > 1).most_common(1)[0][0]]
    weights = [ground[t] for t in pool] if ground else [1]
    rng = random.Random(210)
    voided = filled = 0
    for sq in range(10000):
        x, y = sq % 100, sq // 100
        inside = (x, y) in keep_sq
        if not inside:
            if floors[sq] > 1:
                voided += 1
            floors[sq] = 1
            roofs[sq] = 1
        elif floors[sq] <= 1:
            floors[sq] = rng.choices(pool, weights)[0]
            filled += 1

    # --- assemble
    ex_rec, sb_rec = template_records(gi)
    next_id = max(struct.unpack_from(">i", raw, o.off)[0] for o in m.objects) + 1
    new = []
    for (x, y), (dest, pid) in sorted(band.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        r = bytearray(ex_rec)
        struct.pack_into(">i", r, OBJ_ID_OFF, next_id + len(new))
        struct.pack_into(">i", r, OBJ_TILE_OFF, y * 200 + x)
        struct.pack_into(">i", r, OBJ_PID_OFF, pid)
        struct.pack_into(">4i", r, len(r) - 16, *dest)
        new.append(bytes(r))
    for x, y in sorted(blockers, key=lambda p: (p[1], p[0])):
        r = bytearray(sb_rec)
        struct.pack_into(">i", r, OBJ_ID_OFF, next_id + len(new))
        struct.pack_into(">i", r, OBJ_TILE_OFF, y * 200 + x)
        new.append(bytes(r))
    out = bytearray(raw[:total_off])
    if moved_start:
        struct.pack_into(">i", out, START_TILE_OFF, start)
    out[m.tiles_offset:m.tiles_offset + 40000] = struct.pack(
        ">20000H", *[v for pair in zip(roofs, floors) for v in pair])
    n0 = len(keep) + len(new)
    total = struct.unpack_from(">i", raw, total_off)[0] - len(spans) + n0
    out += struct.pack(">ii", total, n0) + b"".join(keep) + b"".join(new) + raw[e0_end:]
    report = dict(squares=len(keep_sq), dropped=dict(dropped), exits=len(band), blockers=len(blockers),
                  patches=wanted, traced=[((round(a), round(b)), c) for (a, b), c in traced],
                  destinations=dict(Counter(v[0] for v in band.values())), reachable=sorted(sides),
                  arrivals=arrivals_out, start_moved_to=(start if moved_start else None),
                  voided=voided, filled=filled, problems=problems)
    return bytes(out), report


_TILES = {}


def tiles_name(gi, v):
    if "l" not in _TILES:
        _TILES["l"] = [n.lower() for n in parse_lst(gi.vfs.read("art\\tiles\\tiles.lst"))]
    lst = _TILES["l"]
    v &= 0xFFF
    return lst[v] if v < len(lst) else ""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("map")
    ap.add_argument("--box", help="squares x0,y0,x1,y1 (default: content + margin)")
    ap.add_argument("--arrive", action="append", default=[],
                    help="an arrival hex this map receives, as label=tile (checked to be inside)")
    ap.add_argument("--source", help="trim this file instead (e.g. an earlier version); written to the map's usual place")
    ap.add_argument("--patch", action="append", default=[],
                    help="an exit patch, where:map. where = roads (a strip wherever a street meets the edge), road (centred where the old exits to that map "
                         "met the edge) or top/bottom/left/right (mid-edge); map -2 = the world map. "
                         "Without any, one patch per old destination, at its old exits")
    ap.add_argument("--no-notch", action="store_true", help="keep the whole box")
    ap.add_argument("--carry-all", action="store_true",
                    help="with --fill: carry every paved square on the old edge out to the new one, not just streets")
    ap.add_argument("--fill", action="store_true",
                    help="straighten by filling the floor out to its own extent instead of cutting")
    ap.add_argument("--preview", action="store_true")
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()

    name = bare_name(args.map)
    a, d = AUTHORED / name, DEPLOYED / name
    raw = a.read_bytes()
    if d.is_file() and d.read_bytes() != raw:
        sys.exit(f"{args.map}: the mod/data and base/mods/foc copies differ (an unsynced gecko save?) - stopping")
    if args.source:
        raw = inside(args.source).read_bytes()  # NOSONAR - path checked by foclib.paths.inside()
    gi = GameIndex(pristine=False)
    m = MapFile.parse(raw, gi.proto)
    arrivals = [(s.split("=")[0], int(s.split("=")[1])) for s in args.arrive]
    if args.fill:
        raw, fbox, frep = fill_floor(raw, m, gi, args.carry_all)
        m = MapFile.parse(raw, gi.proto)
        args.box = ",".join(map(str, fbox))
        args.no_notch = True
        print(f"  filled out to {fbox}: {frep}")
    content = content_squares(m, gi)
    box = tuple(int(v) for v in args.box.split(",")) if args.box else content_box(content)
    specials = [hex_xy(o.tile) for o in m.objects if is_exit_grid(o.pid)]
    keep_hex = [(t % 200, t // 200) for _n, t in arrivals] + [hex_xy(m.default_tile)]
    keep_sq = shape(box, content, keep_hex) if not args.no_notch else \
        {(x, y) for x in range(box[0], box[2] + 1) for y in range(box[1], box[3] + 1)}
    print(f"  box: {box}")
    out, rep = trim(raw, m, gi, keep_sq, content, arrivals, args.force, args.patch)
    for k, v in rep.items():
        print(f"  {k}: {v}")
    m2 = MapFile.parse(out, gi.proto)
    prev = ROOT / "build" / "map_trim" / f"{Path(args.map).stem}.png"
    prev.parent.mkdir(parents=True, exist_ok=True)
    img = render.render_map(gi, m2, 0, True).convert("RGB")
    # mark the exits (red: world map, green: another map) and the camera limit (blue)
    from PIL import ImageDraw
    spans2, *_ = spans_e0(out, m2, gi)
    dest2 = {o.tile: struct.unpack_from(">i", out, e - 16)[0] for o, s_, e in spans2 if is_exit_grid(o.pid)}
    pts = [render.square_xy(sq) for sq, t in enumerate(m2.floors[0]) if t > 1]
    pts += [(render.hex_xy_screen(o.tile)[0] - 100, render.hex_xy_screen(o.tile)[1] - 200)
            for o in m2.objects if o.elevation == 0 and 0 <= o.tile < 40000]
    minx, miny = min(q[0] for q in pts), min(q[1] for q in pts)
    dr = ImageDraw.Draw(img)
    for o in m2.objects:
        if o.elevation != 0 or not 0 <= o.tile < 40000:
            continue
        x, y = render.hex_xy_screen(o.tile)
        px, py = x + 16 - minx, y + 8 - miny
        if is_exit_grid(o.pid):
            dr.ellipse((px - 7, py - 4, px + 7, py + 4), fill=(255, 40, 40) if dest2[o.tile] == -2 else (60, 255, 60))
        elif o.pid == SCROLL_BLOCKER:
            dr.ellipse((px - 3, py - 2, px + 3, py + 2), fill=(60, 120, 255))
    # the streets followed out to the edge (yellow)
    for (x0, y0), (x1, y1) in rep["traced"]:
        dr.line((x0 + 16 - minx, y0 + 8 - miny, x1 + 16 - minx, y1 + 8 - miny), fill=(255, 230, 0), width=5)
    img.thumbnail((2000, 1200))
    img.save(prev)
    print(f"preview: {prev.relative_to(ROOT)}")
    if rep["problems"]:
        sys.exit("not writing: " + "; ".join(rep["problems"]))
    if not args.write:
        return 0
    stamp = time.strftime("%Y%m%d_%H%M%S")
    for p in (a, d):
        if p.is_file():
            shutil.copy2(p, p.with_name(f"{p.name}.{stamp}.bak"))
        p.write_bytes(out)
        print(f"  wrote {p.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
