#!/usr/bin/env python3
"""
prop_footprints.py - the blockers the originals put round a piece of scenery that
is bigger than its own hex: for each art, over every placement in the given
maps, the offsets (dx, dy) from the piece's hex at which a blocking hex
(Secret Blocking Hex, Block Hex Auto Inviso, or a block.frm wall) stands, and
how often, so a builder can copy the footprint exactly.

    python tools/prop_footprints.py ART [ART ...] --maps fo2:GECKPWPL:0 fo2:GECKTUNL:0
        -> printed: art, placements, offsets seen in at least half of them
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import port  # noqa: E402
from foclib import MapFile, parse_lst  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
BLOCKERS = {0x02000043, 0x02000158}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("arts", nargs="+")
    ap.add_argument("--maps", nargs="+", required=True)
    ap.add_argument("--radius", type=int, default=4)
    a = ap.parse_args()
    want = {x.lower() for x in a.arts}
    seen = Counter()
    offs = defaultdict(Counter)
    for spec in a.maps:
        sname, mname, elev = spec.split(":")
        elev = int(elev)
        src = port.open_source(sname)
        m = MapFile.parse(src.vfs.read(f"maps\\{mname}.map"), src.proto)
        scen = [x.split(",")[0].lower() for x in parse_lst(src.vfs.read("art\\scenery\\scenery.lst"))]
        items = [x.split(",")[0].lower() for x in parse_lst(src.vfs.read("art\\items\\items.lst"))]
        walls = [x.split(",")[0].lower() for x in parse_lst(src.vfs.read("art\\walls\\walls.lst"))]
        blk = set()
        for o in m.objects:
            if o.elevation != elev or not (0 <= o.tile < 40000):
                continue
            if o.pid in BLOCKERS:
                blk.add((o.tile % 200, o.tile // 200))
        for o in m.objects:
            if o.elevation != elev or not (0 <= o.tile < 40000) or (o.pid >> 24) not in (0, 2):
                continue
            lst = scen if (o.pid >> 24) == 2 else items
            art = lst[o.fid & 0xFFF] if (o.fid & 0xFFF) < len(lst) else ""
            if art not in want:
                continue
            x, y = o.tile % 200, o.tile // 200
            seen[art] += 1
            r = a.radius
            for dx in range(-r, r + 1):
                for dy in range(-r, r + 1):
                    if (x + dx, y + dy) in blk:
                        offs[art][(dx, dy)] += 1
    res = {}
    for art in sorted(want):
        n = seen[art]
        if not n:
            print(f"{art}: not placed in these maps")
            continue
        common = sorted(k for k, v in offs[art].items() if v * 2 >= n)
        res[art] = common
        print(f"{art}: {n} placed; blockers at {common}")
    (ROOT / "build" / "prop_footprints.json").write_text(json.dumps({k: v for k, v in res.items()}), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
