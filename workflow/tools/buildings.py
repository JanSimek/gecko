#!/usr/bin/env python3
"""
buildings.py - extract WHOLE buildings as prefabs.

    python tools/buildings.py            index every building in the base
    python tools/buildings.py --list

The corpus cuts maps into fixed 14x14 windows, which is fine for terrain and
useless for architecture: a window slices a building in half and you get random
parts. A building is a unit - walls, doorway, the floor inside, the roof over
it, and the furniture in it - and it is only useful if it comes across whole.

HOW A BUILDING IS FOUND
-----------------------
By its ROOF. In Fallout 2 a roof tile marks a structure the player walks under,
so connected components of roofed squares are exactly the buildings: Modoc has
8, Klamath 19, Broken Hills 13, NCR 17, at 60-230 squares each. Take each
component's bounding box plus a margin for the outer wall line, and lift the
floor, the roof and every object inside it.

Output matches the corpus fragment shape, so the editor can place either.
"""

from __future__ import annotations

import argparse
import collections
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib import (  # noqa: E402
    OBJ_TYPE_CRITTER,
    OBJ_TYPE_SCENERY,
    OBJ_TYPE_WALL,
    GameIndex,
    TYPE_NAME,
    hex_xy,
    parse_lst,
    pid_type,
)

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "corpus" / "buildings.jsonl"

MIN_ROOF = 24            # smaller is an awning or a bit of overhang
MAX_ROOF = 900
MARGIN = 2               # squares, so the outer wall line and door come too


def roof_components(m, ei: int):
    roofs = m.roofs[ei]
    seen = bytearray(10000)
    out = []
    for start in range(10000):
        if seen[start] or roofs[start] <= 1:
            continue
        stack, comp = [start], []
        seen[start] = 1
        while stack:
            sq = stack.pop()
            comp.append(sq)
            x, y = sq % 100, sq // 100
            for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if 0 <= nx < 100 and 0 <= ny < 100:
                    k = ny * 100 + nx
                    if not seen[k] and roofs[k] > 1:
                        seen[k] = 1
                        stack.append(k)
        out.append(comp)
    return out


def extract_map(gi: GameIndex, game_path: str, tiles, art_lists, out) -> int:
    try:
        m = gi.load_map(gi.vfs.read(game_path))
    except Exception:
        return 0

    made = 0
    for ei, elev in enumerate(m.elevations):
        objs = [o for o in m.objects
                if o.elevation == elev and 0 <= o.tile < 40000]
        by_sq = collections.defaultdict(list)
        for o in objs:
            hx, hy = hex_xy(o.tile)
            by_sq[(hy // 2) * 100 + (hx // 2)].append(o)

        for comp in roof_components(m, ei):
            if not (MIN_ROOF <= len(comp) <= MAX_ROOF):
                continue
            xs = [c % 100 for c in comp]
            ys = [c // 100 for c in comp]
            gx0 = max(0, min(xs) - MARGIN)
            gy0 = max(0, min(ys) - MARGIN)
            gx1 = min(99, max(xs) + MARGIN)
            gy1 = min(99, max(ys) + MARGIN)
            gw, gh = gx1 - gx0 + 1, gy1 - gy0 + 1
            if gw < 4 or gh < 4 or gw > 44 or gh > 44:
                continue

            palette, pal_index = [], {}

            def pack(grid):
                cells = []
                for yy in range(gy0, gy1 + 1):
                    row = []
                    for xx in range(gx0, gx1 + 1):
                        tid = grid[yy * 100 + xx]
                        n = tiles[tid] if 1 < tid < len(tiles) and tiles[tid] else None
                        if n is None:
                            row.append(-1)
                            continue
                        if n not in pal_index:
                            pal_index[n] = len(palette)
                            palette.append(n)
                        row.append(pal_index[n])
                    cells.append(row)
                return cells

            floor_cells = pack(m.floors[ei])
            roof_cells = pack(m.roofs[ei])

            recs, counts, tags = [], collections.Counter(), set()
            for yy in range(gy0, gy1 + 1):
                for xx in range(gx0, gx1 + 1):
                    for o in by_sq.get(yy * 100 + xx, ()):
                        t = pid_type(o.pid)
                        if t not in TYPE_NAME:
                            continue
                        lst = art_lists.get(t, [])
                        i = o.fid & 0xFFF
                        art = lst[i] if 0 <= i < len(lst) else ""
                        hx, hy = hex_xy(o.tile)
                        recs.append({
                            "t": t, "art": art,
                            "proto": gi.proto_filename(o.pid),
                            "pid": o.pid,
                            "dx": hx - gx0 * 2, "dy": hy - gy0 * 2,
                            "rot": o.rotation, "frm": o.frame,
                        })
                        counts[TYPE_NAME[t]] += 1
                        a = (art or "").lower()
                        if "door" in a:
                            tags.add("door")
                        if "stair" in a:
                            tags.add("stairs")
            if counts.get("critters"):
                tags.add("critters")
            if counts.get("items"):
                tags.add("items")
            if counts.get("walls", 0) < 6:
                continue                      # a roof with no walls is not a building

            tags.add("building")
            bid = (f"bld:{Path(game_path).stem.lower()}:e{elev}:{gx0}_{gy0}")
            out.write(json.dumps({
                "id": bid, "source": "fo2", "map": game_path,
                "elevation": elev, "origin_sq": [gx0, gy0],
                "size_sq": [gw, gh], "roof_squares": len(comp),
                "kind": "building", "tileset": "",
                "tags": sorted(tags),
                "objects_by_type": dict(counts),
                "object_count": len(recs),
                "palette": palette, "floor": floor_cells, "roof": roof_cells,
                "objects": recs,
            }, separators=(",", ":")) + "\n")
            made += 1
    return made


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()

    if args.list:
        if not OUT.is_file():
            sys.exit("no buildings index - run without --list first")
        rows = [json.loads(l) for l in OUT.open(encoding="utf-8")]
        rows.sort(key=lambda r: -r["object_count"])
        for r in rows[:40]:
            w, h = r["size_sq"]
            print(f"  {r['id']:34s} {w:2d}x{h:<2d} obj={r['object_count']:<4d} "
                  f"roof={r['roof_squares']:<4d} {','.join(r['tags'])}")
        print(f"({len(rows)} buildings)")
        return 0

    gi = GameIndex()
    tiles = parse_lst(gi.vfs.read("art/tiles/tiles.lst"))
    art_lists = {t: parse_lst(gi.vfs.read(f"art/{d}/{d}.lst"))
                 for t, d in TYPE_NAME.items() if d != "tiles"}

    names = sorted({n for p in gi.vfs.providers if p.kind == "dat"
                    for n in p.dat.names() if n.lower().endswith(".map")},
                   key=str.lower)
    uniq = {}
    for n in names:
        uniq.setdefault(n.lower(), n)
    names = sorted(uniq.values(), key=str.lower)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    total = 0
    with OUT.open("w", encoding="utf-8") as f:
        for n in names:
            total += extract_map(gi, n, tiles, art_lists, f)
    print(f"{total} whole buildings from {len(names)} maps")
    print(f"  {OUT.relative_to(ROOT)} ({OUT.stat().st_size/1048576:.1f} MB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
