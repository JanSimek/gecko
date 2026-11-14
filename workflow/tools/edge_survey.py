#!/usr/bin/env python3
"""
edge_survey.py - how Fallout, Fallout 2 and the TCs place their Hi-Res Patch edge
boxes (maps/<name>.edg), measured against each map's painted floor.

    python tools/edge_survey.py          -> build/map_survey/edges.csv + a summary per source

For every zone of every .edg (v1 and v2, the format gecko's MapEdgeReader reads)
whose map is in the same source: the box in engine screen px (tileToScreenXY of
its four hexes), and against the elevation's painted floor (each square tile's
rhombus, tile.cc squareTileToScreenXY: top-left = hex(2c,2r) - (48,2), corners
(48,0) (80,24) (32,36) (0,12)):
  void_in     share of the box with no floor under it
  floor_out   share of the painted floor outside the box
  walk_out    share of the open floor hexes (no blocking object) outside the box
  exits       exit-grid hexes inside / outside the box
  black       black frame scenery (art named black*): how many, and how far each
              box side is from the nearest black piece's inner edge
Elevations with no floor, and full-grid zones, are counted separately.
"""

from __future__ import annotations

import csv
import statistics
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))

import port  # noqa: E402
from foclib import parse_lst  # noqa: E402
from foclib.formats import MapFile  # noqa: E402
from map_survey import SOURCES, list_maps  # noqa: E402

OUT = Path(__file__).resolve().parent.parent / "build" / "map_survey"
W = 200
FULL = (199, 0, 39800, 39999)
SCALE = 4          # px per mask px


