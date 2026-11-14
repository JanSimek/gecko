#!/usr/bin/env python3
"""
check_links.py - check every way into and out of FOC maps.

    python tools/check_links.py RWFAC1.MAP RWFAC2.MAP RWFAC3.MAP
    python tools/check_links.py --all

For each map: every exit grid's destination tile must be on floor, not on a
blocking object, and not on (or next to) an exit grid of the destination
map; and every exit grid group must be reachable on foot from at least one
arrival tile (the map's start, or a tile some exit grid sends the player
to). Extra arrivals that scripts use can be given with --arrive MAP:ELEV:TILE.
Read-only.
"""

from __future__ import annotations

import argparse
import struct
import sys
from collections import deque, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib import GameIndex  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
MAPS = ROOT / "base" / "mods" / "foc" / "maps"
W = 200
DIRS = {0: (-1, W - 1, W, W + 1, 1, -W), 1: (-W - 1, -1, W, 1, 1 - W, -W)}


def map_ids() -> dict[int, str]:
    out = {}
    for line in (ROOT / "mod" / "ids" / "maps.txt").read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if parts and parts[0].isdigit():
            out[int(parts[0])] = parts[1].upper()
    return out


def load(gi, name):
    raw = (MAPS / name).read_bytes()
    m = gi.load_map(raw)
    info = {"m": m, "raw": raw, "blocked": defaultdict(set), "exits": defaultdict(list)}
    for o in m.objects:
        if not (0 <= o.tile < 40000):
            continue
        t = o.pid >> 24
        flags = struct.unpack_from(">I", raw, o.off + 0x24)[0]
        if t == 5 and 0x05000010 <= o.pid <= 0x05000017:
            d = struct.unpack_from(">4i", raw, o.off + 72 + 12 + 4)
            info["exits"][o.elevation].append((o.tile, d[0], d[1], d[2]))
        elif t in (2, 3) and not flags & 0x10:
            pr = gi.proto(o.pid) if t == 2 else None
            if pr is not None and pr.subtype == 0:      # a door: the player opens it
                continue
            info["blocked"][o.elevation].add(o.tile)
    return info


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("maps", nargs="*")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--arrive", action="append", default=[])
    args = ap.parse_args()
    gi = GameIndex(pristine=False)
    ids = map_ids()
    by_name = {v: k for k, v in ids.items()}
    names = [p.name for p in sorted(MAPS.glob("*")) if p.suffix.lower() == ".map"] if args.all else args.maps
    cache = {}

    def get(n):
        n = n.upper()
        if n not in cache:
            p = next((q for q in MAPS.glob("*") if q.name.upper() == n), None)
            cache[n] = load(gi, p.name) if p else None
        return cache[n]

    arrivals = defaultdict(set)            # (MAP, elev) -> tiles
    problems = 0
    for n in names:
        info = get(n)
        for e, exs in info["exits"].items():
            for _t, dm, dt, de in exs:
                if dm >= 0 and dm in ids and dt >= 0:
                    arrivals[(ids[dm], de)].add(dt)
    for a in args.arrive:
        mp, e, t = a.split(":")
        arrivals[(mp.upper(), int(e))].add(int(t))
    for n in names:
        info = get(n)
        m = info["m"]
        arrivals[(n.upper(), m.default_elevation)].add(m.default_tile)
    for n in names:
        info = get(n)
        m = info["m"]
        print(f"== {n}")
        for e in m.elevations:
            ei = m.elevations.index(e)
            blocked = info["blocked"][e]
            exits = {t for t, *_ in info["exits"][e]}

            def floor(t):
                return m.floors[ei][(t // W // 2) * 100 + (t % W) // 2] > 1
            arr = arrivals.get((n.upper(), e), set())
            for t in sorted(arr):
                bad = []
                if not floor(t):
                    bad.append("no floor")
                if t in blocked:
                    bad.append("blocked")
                near = [x for x in exits if x == t or x in {t + d for d in DIRS[(t % W) & 1]}]
                if near:
                    bad.append("on/next to an exit grid")
                hint = ""
                if bad:
                    problems += 1
                    # nearest open floor hex, by ring search
                    best = None
                    for r in range(1, 12):
                        for y in range(t // W - r, t // W + r + 1):
                            for x in range(t % W - r, t % W + r + 1):
                                c = y * W + x
                                if 0 <= c < 40000 and floor(c) and c not in blocked and c not in exits:
                                    best = c
                                    break
                            if best:
                                break
                        if best:
                            break
                    if best:
                        hint = f"  (nearest open: {best} = ({best % W},{best // W}))"
                print(f"   elev {e} arrival {t} ({t % W},{t // W}): {'OK' if not bad else ', '.join(bad)}{hint}")
            seen = set()
            for s in arr:
                if s in seen or not floor(s):
                    continue
                q = deque([s])
                seen.add(s)
                while q:
                    t = q.popleft()
                    if t in exits and t != s:
                        continue
                    for d in DIRS[(t % W) & 1]:
                        x = t + d
                        if 0 <= x < 40000 and abs(x % W - t % W) <= 1 and x not in seen \
                                and x not in blocked and floor(x):
                            seen.add(x)
                            q.append(x)
            groups = defaultdict(list)
            for t, dm, dt, de in info["exits"][e]:
                groups[(dm, dt, de)].append(t)
            for (dm, dt, de), ts in groups.items():
                reach = sum(t in seen for t in ts)
                dest = "worldmap" if dm == -2 else ids.get(dm, dm)
                ok = reach > 0
                if not ok:
                    problems += 1
                extra = ""
                if not ok and seen:
                    d, a, b = min(((abs(t % W - x % W) + abs(t // W - x // W)), t, x) for t in ts for x in seen)
                    extra = f" (closest reachable hex {b} = ({b % W},{b // W}), {d} hexes from exit hex ({a % W},{a // W}))"
                print(f"   elev {e} exits -> {dest} elev {de} tile {dt}: {len(ts)} hexes, "
                      f"{reach} reachable{'' if ok else '  <-- UNREACHABLE' + extra}")
    print(f"{problems} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
