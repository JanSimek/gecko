#!/usr/bin/env python3
"""
map_exits_to_edge.py - rebuild a map's exit-grid ring at the edge of its
painted floor.

    python tools/map_exits_to_edge.py RWCORE2.MAP --preview   # render + checks only
    python tools/map_exits_to_edge.py RWCORE2.MAP             # write (backups kept)

Why: RWCORE / RWCORE2 were cut from vanilla city2.map, whose exit ring sits a
few hexes inside the painted ground - fine at 640x480, but at the owner's
1920x1080 (x2) view there is ground beyond it (owner, 2026-09-28).

How: the vanilla ring is a band of exit hexes forming a rectangle on screen,
RING_DEPTH px deep on every side. The rectangle's sides are pushed out, each
as far as the painted floor still covers most of the new outer edge
(EDGE_COVER), and the band is rebuilt at the new size: every hex whose
screen point lies in the outer rectangle but not the inner one. A bigger
ring needs more exits than the old one had, so the old exit objects are
dropped and new ones written from one of them (same exit data: to the world
map), each side keeping the exit-grid variant (pid) the old side used.

Checked before writing: walking from the map's start hex over the engine's
own hex neighbours (tile.cc _dir_tile), nothing outside the ring can be
reached without stepping on an exit. Writes mod/data/maps/<name> and
base/mods/foc/maps/<name>, each after a <name>.<timestamp>.bak copy, and
refuses if the two differ (a gecko edit not yet synced back).
"""

from __future__ import annotations

import argparse
import shutil
import struct
import sys
import time
from collections import Counter, deque
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import numpy as np  # noqa: E402

from foclib import GameIndex, MapFile  # noqa: E402
from foclib.formats import is_exit_grid  # noqa: E402
import render  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
AUTHORED = ROOT / "mod" / "data" / "maps"
DEPLOYED = ROOT / "base" / "mods" / "foc" / "maps"
EDGE_COVER = 0.9          # share of the new outer edge that must be painted floor
SCAN_STEP = 16            # px per outward step when searching the edge
RING_MIN = 24             # thinnest band (px) tried; the first that seals is used
OBJ_ID_OFF, OBJ_TILE_OFF = 0, 4


def _screen():
    sx = np.zeros(40000, int)
    sy = np.zeros(40000, int)
    for t in range(40000):
        sx[t], sy[t] = render.hex_xy_screen(t)
    return sx, sy


SX, SY = _screen()


def neighbours(t: int):
    """tile.cc _dir_tile (hex grid width 200)."""
    x, y = t % 200, t // 200
    d = [(-1, 0), (-1, 1), (0, 1), (1, 1), (1, 0), (0, -1)] if x % 2 == 0 else \
        [(-1, -1), (-1, 0), (0, 1), (1, 0), (1, -1), (0, -1)]
    for a, b in d:
        nx, ny = x + a, y + b
        if 0 <= nx < 200 and 0 <= ny < 200:
            yield ny * 200 + nx


