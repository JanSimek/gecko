#!/usr/bin/env python3
"""
wall_junctions.py - measure how Fallout 1 and 2 build one wall set's runs and
junctions, in the form a wall builder can use (rework_clcourt.build_walls'
JUNCTION table is the nec set's).

    python tools/wall_junctions.py dv          -> build/wall_junctions/dv.json + a report

For every piece of the set on every map: whether it sits in an x-run (a wall
neighbour at x-1 or x+1) or a y-run, the row and column parity, and for x-run
hexes the key (lo, hi, up, dn) - lo/hi: the run continues to x-1 / x+1; up: a
y-run touches from above (x, y-1); dn: a y-run hangs below a one-hex gap
(x, y+1 empty, x, y+2 a wall) - with a door beside it noted as "door". For
y-run hexes: whether it is the top (nothing above), the bottom, or inside, and
whether a door is next to it. Doors are scenery of the door subtype within one
hex of the run.
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
OUT = ROOT / "build" / "wall_junctions"


def main() -> int:
    fam = sys.argv[1]
    pat = re.compile(rf"^{re.escape(fam)}\d+[a-z]?\.frm$", re.I)
    OUT.mkdir(parents=True, exist_ok=True)
    xkey = defaultdict(Counter)       # f"{lo},{hi},{up},{dn}|x%2" -> pieces
    ykey = defaultdict(Counter)       # f"{role}|door" -> pieces
    rows = Counter()
    cols = Counter()
    xrun = defaultdict(Counter)       # x%2 -> plain x-run pieces
    for sname in ("fo2", "fo1"):
        src = port.open_source(sname)
        walls = [x.split(",")[0].lower() for x in parse_lst(src.vfs.read("art\\walls\\walls.lst"))]
        for name in list_maps(src):
            try:
                m = MapFile.parse(src.vfs.read(f"maps\\{name}"), src.proto)
            except Exception:
                continue
            cells = defaultdict(dict)
            doors = defaultdict(set)
            for o in m.objects:
                if not (0 <= o.tile < 40000):
                    continue
                t = o.pid >> 24
                if t == 3:
                    a = o.fid & 0xFFF
                    art = walls[a] if a < len(walls) else ""
                    if pat.match(art):
                        cells[o.elevation][(o.tile % 200, o.tile // 200)] = art[:-4]
                elif t == 2:
                    pr = src.proto(o.pid)
                    if pr is not None and pr.subtype == 0:
                        doors[o.elevation].add((o.tile % 200, o.tile // 200))
            for e, c in cells.items():
                d = doors[e]
                for (x, y), art in c.items():
                    w, ea = (x - 1, y) in c, (x + 1, y) in c
                    n, s = (x, y - 1) in c, (x, y + 1) in c
                    if w or ea or ((x - 1, y) in d) or ((x + 1, y) in d):
                        lo = "g" if w else ("door" if (x - 1, y) in d else None)
                        hi = "g" if ea else ("door" if (x + 1, y) in d else None)
                        up = "g" if n else None
                        dn = "g" if (not s and (x, y + 2) in c) else None
                        xkey[f"{lo},{hi},{up},{dn}|p{x % 2}"][art] += 1
                        rows[y % 2] += 1
                        if lo == "g" and hi == "g" and not up and not dn:
                            xrun[x % 2][art] += 1
                    elif n or s or ((x, y - 1) in d) or ((x, y + 1) in d):
                        role = "top" if not n else ("bottom" if not s else "inside")
                        door = "door-above" if (x, y - 1) in d else ("door-below" if (x, y + 1) in d else "")
                        gap_above = (x, y - 2) in c and not n
                        ykey[f"{role}|{door}|{'gap' if gap_above else ''}|p{x % 2}"][art] += 1
                        cols[x % 2] += 1
    rep = [f"x-run rows by parity: {dict(rows)}; y-run columns by parity: {dict(cols)}", "", "x-run hexes (lo,hi,up,dn|x%2):"]
    for k, c in sorted(xkey.items(), key=lambda kv: -sum(kv[1].values())):
        if sum(c.values()) >= 3:
            rep.append(f"  {k:28s} {sum(c.values()):5d}  " + ", ".join(f"{a}x{n}" for a, n in c.most_common(8)))
    rep += ["", "y-run hexes (role|door|gap above|x%2):"]
    for k, c in sorted(ykey.items(), key=lambda kv: -sum(kv[1].values())):
        if sum(c.values()) >= 3:
            rep.append(f"  {k:32s} {sum(c.values()):5d}  " + ", ".join(f"{a}x{n}" for a, n in c.most_common(8)))
    rep += ["", "plain x-run pieces by x%2:"] + [f"  p{k}: " + ", ".join(f"{a}x{n}" for a, n in c.most_common(12)) for k, c in sorted(xrun.items())]
    (OUT / f"{fam}.txt").write_text("\n".join(rep), encoding="utf-8")
    (OUT / f"{fam}.json").write_text(json.dumps({"x": {k: c.most_common() for k, c in xkey.items()},
                                                 "y": {k: c.most_common() for k, c in ykey.items()},
                                                 "plain_x": {k: c.most_common() for k, c in xrun.items()}}, indent=1),
                                     encoding="utf-8")
    print("\n".join(rep))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
