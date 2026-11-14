#!/usr/bin/env python3
"""
shift_map.py - move everything on a map (every elevation) by an even number of
hexes, so a town that sits off to one side of the 200x200 grid ends up in the
middle, where its edge box (maps/<name>.edg) has painted ground all round.

    python tools/shift_map.py CLCOURT.MAP --dx -34 --dy 14 --preview build/shift
    python tools/shift_map.py CLCOURT.MAP --dx -34 --dy 14 --write

Owner, 2026-10-18: "you can move the court house to the center of the map so none
of it gets cut".

What moves (dx and dy must be even, so floor squares stay whole):
  * floor and roof squares, by (dx/2, dy/2) squares; squares pushed off the grid are
    dropped, and the ground-level squares opened up on the far side are filled with
    a copy of the map's own desert next to them (the way tools/paint_corners.py
    fills, so the ground keeps its pattern); upper and lower floors stay empty there
  * every object's hex; an object pushed off the grid is dropped (listed)
  * stairs and ladders on this map: their destination hex (object data, tile |
    elevation << 29, map -1)
  * the map's start hex
  * exit grids on OTHER maps that lead here: their destination hex
Script constants that name hexes on the map are not touched; the report lists the
offset to add (dy * 200 + dx). Both copies of every changed map are written (old
ones to _safety_backups/shift_map/); refuses while gecko or the game is running.
"""

from __future__ import annotations

import argparse
import math
import shutil
import struct
import subprocess
import sys
import time
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib.paths import bare_name, inside  # noqa: E402
from foclib import GameIndex, parse_lst  # noqa: E402
from foclib.formats import MapFile  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
AUTH = ROOT / "mod" / "data" / "maps"
DEPL = ROOT / "base" / "mods" / "foc" / "maps"
BACKUP = ROOT / "_safety_backups" / "shift_map"
W = 200
DESERT = ("edg", "edgs", "dearth")


def family(name):
    return name.lower().rsplit(".", 1)[0].rstrip("0123456789")


def map_ids():
    out = {}
    for line in (ROOT / "mod" / "ids" / "maps.txt").read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if parts and parts[0].isdigit():
            out[parts[1].upper()] = int(parts[0])
    return out


