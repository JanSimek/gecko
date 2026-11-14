#!/usr/bin/env python3
"""
open_tiles.py - list open floor hexes in a hex rectangle of a FOC map: no
blocking object, no exit grid within 2 hexes, reachable on foot from the
map's start, at least --spacing hexes apart. For placing spawns.

    python tools/open_tiles.py RWCORE2.MAP 0 128 90 146 106 --count 12 --spacing 3
"""

from __future__ import annotations

import argparse
import struct
import sys
from collections import deque
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib import GameIndex  # noqa: E402

MAPS = Path(__file__).resolve().parent.parent / "base" / "mods" / "foc" / "maps"
W = 200
DIRS = {0: (-1, W - 1, W, W + 1, 1, -W), 1: (-W - 1, -1, W, 1, 1 - W, -W)}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("map")
    ap.add_argument("elev", type=int)
    ap.add_argument("x0", type=int)
    ap.add_argument("y0", type=int)
    ap.add_argument("x1", type=int)
    ap.add_argument("y1", type=int)
    ap.add_argument("--count", type=int, default=10)
    ap.add_argument("--spacing", type=int, default=2)
    ap.add_argument("--from", dest="start", type=int, help="reachability start tile (default: map start)")
    args = ap.parse_args()
    gi = GameIndex(pristine=False)
    p = next(q for q in MAPS.glob("*") if q.name.upper() == args.map.upper())
    raw = p.read_bytes()
    m = gi.load_map(raw)
    ei = m.elevations.index(args.elev)
    blocked, exits, occupied = set(), set(), set()
    for o in m.objects:
        if o.elevation != args.elev or not (0 <= o.tile < 40000):
            continue
        t = o.pid >> 24
        fl = struct.unpack_from(">I", raw, o.off + 0x24)[0]
        occupied.add(o.tile)
        if t == 5 and 0x05000010 <= o.pid <= 0x05000017:
            exits.add(o.tile)
        elif t in (2, 3) and not fl & 0x10:
            blocked.add(o.tile)

    def floor(t):
        return m.floors[ei][(t // W // 2) * 100 + (t % W) // 2] > 1

    start = args.start if args.start is not None else m.default_tile
    seen = {start}
    q = deque([start])
    while q:
        t = q.popleft()
        for d in DIRS[(t % W) & 1]:
            n = t + d
            if 0 <= n < 40000 and abs(n % W - t % W) <= 1 and n not in seen and n not in blocked and floor(n):
                seen.add(n)
                q.append(n)
    near_exit = {e + a * W + b for e in exits for a in range(-2, 3) for b in range(-2, 3)}
    out = []
    for y in range(args.y0, args.y1 + 1):
        for x in range(args.x0, args.x1 + 1):
            t = y * W + x
            if t not in seen or t in blocked or t in occupied or t in near_exit:
                continue
            if any(abs(t % W - o % W) < args.spacing and abs(t // W - o // W) < args.spacing for o in out):
                continue
            out.append(t)
            if len(out) >= args.count:
                break
        if len(out) >= args.count:
            break
    print(", ".join(str(t) for t in out))
    print(f"# {len(out)} tiles: " + " ".join(f"({t % W},{t // W})" for t in out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
