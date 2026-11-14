#!/usr/bin/env python3
"""
furniture_walls.py - where Fallout 1 and Fallout 2 put furniture against
walls, measured over every map: for each scenery/item art that stands
within two hexes of a wall, the offset from the object's hex to the nearest
wall hexes, as a side ("x-run above", "x-run below", "y-run at higher x",
"y-run at lower x") and a distance, so a map tool can put a bookcase or a
bed against a wall the way the originals do, and never on one.

    python tools/furniture_walls.py [ART ...]    -> build/furniture_walls.json (+ printed table)
"""

from __future__ import annotations

import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import port  # noqa: E402
from foclib import MapFile, parse_lst  # noqa: E402
from story_survey import list_maps  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    want = {a.lower() for a in sys.argv[1:]}
    res = defaultdict(Counter)
    total = Counter()
    for sname in ("fo2", "fo1"):
        src = port.open_source(sname)
        walls = [x.split(",")[0].lower() for x in parse_lst(src.vfs.read("art\\walls\\walls.lst"))]
        lsts = {0: [x.split(",")[0].lower() for x in parse_lst(src.vfs.read("art\\items\\items.lst"))],
                2: [x.split(",")[0].lower() for x in parse_lst(src.vfs.read("art\\scenery\\scenery.lst"))]}
        for name in list_maps(src):
            try:
                m = MapFile.parse(src.vfs.read(f"maps\\{name}"), src.proto)
            except Exception:
                continue
            wset = defaultdict(set)
            objs = []
            for o in m.objects:
                if not (0 <= o.tile < 40000):
                    continue
                t = o.pid >> 24
                x, y = o.tile % 200, o.tile // 200
                if t == 3:
                    a = walls[o.fid & 0xFFF] if (o.fid & 0xFFF) < len(walls) else ""
                    if not a.startswith("block"):
                        wset[o.elevation].add((x, y))
                elif t in (0, 2):
                    lst = lsts[t]
                    a = lst[o.fid & 0xFFF] if (o.fid & 0xFFF) < len(lst) else ""
                    if a and (not want or a in want):
                        objs.append((o.elevation, x, y, a))
            for e, x, y, a in objs:
                ws = wset[e]
                total[a] += 1
                if (x, y) in ws:
                    res[a]["ON wall hex"] += 1
                    continue
                sides = []
                for d in (1, 2):
                    if any((x + dx, y - d) in ws for dx in (-1, 0, 1)) and (x, y - d) in ws:
                        sides.append(f"x-run {d} above (y-{d})")
                    if (x, y + d) in ws and any((x + dx, y + d) in ws for dx in (-1, 1)):
                        sides.append(f"x-run {d} below (y+{d})")
                    if (x + d, y) in ws and any((x + d, y + dy) in ws for dy in (-1, 1)):
                        sides.append(f"y-run {d} at higher x (x+{d})")
                    if (x - d, y) in ws and any((x - d, y + dy) in ws for dy in (-1, 1)):
                        sides.append(f"y-run {d} at lower x (x-{d})")
                res[a][", ".join(sides) if sides else "free"] += 1
    out = {a: {"count": total[a], "where": c.most_common(8)} for a, c in res.items()}
    (ROOT / "build" / "furniture_walls.json").write_text(json.dumps(out, indent=1), encoding="utf-8")
    for a in sorted(out, key=lambda k: -total[k])[:80 if not want else None]:
        print(f"{a:16s} {total[a]:5d}  " + "; ".join(f"{k} {n}" for k, n in res[a].most_common(4)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
