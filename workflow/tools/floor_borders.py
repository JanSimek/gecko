#!/usr/bin/env python3
"""
floor_borders.py - how Fallout 2 frames a room's floor with a border tile set
(the vtgst strips along the walls of Gecko's tunnels and power plant, the
vault sets), measured from the maps that use it.

    python tools/floor_borders.py vtgst brn       -> build/floor_borders/vtgst.json + report

A floor square of the border set is described by which of its eight neighbour
squares are "outside" (not floored with the interior set or the border set):
the four sides N (row-1), S (row+1), W (column+1, screen left), E (column-1),
then the four diagonals. For each signature and (column % 2, row % 2) the
report gives the pieces the originals use. It also gives the interior set's
checker: the piece on squares with (column + row) even and odd.

And where the floor sits against the walls: for every wall hex next to a
border square, whether the wall's own square is floored, so a builder knows
whether the border runs under the wall or inside it.
"""

from __future__ import annotations

import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import port  # noqa: E402
from foclib import MapFile, parse_lst  # noqa: E402
from story_survey import list_maps  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "floor_borders"
SIDES = (("N", 0, -1), ("S", 0, 1), ("W", 1, 0), ("E", -1, 0))
DIAG = (("nw", 1, -1), ("ne", -1, -1), ("sw", 1, 1), ("se", -1, 1))


def main() -> int:
    border, inner = sys.argv[1], sys.argv[2]
    OUT.mkdir(parents=True, exist_ok=True)
    sig = defaultdict(Counter)
    checker = defaultdict(Counter)
    wall_sq = Counter()
    maps = Counter()
    for sname in ("fo2", "fo1"):
        src = port.open_source(sname)
        tiles = [x.split(",")[0].lower().replace(".frm", "") for x in parse_lst(src.vfs.read("art\\tiles\\tiles.lst"))]
        for name in list_maps(src):
            try:
                m = MapFile.parse(src.vfs.read(f"maps\\{name}"), src.proto)
            except Exception:
                continue
            for e, fl in zip(m.elevations, m.floors):
                nm = {}
                for sq, v in enumerate(fl):
                    i = v & 0xFFF
                    if 1 < i < len(tiles):
                        nm[sq] = tiles[i]
                b = {sq for sq, n in nm.items() if n.startswith(border)}
                if len(b) < 8:
                    continue
                maps[f"{sname}/{name}"] += 1
                inn = {sq for sq, n in nm.items() if n.startswith(inner)}
                region = b | inn

                def at(c, r):
                    return r * 100 + c if 0 <= c < 100 and 0 <= r < 100 else -1
                for sq in b:
                    c, r = sq % 100, sq // 100
                    s = "".join(k if at(c + dc, r + dr) not in region else "." for k, dc, dr in SIDES)
                    d = "".join(k[-1] if at(c + dc, r + dr) not in region else "." for k, dc, dr in DIAG)
                    sig[f"{s}|{d}|c{c % 2}r{r % 2}"][nm[sq]] += 1
                for sq in inn:
                    c, r = sq % 100, sq // 100
                    checker[(c + r) % 2][nm[sq]] += 1
                walls = {}
                for o in m.objects:
                    if (o.pid >> 24) == 3 and o.elevation == e and 0 <= o.tile < 40000:
                        walls[(o.tile % 200, o.tile // 200)] = 1
                for (x, y) in walls:
                    sq = (y // 2) * 100 + x // 2
                    near = any(at(x // 2 + dc, y // 2 + dr) in b for dc in (-1, 0, 1) for dr in (-1, 0, 1))
                    if near:
                        wall_sq["floored: " + ("border" if sq in b else "interior" if sq in inn else "none")] += 1
    rep = [f"maps: {len(maps)} ({', '.join(m for m, _ in maps.most_common(12))})",
           f"interior checker: (c+r) even {checker[0].most_common(3)}; odd {checker[1].most_common(3)}",
           f"the square under a wall hex beside the border: {dict(wall_sq)}", "",
           "border signature (outside sides N S W E | diagonals | parity) -> pieces"]
    for k, c in sorted(sig.items(), key=lambda kv: -sum(kv[1].values())):
        if sum(c.values()) >= 4:
            rep.append(f"  {k:24s} {sum(c.values()):5d}  " + ", ".join(f"{a}x{n}" for a, n in c.most_common(5)))
    (OUT / f"{border}.txt").write_text("\n".join(rep), encoding="utf-8")
    (OUT / f"{border}.json").write_text(json.dumps({"sig": {k: c.most_common() for k, c in sig.items()},
                                                    "checker": {str(k): c.most_common() for k, c in checker.items()}},
                                                   indent=1), encoding="utf-8")
    print("\n".join(rep[:60]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
