#!/usr/bin/env python3
"""
exit_survey.py - how vanilla and TC maps lay out their exit grids.

    python tools/exit_survey.py            -> build/map_survey/exits.csv + a summary

Exit grids on a map are grouped into patches (hexes touching, 8-way). For
each patch: size, its extent along and across the nearest floor edge, how
far it sits from the floored edge and from the scroll-blocker ring, the
exit-grid pids used, whether it lies on road tiles, and its destination
(world map, another map, the town map).
"""

from __future__ import annotations

import csv
import re
import statistics
import struct
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import port  # noqa: E402
from foclib import parse_lst  # noqa: E402
from foclib.formats import MapFile, hex_xy, is_exit_grid  # noqa: E402
from map_survey import ROAD_RE, SOURCES, list_maps  # noqa: E402
from map_exits_to_edge import object_spans  # noqa: E402

OUT = Path(__file__).resolve().parent.parent / "build" / "map_survey"


def patches(pts):
    pts = set(pts)
    seen, out = set(), []
    for p in pts:
        if p in seen:
            continue
        stack, comp = [p], []
        seen.add(p)
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


def main() -> int:
    rows = []
    for src in SOURCES:
        try:
            gi = port.open_source(src)
        except SystemExit:
            continue
        tiles = [n.lower() for n in parse_lst(gi.vfs.read("art\\tiles\\tiles.lst"))]
        for mp in list_maps(gi):
            try:
                raw = gi.vfs.read(mp)
                m = MapFile.parse(raw, gi.proto)
                spans, *_ = object_spans(raw, m, gi)
            except Exception:
                continue
            fl = m.floors[0]
            floored = {i for i, v in enumerate(fl) if (v & 0xFFF) > 1}
            if len(floored) < 500:
                continue
            ex = {}
            for o, s, e in spans:
                if is_exit_grid(o.pid) and 0 <= o.tile < 40000:
                    ex[hex_xy(o.tile)] = (o.pid, struct.unpack_from(">4i", raw, e - 16))
            if not ex:
                continue
            sb = [hex_xy(o.tile) for o in m.objects if o.pid == 0x500000C and o.elevation == m.elevations[0]]
            # distance (squares) from a square to the nearest unfloored square
            def edge_dist(sx, sy):
                for r in range(0, 30):
                    for dx in range(-r, r + 1):
                        for dy in (-r, r) if abs(dx) != r else range(-r, r + 1):
                            q = (sy + dy) * 100 + sx + dx
                            if not (0 <= sx + dx < 100 and 0 <= sy + dy < 100) or q not in floored:
                                return r
                return 30
            for comp in patches(ex):
                xs = [p[0] for p in comp]; ys = [p[1] for p in comp]
                pids = Counter(ex[p][0] for p in comp)
                dests = Counter(ex[p][1][0] for p in comp)
                cx, cy = sum(xs) / len(xs), sum(ys) / len(ys)
                ed = edge_dist(int(cx) // 2, int(cy) // 2)
                sbd = min((abs(a - cx) + abs(b - cy) for a, b in sb), default=None)
                road = sum(1 for x, y in comp if ROAD_RE.match(tiles[fl[(y // 2) * 100 + x // 2] & 0xFFF] if (fl[(y // 2) * 100 + x // 2] & 0xFFF) < len(tiles) else "")) / len(comp)
                # thickness: hexes across the patch at its middle, both axes
                w_x = max(xs) - min(xs) + 1
                w_y = max(ys) - min(ys) + 1
                rows.append(dict(source=src, map=mp.split("\\")[-1], n=len(comp), w_x=w_x, w_y=w_y,
                                 thin=min(w_x, w_y), long=max(w_x, w_y),
                                 fill=round(len(comp) / (w_x * w_y), 2),
                                 pids="/".join(f"{p & 0xFF:x}" for p, _ in pids.most_common()),
                                 dest=dests.most_common(1)[0][0], edge_sq=ed,
                                 blocker_hex=round(sbd, 1) if sbd is not None else "",
                                 road=round(road, 2), patches_on_map=0))
            k = len([r for r in rows if r["map"] == mp.split("\\")[-1] and r["source"] == src])
            for r in rows[-k:]:
                r["patches_on_map"] = k
        print(src, len(rows))
    OUT.mkdir(parents=True, exist_ok=True)
    with (OUT / "exits.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    def med(k, sel):
        v = [float(r[k]) for r in sel if r[k] != ""]
        return round(statistics.median(v), 1) if v else None
    big = [r for r in rows if r["n"] >= 4]
    print(f"\npatches: {len(rows)} ({len(big)} with 4+ hexes)")
    for label, sel in (("all", big), ("to world map", [r for r in big if r["dest"] == -2]),
                       ("to another map", [r for r in big if r["dest"] >= 0])):
        print(f"{label:15} n={len(sel):5} hexes {med('n', sel)} thin {med('thin', sel)} long {med('long', sel)} "
              f"fill {med('fill', sel)} edge_sq {med('edge_sq', sel)} blocker_hex {med('blocker_hex', sel)} "
              f"road {med('road', sel)} patches/map {med('patches_on_map', sel)}")
    print("thickness:", Counter(r["thin"] for r in big).most_common(8))
    print("pids:", Counter(r["pids"] for r in big).most_common(10))
    print("edge distance (squares):", Counter(r["edge_sq"] for r in big).most_common(10))
    return 0


if __name__ == "__main__":
    sys.exit(main())
