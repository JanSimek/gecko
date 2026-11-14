#!/usr/bin/env python3
"""
map_edge_blockers.py - stop the player walking into the black edge of a map.

    python tools/map_edge_blockers.py RWRUINS.MAP --dry-run
    python tools/map_edge_blockers.py --all

A map's edge is framed with RPU's black screen rectangles, scenery BLACK002
(pid 0x020008F8) and BLACK003 (0x020008F9). They are NoBlock, so the player
walks straight into the black. This tool works out which hexes each piece
covers on screen (a hex is covered when its centre is inside the sprite, by
object.cc's draw maths, as car_blockers.py does it) and puts a Secret Blocking
Hex (scenery 67) on the covered hexes that border the playable side (the
hexes reached from the map's start or its exit grids). Every way into the
black crosses one of those, so that ring is enough; the black interior and
its far side stay empty.

Hexes holding an exit grid or a blocking object are left alone, and so are
covered hexes next to an exit grid, so a grid the owner put at the edge still
works. Like map_vehicle_blockers.py the records are appended byte for byte,
running it twice adds nothing, both map copies are written when they match,
and the old file goes to _safety_backups/edge_blockers/.
"""

from __future__ import annotations

import argparse
import filecmp
import shutil
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib import GameIndex  # noqa: E402
from foclib.formats import parse_lst  # noqa: E402
import car_blockers as cb  # noqa: E402
import map_vehicle_blockers as mvb  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
BACKUP = ROOT / "_safety_backups" / "edge_blockers"

BLACK_PIDS = (0x020008F8, 0x020008F9)
FLAGS_OFF = 0x24
NO_BLOCK = 0x10
W = 200
DIRS = {0: (-1, W - 1, W, W + 1, 1, -W), 1: (-W - 1, -1, W, 1, 1 - W, -W)}


def neighbours(t: int):
    x = t % W
    for d in DIRS[x & 1]:
        n = t + d
        if 0 <= n < 40000 and abs(n % W - x) <= 1:
            yield n


def sprite_rect(o, gi, art, cache):
    idx = o.fid & 0xFFF
    name = art[idx].strip().lower()
    if name not in cache:
        cache[name] = gi.vfs.read(f"art/scenery/{name}")
    w, h, ox, oy = cb.frm_geometry(cache[name], o.rotation)
    hx, hy = cb.hex_screen(o.tile)
    left = hx + 16 + ox - w // 2
    bottom = hy + 8 + oy + 1
    return left, bottom - h, left + w, bottom


def plan(raw: bytes, m, gi, art):
    cache: dict = {}
    rects: dict[int, list] = {}
    for o in m.objects:
        if o.pid in BLACK_PIDS and 0 <= o.tile < 40000:
            rects.setdefault(o.elevation, []).append(sprite_rect(o, gi, art, cache))
    exits: dict[int, set] = {}
    solid: dict[int, set] = {}
    for o in m.objects:
        if not (0 <= o.tile < 40000):
            continue
        t = o.pid >> 24
        if t == 5:
            exits.setdefault(o.elevation, set()).add(o.tile)
        elif o.pid == cb.BLOCKING_HEX_PID or (
                t in (2, 3) and not struct.unpack_from(">I", raw, o.off + FLAGS_OFF)[0] & NO_BLOCK):
            solid.setdefault(o.elevation, set()).add(o.tile)
    centres = [(t, cb.hex_screen(t)) for t in range(40000)]
    want: dict[int, set] = {}
    for e, rs in rects.items():
        covered = set()
        for t, (px, py) in centres:
            cx, cy = px + 16, py + 8
            for l, tp, r, b in rs:
                if l <= cx < r and tp <= cy < b:
                    covered.add(t)
                    break
        ex = exits.get(e, set())
        sol = solid.get(e, set())
        # the playable side: everything the player reaches from the start or
        # an exit grid without crossing the black or a solid object
        seen = {t for t in ([m.default_tile] if e == m.default_elevation else []) + sorted(ex)
                if t not in covered}
        todo = list(seen)
        while todo:
            t = todo.pop()
            for n in neighbours(t):
                if n not in seen and n not in covered and n not in sol:
                    seen.add(n)
                    todo.append(n)
        ring = {t for t in covered if any(n in seen for n in neighbours(t))}
        near_exit = ex | {n for t in ex for n in neighbours(t)}
        want[e] = ring - near_exit - sol
    return want, {e: len(r) for e, r in rects.items()}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("map", nargs="?")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    gi = GameIndex(pristine=False)
    art = parse_lst(gi.vfs.read(r"art\scenery\scenery.lst"))
    rec = mvb.template_record(gi)
    maps = ([p.name for p in sorted(mvb.DEPLOYED.glob("*")) if p.suffix.lower() == ".map"]
            if args.all else [args.map])
    for name in maps:
        dep, aut = mvb.DEPLOYED / name, mvb.AUTHORED / name
        if aut.exists() and not filecmp.cmp(dep, aut, shallow=False):
            print(f"{name}: the two copies differ - run map_fixup.py on it first; skipped")
            continue
        raw = dep.read_bytes()
        m = gi.load_map(raw)
        want, pieces = plan(raw, m, gi, art)
        n = sum(len(v) for v in want.values())
        print(f"{name}: {sum(pieces.values())} black pieces, {n} new blocking hexes")
        if args.dry_run or n == 0:
            continue
        out = mvb.apply(raw, m, gi, want, rec)
        assert len(gi.load_map(out).objects) == len(m.objects) + n
        BACKUP.mkdir(parents=True, exist_ok=True)
        shutil.copy2(dep, BACKUP / f"{name}.{time.strftime('%Y%m%d_%H%M%S')}")
        dep.write_bytes(out)
        if aut.exists():
            aut.write_bytes(out)
        print(f"   wrote {name} (old copy in {BACKUP.relative_to(ROOT)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
