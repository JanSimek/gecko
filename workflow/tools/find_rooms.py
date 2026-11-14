#!/usr/bin/env python3
"""
find_rooms.py - list the rooms of any source level, so compose_map.py can
lift single rooms instead of whole levels.

    python tools/find_rooms.py yesterday tbsrvc02 0 -o build/rooms/tbsrvc02_0.png
    python tools/find_rooms.py olympus wbbunker 0 --min 30

A room is a connected region of walkable hexes, where walls, blocking
scenery and doors are barriers and the level's background filler tile does
not count as floor. Each is printed with its bounding box in SQUARES, grown
by one square so the walls around it come along (the `rect` a compose lift
takes), its size, its doors and a few of its furnishings; the image numbers
them on the render. Read-only.
"""

from __future__ import annotations

import argparse
import sys
from collections import Counter, deque
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import port  # noqa: E402
import render  # noqa: E402
from foclib import GameIndex, MapFile, parse_lst  # noqa: E402
from foclib.formats import hex_xy, pid_type  # noqa: E402

W = 200
DIRS = {0: (-1, W - 1, W, W + 1, 1, -W), 1: (-W - 1, -1, W, 1, 1 - W, -W)}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("source")
    ap.add_argument("map")
    ap.add_argument("elev", type=int)
    ap.add_argument("-o", "--out")
    ap.add_argument("--min", type=int, default=24, help="smallest room, in hexes")
    args = ap.parse_args()
    gi = (GameIndex() if args.source == "fo2" else GameIndex(pristine=False) if args.source == "foc"
          else port.open_source(args.source))
    name = args.map if args.map.lower().endswith(".map") else args.map + ".map"
    m = MapFile.parse(gi.vfs.read(f"maps\\{name}"), gi.proto)
    ei = m.elevations.index(args.elev)
    floors = m.floors[ei]
    cnt = Counter(v & 0xFFF for v in floors if (v & 0xFFF) > 1)
    filler = None
    if cnt:
        tid, c = cnt.most_common(1)[0]
        if c > 0.4 * sum(cnt.values()):
            filler = tid
    sc = parse_lst(gi.vfs.read("art\\scenery\\scenery.lst"))
    blocked, doors = set(), {}
    things: dict[int, str] = {}
    for o in m.objects:
        if o.elevation != args.elev or not (0 <= o.tile < 40000):
            continue
        t = pid_type(o.pid)
        pr = gi.proto(o.pid)
        if t == 3:
            blocked.add(o.tile)
        elif t == 2 and pr is not None:
            if pr.subtype == 0:
                doors[o.tile] = sc[o.fid & 0xFFF] if (o.fid & 0xFFF) < len(sc) else "?"
                blocked.add(o.tile)
            elif not (pr.flags & 0x10):
                blocked.add(o.tile)
                things[o.tile] = sc[o.fid & 0xFFF] if (o.fid & 0xFFF) < len(sc) else "?"

    def floor_ok(t):
        x, y = t % W, t // W
        v = floors[(y // 2) * 100 + x // 2] & 0xFFF
        return v > 1 and v != filler

    seen, rooms = set(), []
    for start in range(40000):
        if start in seen or start in blocked or not floor_ok(start):
            continue
        comp, q = [], deque([start])
        seen.add(start)
        while q:
            t = q.popleft()
            comp.append(t)
            for d in DIRS[(t % W) & 1]:
                n = t + d
                if 0 <= n < 40000 and abs(n % W - t % W) <= 1 and n not in seen \
                        and n not in blocked and floor_ok(n):
                    seen.add(n)
                    q.append(n)
        if len(comp) >= args.min:
            rooms.append(comp)
    rooms.sort(key=lambda c: -len(c))
    out = []
    for i, comp in enumerate(rooms, 1):
        xs = [t % W for t in comp]
        ys = [t // W for t in comp]
        sx0, sy0, sx1, sy1 = min(xs) // 2 - 1, min(ys) // 2 - 1, max(xs) // 2 + 1, max(ys) // 2 + 1
        cs = set(comp)
        near = set()
        for t in comp:
            for d in DIRS[(t % W) & 1]:
                near.add(t + d)
        rd = [doors[t] for t in near if t in doors]
        furn = Counter(things[t] for t in near if t in things)
        print(f"room {i:2}: rect [{sx0}, {sy0}, {sx1}, {sy1}]  {len(comp)} hexes, "
              f"{len(rd)} doors, {', '.join(f'{k}x{v}' for k, v in furn.most_common(5))}")
        out.append((i, comp))
    if args.out:
        from PIL import ImageDraw
        img = render.render_map(gi, m, ei)
        pts = []
        for sq, tid in enumerate(floors):
            if tid > 1:
                pts.append(render.square_xy(sq))
        for o in m.objects:
            if o.elevation == args.elev and 0 <= o.tile < 40000:
                x, y = render.hex_xy_screen(o.tile)
                pts.append((x - 100, y - 200))
                pts.append((x + 100, y + 60))
        minx = min(p[0] for p in pts)
        miny = min(p[1] for p in pts)
        dr = ImageDraw.Draw(img)
        for i, comp in out:
            mid = sorted(comp)[len(comp) // 2]
            x, y = render.hex_xy_screen(mid)
            cx, cy = x + 16 - minx, y + 8 - miny
            dr.rectangle([cx - 14, cy - 10, cx + 14, cy + 10], fill=(0, 0, 0, 255))
            dr.text((cx - 8, cy - 7), str(i), fill=(255, 255, 0, 255))
        img = img.convert("RGB")
        img = img.resize((img.width // 2, img.height // 2))
        Path(args.out).parent.mkdir(parents=True, exist_ok=True)
        img.save(args.out)
        print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
