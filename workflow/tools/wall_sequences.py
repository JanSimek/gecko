#!/usr/bin/env python3
"""
wall_sequences.py - how the originals order the pieces of a wall set along a
run: for every x-run (along x, on one row) and y-run (along y, on one column)
of the set in every Fallout 1 and 2 map, the piece at each hex and the one
after it, so a builder can lay a run the way theirs are laid (owner,
2026-10-20: "the walls you laid out are very strange ... look at the original
maps to see how wall pieces are supposed to be placed and ordered").

    python tools/wall_sequences.py dv      -> build/wall_sequences/dv.json + report

Reports, for each piece: how often it is followed by each piece (x-runs: the
piece at x+1; y-runs: at y+1), the share of plain runs, the commonest runs of
detailed pieces between plain ones, and the order the pieces are written in
the map file (by hex index, by run, or by screen depth).
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
OUT = ROOT / "build" / "wall_sequences"


def main() -> int:
    fam = sys.argv[1]
    pat = re.compile(rf"^{re.escape(fam)}\d+[a-z]?\.frm$", re.I)
    OUT.mkdir(parents=True, exist_ok=True)
    nxt = {"x": defaultdict(Counter), "y": defaultdict(Counter)}
    motifs = {"x": Counter(), "y": Counter()}
    order = Counter()
    for sname in ("fo2", "fo1"):
        src = port.open_source(sname)
        walls = [x.split(",")[0].lower() for x in parse_lst(src.vfs.read("art\\walls\\walls.lst"))]
        for name in list_maps(src):
            try:
                m = MapFile.parse(src.vfs.read(f"maps\\{name}"), src.proto)
            except Exception:
                continue
            cells = defaultdict(dict)
            seq = defaultdict(list)
            for o in m.objects:
                if (o.pid >> 24) != 3 or not (0 <= o.tile < 40000):
                    continue
                art = walls[o.fid & 0xFFF] if (o.fid & 0xFFF) < len(walls) else ""
                if pat.match(art):
                    cells[o.elevation][(o.tile % 200, o.tile // 200)] = art[:-4]
                    seq[o.elevation].append(o.tile)
            for e, c in cells.items():
                # file order: is it sorted by hex index?
                s = seq[e]
                if len(s) > 20:
                    asc = sum(1 for a, b in zip(s, s[1:]) if b > a) / (len(s) - 1)
                    order["by hex index (ascending)" if asc > 0.9 else "descending" if asc < 0.1 else "mixed"] += 1
                for (x, y), art in c.items():
                    if (x + 1, y) in c and ((x - 1, y) in c or (x + 2, y) in c):
                        nxt["x"][art][c[(x + 1, y)]] += 1
                    if (x, y + 1) in c and not ((x + 1, y) in c or (x - 1, y) in c):
                        nxt["y"][art][c[(x, y + 1)]] += 1
                # motifs: the pieces between two plain ones along an x-run row
                rows = defaultdict(list)
                for (x, y), art in c.items():
                    rows[y].append((x, art))
                for y, items in rows.items():
                    items.sort()
                    run = []
                    for (x, art), nxt_item in zip(items, items[1:] + [(None, None)]):
                        run.append(art)
                        if nxt_item[0] != x + 1:
                            plain = {"dv1000", "dv1004"} if fam == "dv" else set()
                            cur = []
                            for a in run:
                                if a in plain:
                                    if cur:
                                        motifs["x"][" ".join(cur)] += 1
                                    cur = []
                                else:
                                    cur.append(a)
                            run = []
    rep = [f"file order of the set's pieces per level: {dict(order)}", "", "x-runs: piece -> the piece at x+1"]
    for art, c in sorted(nxt["x"].items(), key=lambda kv: -sum(kv[1].values()))[:40]:
        t = sum(c.values())
        rep.append(f"  {art:8s} {t:5d}  " + ", ".join(f"{a} {n / t:.2f}" for a, n in c.most_common(5)))
    rep += ["", "y-runs: piece -> the piece at y+1"]
    for art, c in sorted(nxt["y"].items(), key=lambda kv: -sum(kv[1].values()))[:30]:
        t = sum(c.values())
        rep.append(f"  {art:8s} {t:5d}  " + ", ".join(f"{a} {n / t:.2f}" for a, n in c.most_common(5)))
    rep += ["", "x-run motifs between plain pieces:"] + [f"  {m:40s} {n}" for m, n in motifs["x"].most_common(30)]
    (OUT / f"{fam}.txt").write_text("\n".join(rep), encoding="utf-8")
    (OUT / f"{fam}.json").write_text(json.dumps({k: {a: dict(c) for a, c in v.items()} for k, v in nxt.items()} |
                                                {"motifs": dict(motifs["x"].most_common(200))}, indent=1), encoding="utf-8")
    print("\n".join(rep))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