def painted_mask(m: MapFile) -> np.ndarray:
    fl = np.array(m.floors[0]) & 0xFFFF
    t = np.arange(40000)
    return fl[(t // 200 // 2) * 100 + (t % 200) // 2] > 1


def ring_rect(exits):
    xs = np.array([SX[o.tile] for o in exits])
    ys = np.array([SY[o.tile] for o in exits])
    x0, y0, x1, y1 = xs.min(), ys.min(), xs.max(), ys.max()
    L, R, T, B = xs - x0, x1 - xs, ys - y0, y1 - ys
    mid_h = (L > 150) & (R > 150)
    mid_v = (T > 150) & (B > 150)
    depth = int(max(T[mid_h & (ys < (y0 + y1) / 2)].max(), B[mid_h & (ys >= (y0 + y1) / 2)].max(),
                    L[mid_v & (xs < (x0 + x1) / 2)].max(), R[mid_v & (xs >= (x0 + x1) / 2)].max()))
    return (int(x0), int(y0), int(x1), int(y1)), depth


def side_pids(exits, rect, depth):
    x0, y0, x1, y1 = rect
    c = {s: Counter() for s in ("left", "right", "top", "bottom")}
    for o in exits:
        sx, sy = SX[o.tile], SY[o.tile]
        if sx - x0 <= depth: c["left"][o.pid] += 1
        elif x1 - sx <= depth: c["right"][o.pid] += 1
        elif sy - y0 <= depth: c["top"][o.pid] += 1
        elif y1 - sy <= depth: c["bottom"][o.pid] += 1
    fallback = Counter(o.pid for o in exits).most_common(1)[0][0]
    return {s: (v.most_common(1)[0][0] if v else fallback) for s, v in c.items()}


def grow(rect, painted):
    """Push each side out while the new outer edge stays mostly painted."""
    x0, y0, x1, y1 = rect
    idx = np.nonzero(painted)[0]
    px, py = SX[idx], SY[idx]
    allx, ally = SX, SY

    def cover(sel_all):
        n = sel_all.sum()
        if n == 0:
            return 0.0
        return painted[sel_all].sum() / n

    def edge(side, a, b, c, d):
        # hexes in the 32 px strip just inside the candidate outer edge
        if side == "left":
            sel = (allx >= a) & (allx < a + 32) & (ally >= b) & (ally <= d)
        elif side == "right":
            sel = (allx <= c) & (allx > c - 32) & (ally >= b) & (ally <= d)
        elif side == "top":
            sel = (ally >= b) & (ally < b + 24) & (allx >= a) & (allx <= c)
        else:
            sel = (ally <= d) & (ally > d - 24) & (allx >= a) & (allx <= c)
        return cover(sel)

    changed = True
    while changed:
        changed = False
        for side, dxy in (("left", (-SCAN_STEP, 0, 0, 0)), ("right", (0, 0, SCAN_STEP, 0)),
                          ("top", (0, -SCAN_STEP, 0, 0)), ("bottom", (0, 0, 0, SCAN_STEP))):
            cand = (x0 + dxy[0], y0 + dxy[1], x1 + dxy[2], y1 + dxy[3])
            if edge(side, *cand) >= EDGE_COVER:
                x0, y0, x1, y1 = cand
                changed = True
    return x0, y0, x1, y1


def build_ring(rect, depth, pids):
    x0, y0, x1, y1 = rect
    tiles = []
    for t in range(40000):
        sx, sy = SX[t], SY[t]
        if not (x0 <= sx <= x1 and y0 <= sy <= y1):
            continue
        left, right = sx - x0 <= depth, x1 - sx <= depth
        top, bottom = sy - y0 <= depth, y1 - sy <= depth
        if left or right or top or bottom:
            side = "left" if left else "right" if right else "top" if top else "bottom"
            tiles.append((t, pids[side]))
    return tiles


def sealed(start: int, ring: set[int], rect) -> tuple[bool, int]:
    x0, y0, x1, y1 = rect
    seen = {start}
    q = deque([start])
    while q:
        t = q.popleft()
        for n in neighbours(t):
            if n in ring or n in seen:
                continue
            if not (x0 <= SX[n] <= x1 and y0 <= SY[n] <= y1):
                return False, len(seen)
            seen.add(n)
            q.append(n)
    # also the map border must be unreachable (neighbours() stops at it)
    return True, len(seen)


def object_spans(raw: bytes, m: MapFile, gi):
    """[(obj, start, end)] for elevation-0 objects, and the offsets of the
    total-count and elevation-0-count ints."""
    first = m.objects[0].off
    total_off, e0_off = first - 8, first - 4
    n0 = struct.unpack_from(">i", raw, e0_off)[0]
    spans, p = [], first
    for i in range(n0):
        obj, end = MapFile._read_object(raw, p, gi.proto)
        spans.append((m.objects[i], p, end))
        p = end
    return spans, total_off, e0_off, p


def rebuild(raw: bytes, m: MapFile, gi, ring_tiles):
    spans, total_off, e0_off, e0_end = object_spans(raw, m, gi)
    keep = [raw[s:e] for o, s, e in spans if not is_exit_grid(o.pid)]
    tmpl_o, ts, te = next((o, s, e) for o, s, e in spans if is_exit_grid(o.pid))
    template = raw[ts:te]
    next_id = max(struct.unpack_from(">i", raw, o.off)[0] for o in m.objects) + 1
    new = []
    for i, (t, pid) in enumerate(ring_tiles):
        rec = bytearray(template)
        struct.pack_into(">i", rec, OBJ_ID_OFF, next_id + i)
        struct.pack_into(">i", rec, OBJ_TILE_OFF, t)
        struct.pack_into(">i", rec, 0x2C, pid)                    # pid
        new.append(bytes(rec))
    n0 = len(keep) + len(new)
    total = struct.unpack_from(">i", raw, total_off)[0] - len(spans) + n0
    out = bytearray(raw[:total_off])
    out += struct.pack(">ii", total, n0)
    out += b"".join(keep) + b"".join(new)
    out += raw[e0_end:]
    return bytes(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("map")
    ap.add_argument("--preview", action="store_true")
    args = ap.parse_args()

    a, d = AUTHORED / args.map, DEPLOYED / args.map
    raw = a.read_bytes()
    if d.is_file() and d.read_bytes() != raw:
        sys.exit(f"{args.map}: mod/data and base/mods/foc copies differ (unsynced gecko edit?) - stopping")
    gi = GameIndex(pristine=False)
    m = MapFile.parse(raw, gi.proto)
    exits = [o for o in m.objects if is_exit_grid(o.pid) and o.elevation == 0]
    if any(is_exit_grid(o.pid) and o.elevation != 0 for o in m.objects):
        sys.exit("exit grids on other elevations - not handled")
    rect, depth = ring_rect(exits)
    painted = painted_mask(m)
    new_rect = grow(rect, painted)
    pids = side_pids(exits, rect, depth)
    ok_old, _ = sealed(m.default_tile, {o.tile for o in exits}, rect)
    # the old band's depth is generous; use the thinnest band that still seals
    for depth in list(range(RING_MIN, depth, 8)) + [depth]:
        ring = build_ring(new_rect, depth, pids)
        ok_new, inside = sealed(m.default_tile, {t for t, _p in ring}, new_rect)
        if ok_new:
            break
    print(f"{args.map}: ring {rect} -> {new_rect} (depth {depth}px); "
          f"exits {len(exits)} -> {len(ring)}; sealed: old {ok_old}, new {ok_new} ({inside} hexes inside)")
    if not ok_new:
        sys.exit("new ring would leak - not writing")
    out = rebuild(raw, m, gi, ring)
    m2 = MapFile.parse(out, gi.proto)
    assert len(m2.objects) == len(m.objects) - len(exits) + len(ring)
    assert sum(1 for o in m2.objects if is_exit_grid(o.pid)) == len(ring)
    assert [o.pid for o in m2.objects if not is_exit_grid(o.pid)] == \
           [o.pid for o in m.objects if not is_exit_grid(o.pid)]

    img = render.render_map(gi, m2, 0, True)
    prev = ROOT / "build" / "pc_parts" / f"{Path(args.map).stem}_exits.png"
    prev.parent.mkdir(parents=True, exist_ok=True)
    img.thumbnail((1800, 1200))
    img.save(prev)
    print(f"preview: {prev.relative_to(ROOT)}")
    if args.preview:
        return 0
    stamp = time.strftime("%Y%m%d_%H%M%S")
    for p in (a, d):
        if p.is_file():
            shutil.copy2(p, p.with_name(f"{p.name}.{stamp}.bak"))
        p.write_bytes(out)
        print(f"wrote {p.relative_to(ROOT)} (previous: {p.name}.{stamp}.bak)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
