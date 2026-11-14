#!/usr/bin/env python3
"""
nec_corners.py - how Fallout 1 and Fallout 2 join the nec wall set (the
concrete-and-brick walls the owner's courthouse uses) at ends, corners,
T-junctions and crossings, measured over every map of both games.

    python tools/nec_corners.py            -> build/nec_corners/{grammar.json,report.txt}
    python tools/nec_corners.py --family ca --out build/ca_corners    # another wall set, e.g. the cave rock

A wall hex's signature is which of its four in-line neighbours (x-1, x+1,
y-1, y+1) hold another nec piece, plus which of the four diagonals do when
an in-line neighbour is missing (corners in a hex grid are often joined
across a diagonal). For every signature and hex-column parity the report
lists the pieces the originals use, and for each corner and end piece the
pieces that sit on either side of it, so a corner can be matched to the run
it closes (grey concrete or red brick, inside or outside face).
"""

from __future__ import annotations

import argparse
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
OUT = ROOT / "build" / "nec_corners"
NEC = re.compile(r"^nec(\d+)\.frm$", re.I)


def main() -> int:
    global NEC, OUT
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--family", default="nec", help="wall art name stem: <stem><digits>.frm")
    ap.add_argument("--out", type=Path, default=OUT)
    a = ap.parse_args()
    NEC = re.compile(rf"^{re.escape(a.family)}(\d+)\.frm$", re.I)
    OUT = a.out if a.out.is_absolute() else ROOT / a.out
    OUT.mkdir(parents=True, exist_ok=True)
    gram = defaultdict(Counter)            # (sig, xpar) -> piece counts
    nbrs = defaultdict(lambda: defaultdict(Counter))   # piece -> direction -> neighbour piece counts
    examples = defaultdict(list)
    for sname in ("fo2", "fo1"):
        src = port.open_source(sname)
        walls = [x.split(",")[0].lower() for x in parse_lst(src.vfs.read("art\\walls\\walls.lst"))]
        for name in list_maps(src):
            try:
                m = MapFile.parse(src.vfs.read(f"maps\\{name}"), src.proto)
            except Exception:
                continue
            cells = defaultdict(dict)
            for o in m.objects:
                if (o.pid >> 24) != 3 or not (0 <= o.tile < 40000):
                    continue
                art = walls[o.fid & 0xFFF] if (o.fid & 0xFFF) < len(walls) else ""
                if NEC.match(art):
                    cells[o.elevation][(o.tile % 200, o.tile // 200)] = art.replace(".frm", "")
            for elev, c in cells.items():
                for (x, y), art in c.items():
                    s = "".join(k if (x + dx, y + dy) in c else "." for k, dx, dy in
                                (("W", -1, 0), ("E", 1, 0), ("N", 0, -1), ("S", 0, 1)))
                    diag = "".join(k if (x + dx, y + dy) in c else "." for k, dx, dy in
                                   (("a", -1, -1), ("b", 1, -1), ("c", -1, 1), ("d", 1, 1)))
                    key = f"{s}|{diag}|p{x % 2}"
                    gram[key][art] += 1
                    for k, dx, dy in (("W", -1, 0), ("E", 1, 0), ("N", 0, -1), ("S", 0, 1),
                                      ("NW", -1, -1), ("NE", 1, -1), ("SW", -1, 1), ("SE", 1, 1)):
                        if (x + dx, y + dy) in c:
                            nbrs[art][k][c[(x + dx, y + dy)]] += 1
                    if s.count(".") >= 2 and len(examples[key]) < 4:
                        examples[key].append(f"{sname}/{name} e{elev} ({x},{y}) {art}")
    lines = []
    for key in sorted(gram, key=lambda k: -sum(gram[k].values())):
        tot = sum(gram[key].values())
        if tot < 3:
            continue
        lines.append(f"{key:22s} {tot:5d}  " + ", ".join(f"{a}x{n}" for a, n in gram[key].most_common(10)))
    (OUT / "report.txt").write_text("\n".join(lines), encoding="utf-8")
    (OUT / "grammar.json").write_text(json.dumps({
        "grammar": {k: v.most_common() for k, v in gram.items()},
        "neighbours": {a: {d: c.most_common(8) for d, c in v.items()} for a, v in nbrs.items()},
        "examples": examples}, indent=1), encoding="utf-8")
    print(f"{len(gram)} signatures; wrote {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
