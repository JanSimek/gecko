#!/usr/bin/env python3
"""
check_wall_sequences.py - check a FOC map's walls of one set against the
originals' sequences (tools/wall_sequences.py's build/wall_sequences/<set>.json):
every pair of neighbouring pieces along an x-run (x, x+1) and a y-run (y, y+1)
should be a pair the originals use somewhere. Lists the ones they never use.

    python tools/check_wall_sequences.py build/rwfac1/RWFAC1.MAP dv
"""

from __future__ import annotations

import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib.paths import bare_name, inside  # noqa: E402
from foclib import GameIndex, parse_lst  # noqa: E402
from foclib.formats import MapFile  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    path, fam = inside(sys.argv[1]), bare_name(sys.argv[2])
    ref = json.loads((ROOT / "build" / "wall_sequences" / f"{fam}.json").read_text(encoding="utf-8"))
    gi = GameIndex(pristine=False)
    walls = [x.split(",")[0].lower()[:-4] for x in parse_lst(gi.vfs.read("art\\walls\\walls.lst"))]
    m = MapFile.parse(path.read_bytes(), gi.proto)  # NOSONAR - path checked by foclib.paths.inside()
    pat = re.compile(rf"^{re.escape(fam)}\d+[a-z]?$")
    cells = defaultdict(dict)
    for o in m.objects:
        if (o.pid >> 24) == 3:
            a = walls[o.fid & 0xFFFFFF] if (o.fid & 0xFFFFFF) < len(walls) else ""
            if pat.match(a):
                cells[o.elevation][(o.tile % 200, o.tile // 200)] = a
    bad = Counter()
    total = 0
    for e, c in cells.items():
        for (x, y), a in c.items():
            if (x + 1, y) in c and ((x - 1, y) in c or (x + 2, y) in c):
                total += 1
                if c[(x + 1, y)] not in ref["x"].get(a, {}):
                    bad[("x", a, c[(x + 1, y)])] += 1
            if (x, y + 1) in c and not ((x + 1, y) in c or (x - 1, y) in c):
                total += 1
                if c[(x, y + 1)] not in ref["y"].get(a, {}):
                    bad[("y", a, c[(x, y + 1)])] += 1
    print(f"{path.name}: {total} neighbouring pairs, {sum(bad.values())} the originals never use")
    for (k, a, b), n in bad.most_common(30):
        print(f"   {k}-run {a} -> {b}: {n}")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