def fill_desert(fl, todo, tiles):
    """Fill squares `todo` of floor list `fl` with desert, CHUNK x CHUNK squares at a
    time starting next to the existing ground, each chunk a shifted copy of desert
    already there (fill_block)."""
    CHUNK = 10
    todo = set(todo)
    chunks = {}
    for s in todo:
        chunks.setdefault(((s % 100) // CHUNK, (s // 100) // CHUNK), set()).add(s)
    fills = {}
    shifts = Counter()
    work = list(fl)
    pending = dict(chunks)
    while pending:
        # the chunk with the most existing ground beside it goes next
        def contact(sqs):
            n = 0
            for a in sqs:
                c, r = a % 100, a // 100
                for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    b = (r + dr) * 100 + c + dc
                    if 0 <= c + dc < 100 and 0 <= r + dr < 100 and b not in todo and (work[b] & 0xFFF) > 1:
                        n += 1
            return n
        key = max(pending, key=lambda k: contact(pending[k]))
        sqs = pending.pop(key)
        res = fill_block(work, sqs, todo - sqs, tiles)
        if res is None:
            continue
        got, sh = res
        for s, v in got.items():
            work[s] = v
            fills[s] = v
        todo -= sqs
        shifts[sh] += 1
    if not fills:
        return None
    return fills, shifts.most_common(1)[0][0]


def fill_block(fl, todo, unfilled, tiles):
    """Fill squares `todo` of floor list `fl` with a shifted copy of nearby desert,
    the shift chosen by how alike its seam pairs are to the map's own (paint_corners).
    Squares in `unfilled` count as void."""
    painted = lambda s: (fl[s] & 0xFFF) > 1 and s not in todo and s not in unfilled  # noqa: E731
    pairs = Counter()
    for s in range(10000):
        if not painted(s):
            continue
        c, r = s % 100, s // 100
        if c + 1 < 100 and painted(s + 1):
            pairs[("h", fl[s] & 0xFFF, fl[s + 1] & 0xFFF)] += 1
        if r + 1 < 100 and painted(s + 100):
            pairs[("v", fl[s] & 0xFFF, fl[s + 100] & 0xFFF)] += 1
    total = sum(pairs.values())

    def desert(s):
        t = fl[s] & 0xFFF
        return t < len(tiles) and family(tiles[t]) in DESERT
    seam = []
    for a in todo:
        c, r = a % 100, a // 100
        for dc, dr, d, flip in ((1, 0, "h", False), (-1, 0, "h", True), (0, 1, "v", False), (0, -1, "v", True)):
            if 0 <= c + dc < 100 and 0 <= r + dr < 100:
                n = (r + dr) * 100 + c + dc
                if painted(n):
                    seam.append((a, n, d, flip))
    best = None
    for dr in range(-24, 25):
        for dc in range(-24, 25):
            if dc == 0 and dr == 0:
                continue
            ok = True
            for a in todo:
                c, r = a % 100 - dc, a // 100 - dr
                if not (0 <= c < 100 and 0 <= r < 100) or not painted(r * 100 + c) or not desert(r * 100 + c):
                    ok = False
                    break
            if not ok:
                continue
            sc = 0.0
            for a, n, d, flip in seam:
                src = fl[(a // 100 - dr) * 100 + a % 100 - dc] & 0xFFF
                nb = fl[n] & 0xFFF
                key = (d, nb, src) if flip else (d, src, nb)
                sc += math.log((pairs[key] + 0.1) / (total + 1))
            if best is None or sc > best[0]:
                best = (sc, dc, dr)
    if best is None:
        return None
    _, dc, dr = best
    return {a: fl[(a // 100 - dr) * 100 + a % 100 - dc] for a in todo}, (dc, dr)


def shift(name, dx, dy, gi, tiles):
    raw = (AUTH / name).read_bytes()
    m = MapFile.parse(raw, gi.proto)
    out = bytearray(raw)
    dc, dr = dx // 2, dy // 2
    report = []
    # --- tiles, per elevation
    for i, e in enumerate(m.elevations):
        base = m.tiles_offset + 40000 * i
        old_f, old_r = m.floors[i], m.roofs[i]
        new_f, new_r = [1] * 10000, [1] * 10000
        have = set()
        for s in range(10000):
            c, r = s % 100 + dc, s // 100 + dr
            if 0 <= c < 100 and 0 <= r < 100:
                n = r * 100 + c
                new_f[n], new_r[n] = old_f[s], old_r[s]
                have.add(n)
        lost = sum(1 for s in range(10000) if (old_f[s] & 0xFFF) > 1
                   and not (0 <= s % 100 + dc < 100 and 0 <= s // 100 + dr < 100))
        exposed = [s for s in range(10000) if s not in have]
        if e == 0:
            # the old map's own ragged edge can end up inside the floor: holes of
            # unpainted squares the grid's border can't reach get desert too
            empty = {s for s in range(10000) if (new_f[s] & 0xFFF) <= 1 or s not in have}
            outside = {s for s in empty if s % 100 in (0, 99) or s // 100 in (0, 99)} - set(exposed)
            stack = list(outside)
            while stack:
                a = stack.pop()
                c, r = a % 100, a // 100
                for dc2, dr2 in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    b = (r + dr2) * 100 + c + dc2
                    if 0 <= c + dc2 < 100 and 0 <= r + dr2 < 100 and b in empty and b not in outside and b not in exposed:
                        outside.add(b)
                        stack.append(b)
            holes = [s for s in empty if s not in outside and s not in exposed]
            exposed = exposed + holes
        note = f"e{e}: {lost} painted squares pushed off the grid"
        if e == 0 and exposed:
            res = fill_desert(new_f, set(exposed), tiles)
            if res:
                fills, (fc, fr) = res
                for s, v in fills.items():
                    new_f[s] = v
                note += f"; {len(fills)} new desert squares, copied from {fc:+d},{fr:+d} squares away"
            else:
                note += "; no desert to copy into the opened squares"
        report.append(note)
        struct.pack_into(">20000H", out, base, *[v for pair in zip(new_r, new_f) for v in pair])
    # --- objects
    dropped = Counter()
    keep = []
    for o in m.objects:
        _, end = MapFile._read_object(raw, o.off, gi.proto)
        rec = bytearray(raw[o.off:end])
        if 0 <= o.tile < W * W:
            x, y = o.tile % W + dx, o.tile // W + dy
            if not (0 <= x < W and 0 <= y < W):
                dropped[hex(o.pid)] += 1
                continue
            struct.pack_into(">i", rec, 4, y * W + x)
        pr = gi.proto(o.pid) if o.pid >> 24 == 2 else None
        if pr is not None and pr.subtype in (1, 3, 4):        # stairs, ladders: destination on this map
            # proto.cc objectDataRead: stairs are (tile, map), ladders (map, tile)
            t_off, m_off = (88, 92) if pr.subtype == 1 else (92, 88)
            dest = struct.unpack_from(">i", rec, t_off)[0]
            dmap = struct.unpack_from(">i", rec, m_off)[0]
            if dmap in (-1, 0) and dest > 0:
                t, el = dest & 0x1FFFFFFF, dest >> 29
                if 0 <= t < W * W:
                    nt = (t // W + dy) * W + t % W + dx
                    struct.pack_into(">i", rec, t_off, nt | (el << 29))
                    report.append(f"   stairs at e{o.elevation} ({o.tile % W},{o.tile // W}) now lead to e{el} ({nt % W},{nt // W})")
        keep.append((o.elevation, bytes(rec)))
    if dropped:
        report.append(f"   objects pushed off the grid and dropped: {dict(dropped)}")
    first = m.objects[0].off
    end_all = max(MapFile._read_object(raw, o.off, gi.proto)[1] for o in m.objects)
    body = bytearray()
    body += struct.pack(">i", len(keep))
    for e in (0, 1, 2):
        recs = [r for el, r in keep if el == e]
        body += struct.pack(">i", len(recs))
        for r in recs:
            body += r
    out = out[:first - 8] + body + out[end_all:]
    # --- start hex (header: version, name[16], then start tile at 0x14)
    st = struct.unpack_from(">i", out, 0x14)[0]
    if 0 <= st < W * W:
        ns = (st // W + dy) * W + st % W + dx
        struct.pack_into(">i", out, 0x14, ns)
        report.append(f"   start hex ({st % W},{st // W}) -> ({ns % W},{ns // W})")
    MapFile.parse(bytes(out), gi.proto)
    return bytes(out), report


def retarget(other, map_id, dx, dy, gi):
    raw = (AUTH / other).read_bytes()
    m = MapFile.parse(raw, gi.proto)
    out = bytearray(raw)
    n = 0
    for o in m.objects:
        if 0x05000010 <= o.pid <= 0x05000017:
            _, end = MapFile._read_object(raw, o.off, gi.proto)
            dm, dt = struct.unpack_from(">ii", raw, end - 16)
            if dm == map_id and 0 <= dt < W * W:
                struct.pack_into(">i", out, end - 12, (dt // W + dy) * W + dt % W + dx)
                n += 1
    return (bytes(out), n) if n else (None, 0)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("map")
    ap.add_argument("--dx", type=int, required=True)
    ap.add_argument("--dy", type=int, required=True)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--preview", metavar="DIR")
    g.add_argument("--write", action="store_true")
    args = ap.parse_args()
    args.map = bare_name(args.map)
    if args.dx % 2 or args.dy % 2:
        sys.exit("--dx and --dy must be even")
    if args.write:
        running = subprocess.run(["tasklist"], capture_output=True, text=True).stdout.lower()
        if "gecko" in running or "fallout2" in running:
            sys.exit("close gecko and the game first")
    gi = GameIndex(pristine=False)
    tiles = parse_lst(gi.vfs.read("art\\tiles\\tiles.lst"))
    names = [p.name for p in AUTH.iterdir() if p.suffix.lower() == ".map" and (DEPL / p.name).exists()]
    for n in names:
        if (DEPL / n).read_bytes() != (AUTH / n).read_bytes():
            sys.exit(f"{n}: the deployed copy differs from mod/data/maps; run tools/sync_maps.py first")
    data, report = shift(args.map, args.dx, args.dy, gi, tiles)
    print(args.map)
    for r in report:
        print(" ", r)
    map_id = map_ids()[args.map.upper()]
    changed = {args.map: data}
    for other in names:
        if other.upper() == args.map.upper():
            continue
        d, n = retarget(other, map_id, args.dx, args.dy, gi)
        if d:
            changed[other] = d
            print(f"  {other}: {n} exit grids into {args.map} retargeted")
    print(f"  script constants naming hexes on {args.map}: add {args.dy * W + args.dx}")
    if args.preview:
        d = inside(args.preview)
        d.mkdir(parents=True, exist_ok=True)  # NOSONAR - path checked by foclib.paths.inside()
        for n, b in changed.items():
            (d / f"new_{n}").write_bytes(b)
        print(f"--preview: test copies in {d}; nothing written")
        return 0
    BACKUP.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    for n, b in changed.items():
        shutil.copy2(AUTH / n, BACKUP / f"{n}.{stamp}")
        (AUTH / n).write_bytes(b)
        (DEPL / n).write_bytes(b)
        print(f"wrote {n}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
