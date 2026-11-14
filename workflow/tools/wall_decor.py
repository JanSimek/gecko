#!/usr/bin/env python3
"""
wall_decor.py - where the originals hang things on their walls (vents, lights,
pipes, windows, signs, machines set into the wall): for each family of scenery
that shares a hex with a wall piece, which run it is on (x-run or y-run),
whether that is the side of the room the camera sees (an x-run with the room
below it, a y-run with the room to its west: a room's north and east walls),
the wall piece under it, the gap to the next piece of the same family along
the same run, and how many there are per wall hex.

    python tools/wall_decor.py fo2:GECKPWPL:0 fo2:GECKTUNL:0   -> build/wall_decor.json + report
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

ROOT = Path(__file__).resolve().parent.parent


def family(art):
    return re.sub(r"\d+[a-z]?$", "", art.lower().replace(".frm", ""))


def main() -> int:
    fam_runs = defaultdict(Counter)
    under = defaultdict(Counter)
    gaps = defaultdict(list)
    per = Counter()
    wall_hexes = 0
    names = {}
    for spec in sys.argv[1:]:
        sname, mname, elev = spec.split(":")
        elev = int(elev)
        src = port.open_source(sname)
        m = MapFile.parse(src.vfs.read(f"maps\\{mname}.map"), src.proto)
        scen = [x.split(",")[0].lower() for x in parse_lst(src.vfs.read("art\\scenery\\scenery.lst"))]
        walls_l = [x.split(",")[0].lower() for x in parse_lst(src.vfs.read("art\\walls\\walls.lst"))]
        t = src.vfs.read("text\\english\\game\\pro_scen.msg").decode("cp1252", "replace")
        msg = {int(a): b for a, b in re.findall(r"\{(\d+)\}\{[^}]*\}\{([^}]*)\}", t)}
        fl = m.floors[m.elevations.index(elev)]
        walls = {}
        for o in m.objects:
            if o.elevation == elev and (o.pid >> 24) == 3 and 0 <= o.tile < 40000:
                walls[(o.tile % 200, o.tile // 200)] = walls_l[o.fid & 0xFFF][:-4]
        wall_hexes += sum(1 for a in walls.values() if not a.startswith("block"))

        def floored(x, y):
            return (fl[(y // 2) * 100 + x // 2] & 0xFFF) > 1
        on = defaultdict(list)
        for o in m.objects:
            if o.elevation != elev or (o.pid >> 24) != 2 or not (0 <= o.tile < 40000):
                continue
            x, y = o.tile % 200, o.tile // 200
            if (x, y) not in walls:
                continue
            pr = src.proto(o.pid)
            if pr is not None and pr.subtype == 0:
                continue
            art = scen[o.fid & 0xFFF]
            f = family(art)
            names.setdefault(f, msg.get(pr.message_id, "") if pr else "")
            xrun = (x - 1, y) in walls or (x + 1, y) in walls
            if xrun:
                side = "x-run, room below (north wall)" if floored(x, y + 2) and not (x, y + 2) in walls else "x-run, room above (south wall)"
            else:
                side = "y-run, room to the west (east wall)" if floored(x + 2, y) else "y-run, room to the east (west wall)"
            fam_runs[f][side] += 1
            under[f][walls[(x, y)]] += 1
            per[f] += 1
            on[(f, "x" if xrun else "y", y if xrun else x)].append(x if xrun else y)
        for (f, kind, line), pos in on.items():
            pos.sort()
            gaps[f] += [b - a for a, b in zip(pos, pos[1:])]
    rep = [f"wall hexes surveyed: {wall_hexes}"]
    for f, n in per.most_common(25):
        g = sorted(gaps[f])
        rep.append(f"{f:10s} {n:4d} ({names.get(f, '')[:22]}): one per {wall_hexes / n:.0f} wall hexes; "
                   + ", ".join(f"{k} {v}" for k, v in fam_runs[f].most_common())
                   + f"; on {', '.join(a for a, _ in under[f].most_common(4))}"
                   + (f"; gap along a run median {g[len(g) // 2]} (min {g[0]})" if g else ""))
    out = ROOT / "build" / "wall_decor.json"
    out.write_text(json.dumps({f: dict(count=per[f], runs=fam_runs[f], under=under[f].most_common(8),
                                       gaps=sorted(gaps[f])) for f in per}, indent=1), encoding="utf-8")
    print("\n".join(rep))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
