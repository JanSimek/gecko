#!/usr/bin/env python3
"""
find_buildings.py - find buildings, in any source's maps, walled in given
wall families (e.g. to match a FOC town's look).

    python tools/find_buildings.py --families nec,sher,snec,jds,jas,jcs,jbs --min-roof 120

A building is a roof component (tools/buildings.py); its walls are the wall
objects inside its bbox plus a 2-square margin. Prints the buildings whose
walls are mostly in the families asked for, largest first, with source, map,
elevation, origin square and size - the arguments build_clcourt.py and
friends need to lift one.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import port  # noqa: E402
from buildings import roof_components  # noqa: E402
from foclib import parse_lst  # noqa: E402
from foclib.formats import MapFile, hex_xy, pid_type  # noqa: E402
from map_survey import SOURCES, list_maps  # noqa: E402


def fam(n: str) -> str:
    return re.sub(r"\d.*$", "", n.lower())


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--families", required=True)
    ap.add_argument("--min-roof", type=int, default=120)
    ap.add_argument("--max-roof", type=int, default=100000)
    ap.add_argument("--furniture", default="", help="scenery name fragments to count inside (e.g. pew,desk)")
    ap.add_argument("--share", type=float, default=0.6, help="share of the walls in those families")
    ap.add_argument("--sources", default=",".join(SOURCES))
    args = ap.parse_args()
    want = set(args.families.lower().split(","))
    hits = []
    for src in args.sources.split(","):
        try:
            gi = port.open_source(src)
        except SystemExit:
            continue
        wl = parse_lst(gi.vfs.read("art\\walls\\walls.lst"))
        sc = parse_lst(gi.vfs.read("art\\scenery\\scenery.lst"))
        furn = [f for f in args.furniture.lower().split(",") if f]
        for mp in list_maps(gi):
            try:
                m = MapFile.parse(gi.vfs.read(mp), gi.proto)
            except Exception:
                continue
            for ei, elev in enumerate(m.elevations):
                walls = [(hex_xy(o.tile), wl[o.fid & 0xFFF] if (o.fid & 0xFFF) < len(wl) else "")
                         for o in m.objects if o.elevation == elev and pid_type(o.pid) == 3]
                scen = [(hex_xy(o.tile), sc[o.fid & 0xFFF].lower() if (o.fid & 0xFFF) < len(sc) else "")
                        for o in m.objects if o.elevation == elev and pid_type(o.pid) == 2] if furn else []
                for comp in roof_components(m, ei):
                    if not (args.min_roof <= len(comp) <= args.max_roof):
                        continue
                    xs = [s % 100 for s in comp]; ys = [s // 100 for s in comp]
                    x0, x1, y0, y1 = min(xs) - 2, max(xs) + 2, min(ys) - 2, max(ys) + 2
                    c = Counter(fam(n) for (hx, hy), n in walls
                                if x0 <= hx // 2 <= x1 and y0 <= hy // 2 <= y1 and not n.lower().startswith("block"))
                    total = sum(c.values())
                    if not total:
                        continue
                    share = sum(v for k, v in c.items() if k in want) / total
                    if share >= args.share:
                        inside = Counter(f for (hx, hy), n in scen if x0 <= hx // 2 <= x1 and y0 <= hy // 2 <= y1
                                         for f in furn if f in n)
                        hits.append((len(comp), src, mp.split("\\")[-1], elev, (x0, y0), (x1 - x0 + 1, y1 - y0 + 1),
                                     round(share, 2), c.most_common(3), dict(inside)))
    for h in sorted(hits, reverse=True):
        print(h)
    return 0


if __name__ == "__main__":
    sys.exit(main())
