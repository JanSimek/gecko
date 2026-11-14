#!/usr/bin/env python3
"""
render_building.py - render one building (a square box of a source map) with
that source's own art, as find_buildings.py reports it.

    python tools/render_building.py SOURCE MAP ELEV X0 Y0 W H -o out.png
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import port  # noqa: E402
import render  # noqa: E402
from foclib.formats import MapFile, hex_xy  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("source")
    ap.add_argument("map")
    ap.add_argument("elev", type=int)
    ap.add_argument("x0", type=int)
    ap.add_argument("y0", type=int)
    ap.add_argument("w", type=int)
    ap.add_argument("h", type=int)
    ap.add_argument("-o", "--out", type=Path, required=True)
    a = ap.parse_args()
    gi = port.open_source(a.source)
    m = MapFile.parse(gi.vfs.read("maps\\" + a.map), gi.proto)
    ei = m.elevations.index(a.elev)
    x0, y0, x1, y1 = a.x0, a.y0, a.x0 + a.w, a.y0 + a.h
    # keep only this box: floor, roof off, objects inside
    for i in range(10000):
        sx, sy = i % 100, i // 100
        if not (x0 <= sx < x1 and y0 <= sy < y1):
            m.floors[ei][i] = 1
        m.roofs[ei][i] = 1
    m.objects = [o for o in m.objects if o.elevation == a.elev and 0 <= o.tile < 40000
                 and x0 <= hex_xy(o.tile)[0] // 2 < x1 and y0 <= hex_xy(o.tile)[1] // 2 < y1]
    img = render.render_map(gi, m, ei, True)
    img.thumbnail((1200, 800))
    img.save(a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
