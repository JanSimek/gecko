#!/usr/bin/env python3
"""
compare_map.py - measure a map against human-made ones.

    python tools/compare_map.py mod/data/maps/REDWELL.MAP

"It does not look like a real place" is a true observation that needs to become
a set of numbers before it can be fixed. This compares one map against a set of
real town exteriors on the things that plausibly differ:

  density      objects per floored square
  stacking     hexes carrying more than one object
  grounding    objects standing on a floored square at all
  clustering   nearest-neighbour distance - real places have streets and yards,
               so objects come in tight groups with empty space between, while
               a uniform scatter has an even spread
  enclosure    walls vs scenery, and how much floor sits inside a wall ring
"""

from __future__ import annotations

import statistics
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib.paths import inside  # noqa: E402
from foclib import (  # noqa: E402
    OBJ_TYPE_CRITTER,
    OBJ_TYPE_SCENERY,
    OBJ_TYPE_WALL,
    GameIndex,
    MapFile,
    hex_to_square,
    hex_xy,
    pid_type,
)

REFERENCE = [
    "maps/ARVILLAG.MAP",   # Arroyo village
    "maps/MODMAIN.MAP",    # Modoc
    "maps/KLADWTWN.map",   # Klamath downtown
    "maps/DENBUS1.map",    # the Den
    "maps/BROKEN1.map",    # Broken Hills
]


def measure(m: MapFile, label: str) -> dict:
    ei = 0
    elev = m.elevations[ei]
    floors = m.floors[ei]
    floored = {i for i, v in enumerate(floors) if v > 1}

    objs = [o for o in m.objects
            if o.elevation == elev and 0 <= o.tile < 40000]
    if not objs:
        return {}

    per_hex = Counter(o.tile for o in objs)
    stacked = sum(1 for t, n in per_hex.items() if n > 1)
    on_floor = sum(1 for o in objs if hex_to_square(o.tile) in floored)

    types = Counter(pid_type(o.pid) for o in objs)
    walls = types.get(OBJ_TYPE_WALL, 0)
    scen = types.get(OBJ_TYPE_SCENERY, 0)

    # nearest-neighbour distance in hexes, sampled
    pts = [hex_xy(o.tile) for o in objs]
    sample = pts[::max(1, len(pts) // 400)]
    nn = []
    for x, y in sample:
        best = 1e9
        for x2, y2 in pts:
            if x2 == x and y2 == y:
                continue
            d = abs(x2 - x) + abs(y2 - y)
            if d < best:
                best = d
                if best <= 1:
                    break
        if best < 1e9:
            nn.append(best)

    return {
        "label": label,
        "objects": len(objs),
        "floored": len(floored),
        "density": len(objs) / max(1, len(floored)),
        "stacked_pct": 100.0 * stacked / max(1, len(per_hex)),
        "on_floor_pct": 100.0 * on_floor / len(objs),
        "wall_scen": walls / max(1, scen),
        "nn_median": statistics.median(nn) if nn else 0,
        "nn_mean": statistics.mean(nn) if nn else 0,
    }


def main() -> int:
    target = inside(sys.argv[1] if len(sys.argv) > 1
                    else "mod/data/maps/REDWELL.MAP")
    gi = GameIndex()

    rows = []
    rows.append(measure(MapFile.parse(target.read_bytes(), gi.proto), target.name))  # NOSONAR - path checked by foclib.paths.inside()
    for r in REFERENCE:
        try:
            rows.append(measure(gi.load_map(gi.vfs.read(r)), r.split("/")[-1]))
        except Exception as e:  # noqa: BLE001
            print(f"  skip {r}: {e}")
    rows = [r for r in rows if r]

    hdr = (f"{'map':16s} {'objs':>6s} {'floor':>6s} {'obj/floor':>10s} "
           f"{'stacked%':>9s} {'on floor%':>10s} {'wall:scen':>10s} "
           f"{'nn med':>7s} {'nn mean':>8s}")
    print(hdr)
    print("-" * len(hdr))
    for r in rows:
        print(f"{r['label']:16s} {r['objects']:6d} {r['floored']:6d} "
              f"{r['density']:10.2f} {r['stacked_pct']:9.1f} "
              f"{r['on_floor_pct']:10.1f} {r['wall_scen']:10.2f} "
              f"{r['nn_median']:7.1f} {r['nn_mean']:8.2f}")

    mine, refs = rows[0], rows[1:]
    if refs:
        print("\nwhere this map differs from the human ones:")
        for key, name, fmt in (("density", "objects per floored square", "{:.2f}"),
                               ("stacked_pct", "hexes with stacked objects %", "{:.1f}"),
                               ("on_floor_pct", "objects standing on floor %", "{:.1f}"),
                               ("wall_scen", "wall:scenery ratio", "{:.2f}"),
                               ("nn_mean", "mean nearest neighbour (hexes)", "{:.2f}")):
            ref = statistics.mean(r[key] for r in refs)
            got = mine[key]
            flag = ""
            if ref and (got > ref * 1.6 or got < ref / 1.6):
                flag = "   <-- OFF"
            print(f"  {name:32s} mine {fmt.format(got):>7s}   "
                  f"human avg {fmt.format(ref):>7s}{flag}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