def hex_xy(t):
    x, y = t % W, t // W
    v3 = W - 1 - x
    return 48 * (v3 // 2) + 32 * (v3 & 1) + 16 * y, -12 * (v3 // 2) + 12 * y


def parse_edg(b):
    vals = list(struct.unpack(">%di" % (len(b) // 4), b[:len(b) // 4 * 4]))
    if vals[0] != 0x45444745 or vals[1] not in (1, 2):
        return None
    v2 = vals[1] == 2
    rest = vals[3:]
    zones = {0: [], 1: [], 2: []}
    level = 0
    i = 0
    for e in range(3):
        if v2:
            i += 5                                 # square rect + clip sides
        if level != e:
            continue
        while i + 4 <= len(rest):
            zones[e].append(tuple(rest[i:i + 4]))
            i += 4
            if i >= len(rest):
                break
            level = rest[i]
            i += 1
            if level != e:
                break
    return zones


def floor_mask(fl, origin, size):
    ox, oy = origin
    im = Image.new("1", (size[0] // SCALE + 1, size[1] // SCALE + 1), 0)
    d = ImageDraw.Draw(im)
    for sq, v in enumerate(fl):
        if (v & 0xFFF) <= 1:
            continue
        c, r = sq % 100, sq // 100
        hx, hy = hex_xy(2 * r * W + 2 * c)
        sx, sy = hx - 48 - ox, hy - 2 - oy
        pts = [(sx + 48, sy), (sx + 80, sy + 24), (sx + 32, sy + 36), (sx, sy + 12)]
        d.polygon([(px / SCALE, py / SCALE) for px, py in pts], fill=1)
    return im


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    rows = []
    extra = defaultdict(Counter)
    for src in SOURCES:
        try:
            gi = port.open_source(src)
        except SystemExit:
            continue
        try:
            scen = [n.lower() for n in parse_lst(gi.vfs.read("art\\scenery\\scenery.lst"))]
        except Exception:
            scen = []
        maps = list_maps(gi)
        for mp in maps:
            edg_path = mp[:-4] + ".edg"
            if not gi.vfs.exists(edg_path):
                continue
            zones = parse_edg(gi.vfs.read(edg_path))
            if zones is None:
                extra[src]["bad edg"] += 1
                continue
            try:
                raw = gi.vfs.read(mp)
                m = MapFile.parse(raw, gi.proto)
            except Exception:
                extra[src]["map unreadable"] += 1
                continue
            for e in (0, 1, 2):
                zs = zones[e]
                fl = m.floors[m.elevations.index(e)] if e in m.elevations else None
                has_floor = fl is not None and any((v & 0xFFF) > 1 for v in fl)
                if len(zs) > 1:
                    extra[src]["elevations with several zones"] += 1
                if not has_floor:
                    extra[src]["empty elevation: " + ("full grid" if zs == [FULL] else "other")] += 1
                    continue
                if zs == [FULL] or not zs:
                    extra[src]["floored elevation left full grid"] += 1
                    continue
                objs = [o for o in m.objects if o.elevation == e and 0 <= o.tile < W * W]
                blocked, exits, blacks = set(), set(), []
                for o in objs:
                    t = o.pid >> 24
                    flags = struct.unpack_from(">I", raw, o.off + 0x24)[0]
                    if t == 5 and 0x05000010 <= o.pid <= 0x05000017:
                        exits.add(o.tile)
                    elif t in (2, 3) and not flags & 0x10:
                        blocked.add(o.tile)
                    if t == 2:
                        fid = struct.unpack_from(">I", raw, o.off + 0x20)[0] & 0xFFFFFF
                        if fid < len(scen) and scen[fid].startswith("black"):
                            blacks.append(o.tile)
                floored = [t for t in range(W * W) if (fl[(t // W // 2) * 100 + (t % W) // 2] & 0xFFF) > 1]
                walk = [t for t in floored if t not in blocked]
                xs = [hex_xy(t)[0] for t in floored]
                ys = [hex_xy(t)[1] for t in floored]
                origin = (min(xs) - 100, min(ys) - 100)
                size = (max(xs) - min(xs) + 300, max(ys) - min(ys) + 300)
                import numpy as np
                mask = np.array(floor_mask(fl, origin, size), dtype=bool)
                total_floor = int(mask.sum())
                for (L, T, R, B) in zs:
                    x0, x1 = sorted((hex_xy(L)[0], hex_xy(R)[0]))
                    y0, y1 = sorted((hex_xy(T)[1], hex_xy(B)[1]))
                    bx0, by0 = (x0 - origin[0]) // SCALE, (y0 - origin[1]) // SCALE
                    bx1, by1 = (x1 - origin[0]) // SCALE, (y1 - origin[1]) // SCALE
                    H, Wd = mask.shape
                    cy0, cy1 = max(0, by0), min(H - 1, by1)
                    cx0, cx1 = max(0, bx0), min(Wd - 1, bx1)
                    inside_floor = int(mask[cy0:cy1 + 1, cx0:cx1 + 1].sum()) if cy1 >= cy0 and cx1 >= cx0 else 0
                    box_area = max(1, (by1 - by0 + 1) * (bx1 - bx0 + 1))

                    def inside(t):
                        x, y = hex_xy(t)
                        return x0 <= x <= x1 and y0 <= y <= y1
                    w_out = sum(1 for t in walk if not inside(t))
                    ex_in = sum(1 for t in exits if inside(t))
                    side = ["", "", "", ""]
                    if blacks:
                        bxs = [hex_xy(t) for t in blacks]
                        side = [min(abs(p[0] - x0) for p in bxs), min(abs(p[1] - y0) for p in bxs),
                                min(abs(p[0] - x1) for p in bxs), min(abs(p[1] - y1) for p in bxs)]
                    rows.append({
                        "source": src, "map": mp.split("\\")[-1], "elev": e,
                        "box_w": x1 - x0, "box_h": y1 - y0,
                        "void_in": round(1 - inside_floor / box_area, 4),
                        "floor_out": round(1 - inside_floor / max(1, total_floor), 4),
                        "walk_out": round(w_out / max(1, len(walk)), 4),
                        "exits_in": ex_in, "exits_out": len(exits) - ex_in,
                        "blacks": len(blacks),
                        "black_left": side[0], "black_top": side[1], "black_right": side[2], "black_bottom": side[3],
                        "zones": len(zs),
                    })
        print(f"{src}: {sum(1 for r in rows if r['source'] == src)} zones measured", flush=True)
    with open(OUT / "edges.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print()
    for src in SOURCES:
        rs = [r for r in rows if r["source"] == src]
        if not rs:
            continue
        med = lambda k: statistics.median(float(r[k]) for r in rs)  # noqa: E731
        q = lambda k, p: sorted(float(r[k]) for r in rs)[int(p * (len(rs) - 1))]  # noqa: E731
        ex_out = sum(1 for r in rs if r["exits_out"] > 0)
        print(f"{src}: {len(rs)} zones  box median {med('box_w'):.0f}x{med('box_h'):.0f} px")
        print(f"   void inside the box: median {100 * med('void_in'):.1f}%  90th pct {100 * q('void_in', 0.9):.1f}%  "
              f"zones with >2% void {sum(1 for r in rs if r['void_in'] > 0.02)}")
        print(f"   floor outside the box: median {100 * med('floor_out'):.1f}%   walkable outside: median "
              f"{100 * med('walk_out'):.1f}%  90th pct {100 * q('walk_out', 0.9):.1f}%")
        print(f"   zones with exit hexes outside the box: {ex_out} "
              f"(exits outside / all, summed: {sum(r['exits_out'] for r in rs)}/{sum(r['exits_out'] + r['exits_in'] for r in rs)})")
        print(f"   zones on maps with black frame pieces: {sum(1 for r in rs if r['blacks'])}")
        print(f"   other: {dict(extra[src])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
