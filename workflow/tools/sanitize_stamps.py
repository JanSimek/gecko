#!/usr/bin/env python3
"""
sanitize_stamps.py - de-stack the pattern stamps.

`extract_pattern` captures a structure verbatim from a vanilla map, and the
source maps stack objects: a wall-face and its fill share a hex (adobe
corners), a small item sits on a plant, a tent has a misc-12 blocker inside its
own outline. Sawn apart wholesale, each of those becomes two objects on one
hex, which looks broken in-game.

This rewrites every stamp JSON it is given (in place, after a backup) so no
two objects share a (dxHex, dyHex). Which object of a stack survives:

    wall            walls win (a corner is a wall + its fill)
    scenery         then scenery
    misc/exit-grids then misc (scroll blockers) - keep, they matter
    items           items LOSE last - an item on a plant is the plant's
                    footprint, not a thing to pick up

Only a stack whose members are DIFFERENT types can be disambiguated this way.
Two different protos of the SAME type on one hex is either the source map's
real overlap or a capture bug; keep the first and flag the drop.

    python tools/sanitize_stamps.py [stamp.json ...]
"""

from __future__ import annotations

import json
import shutil
import sys
from collections import Counter
from pathlib import Path

from foclib.paths import inside

PICK_TYPE = {  # keep order: earlier types are preferred
    3: 0,   # wall
    2: 1,   # scenery
    5: 2,   # misc (scroll blockers, exit markers)
    0: 3,   # item
    4: 4,   # tile
}


def sanitize_one(path: Path) -> tuple[int, int, int]:
    d = json.loads(path.read_text(encoding="utf-8"))  # NOSONAR - path checked by foclib.paths.inside()
    kept = dropped = flagged = 0
    for v in d.get("variants", []):
        objs = v.get("objects", [])
        seen: dict[tuple[int, int], dict] = {}
        out = []
        for o in objs:
            key = (o["dxHex"], o["dyHex"])
            typ = (o["proPid"] >> 24) & 0xFF
            if key not in seen:
                seen[key] = o
                out.append(o)
                kept += 1
                continue
            # stack: prefer by type order
            other = seen[key]
            if PICK_TYPE.get(typ, 9) < PICK_TYPE.get((other["proPid"] >> 24) & 0xFF, 9):
                # new one wins; evict the old
                out.remove(other)
                out.append(o)
                seen[key] = o
                dropped += 1
                flagged += 1
            else:
                dropped += 1
                if typ == (other["proPid"] >> 24) & 0xFF:
                    flagged += 1
        v["objects"] = out
    path.write_text(json.dumps(d, indent=2, ensure_ascii=False), encoding="utf-8")  # NOSONAR - the stamp goes back to the file it was read from
    return kept, dropped, flagged


def main() -> int:
    paths = [inside(p) for p in sys.argv[1:]]
    if not paths:
        print("usage: python tools/sanitize_stamps.py <stamp.json> ...")
        return 1
    total_k = total_d = total_f = 0
    for p in paths:
        shutil.copy2(p, p.with_suffix(".pre-sanitize.json"))
        k, d, f = sanitize_one(p)
        total_k += k; total_d += d; total_f += f
        print(f"{p.name}: kept {k}, dropped {d} stacked, {f} same-type-or-flagged")
    print(f"total: kept {total_k}, dropped {total_d} stacked objects, {total_f} flagged")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())