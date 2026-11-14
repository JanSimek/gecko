#!/usr/bin/env python3
"""
map_link.py - link two maps with exit grids.

    python tools/map_link.py add-exits Clairemont.map --hexes 4-5,108-119 --to 211 --tile 23460 --rot 0
    python tools/map_link.py retarget CLDRIVEIN.MAP --to 210 --tile 22609 --rot 4
    python tools/map_link.py retarget Clairemont.map --hexes 168-180,106-122 --from -2 --to 212 --tile 28888
    add --write to write; without it the tool only reports.

An exit grid is a misc object (pid 0x05000010-17) whose record ends in four
big-endian int32s: destination map index, tile, elevation, rotation
(proto.cc objectDataRead). Map -2 is the world map, -1 the town map
(map.cc mapHandleTransition); anything else is a maps.txt id.

add-exits  appends a block of exit objects (hex x range, hex y range), each
           a copy of an exit already on the map with a fresh object id, new
           tile and the given destination. Hexes with no floor are skipped.
retarget   points the map's existing exits (optionally only those that now
           lead to --from, and/or only those inside --hexes) at a new
           destination.

Both copies of the map (mod/data/maps and base/mods/foc/maps) are written,
each after a <name>.<timestamp>.bak; the tool refuses if they differ (an
unsynced gecko save). A map that exists in only one of the two places is
written there and copied to the other.
"""

from __future__ import annotations

import argparse
import shutil
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib.paths import bare_name  # noqa: E402
from foclib import GameIndex, MapFile  # noqa: E402
from foclib.formats import is_exit_grid  # noqa: E402
from map_exits_to_edge import object_spans, OBJ_ID_OFF, OBJ_TILE_OFF  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
AUTHORED = ROOT / "mod" / "data" / "maps"
DEPLOYED = ROOT / "base" / "mods" / "foc" / "maps"
DEST = struct.Struct(">iiii")


def load(name: str) -> tuple[bytes, list[Path]]:
    name = bare_name(name)
    a, d = AUTHORED / name, DEPLOYED / name
    have = [p for p in (a, d) if p.is_file()]
    if not have:
        sys.exit(f"no such map: {name}")
    raws = {p.read_bytes() for p in have}
    if len(raws) > 1:
        sys.exit(f"{name}: mod/data and base/mods/foc copies differ (unsynced gecko edit?) - stopping")
    return raws.pop(), [a, d]


def save(raw: bytes, paths: list[Path]) -> None:
    stamp = time.strftime("%Y%m%d_%H%M%S")
    for p in paths:
        if p.is_file():
            shutil.copy2(p, p.with_name(f"{p.name}.{stamp}.bak"))
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(raw)  # NOSONAR - the map goes back to its own two copies
        print(f"  wrote {p.relative_to(ROOT)}")


def exits_with_dest(raw: bytes, m: MapFile, gi):
    spans, *_ = object_spans(raw, m, gi)
    out = []
    for o, s, e in spans:
        if is_exit_grid(o.pid):
            out.append((o, s, e, DEST.unpack_from(raw, e - 16)))
    return out


def cmd_add(args, gi) -> int:
    raw, paths = load(args.map)
    m = MapFile.parse(raw, gi.proto)
    x0, x1 = (int(v) for v in args.hexes.split(",")[0].split("-"))
    y0, y1 = (int(v) for v in args.hexes.split(",")[1].split("-"))
    ex = exits_with_dest(raw, m, gi)
    if not ex:
        sys.exit(f"{args.map} has no exit grid to copy")
    _o, ts, te, _d = ex[0]
    template = raw[ts:te]
    taken = {o.tile for o, *_ in ex}
    floors = m.floors[0]
    tiles = []
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            t = y * 200 + x
            if t in taken or floors[(y // 2) * 100 + (x // 2)] <= 1:
                continue
            tiles.append(t)
    print(f"{args.map}: {len(tiles)} new exits on hexes x {x0}-{x1}, y {y0}-{y1} -> "
          f"map {args.to} tile {args.tile} elev {args.elev} rot {args.rot}")
    if not args.write:
        return 0
    spans, total_off, e0_off, e0_end = object_spans(raw, m, gi)
    next_id = max(struct.unpack_from(">i", raw, o.off)[0] for o in m.objects) + 1
    new = []
    for i, t in enumerate(tiles):
        rec = bytearray(template)
        struct.pack_into(">i", rec, OBJ_ID_OFF, next_id + i)
        struct.pack_into(">i", rec, OBJ_TILE_OFF, t)
        DEST.pack_into(rec, len(rec) - 16, args.to, args.tile, args.elev, args.rot)
        new.append(bytes(rec))
    n0 = struct.unpack_from(">i", raw, e0_off)[0] + len(new)
    total = struct.unpack_from(">i", raw, total_off)[0] + len(new)
    out = bytearray(raw[:total_off]) + struct.pack(">ii", total, n0) + raw[e0_off + 4:e0_end] \
        + b"".join(new) + raw[e0_end:]
    m2 = MapFile.parse(bytes(out), gi.proto)
    assert len(m2.objects) == len(m.objects) + len(new)
    save(bytes(out), paths)
    return 0


def cmd_retarget(args, gi) -> int:
    raw, paths = load(args.map)
    m = MapFile.parse(raw, gi.proto)
    buf = bytearray(raw)
    n = 0
    box = None
    if args.hexes:
        x0, x1 = (int(v) for v in args.hexes.split(",")[0].split("-"))
        y0, y1 = (int(v) for v in args.hexes.split(",")[1].split("-"))
        box = (x0, x1, y0, y1)
    for o, s, e, (dmap, _t, _el, _r) in exits_with_dest(raw, m, gi):
        if args.from_ is not None and dmap != args.from_:
            continue
        if box and not (box[0] <= o.tile % 200 <= box[1] and box[2] <= o.tile // 200 <= box[3]):
            continue
        DEST.pack_into(buf, e - 16, args.to, args.tile, args.elev, args.rot)
        n += 1
    print(f"{args.map}: {n} exits -> map {args.to} tile {args.tile} elev {args.elev} rot {args.rot}")
    if args.write and n:
        MapFile.parse(bytes(buf), gi.proto)
        save(bytes(buf), paths)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("add-exits", "retarget"):
        p = sub.add_parser(name)
        p.add_argument("map")
        p.add_argument("--to", type=int, required=True, help="destination map index (maps.txt id)")
        p.add_argument("--tile", type=int, required=True, help="arrival hex (y*200+x)")
        p.add_argument("--elev", type=int, default=0)
        p.add_argument("--rot", type=int, default=0, help="facing on arrival, 0-5")
        p.add_argument("--write", action="store_true")
        if name == "add-exits":
            p.add_argument("--hexes", required=True, help="x0-x1,y0-y1")
        else:
            p.add_argument("--from", dest="from_", type=int, help="only exits that now lead to this map")
            p.add_argument("--hexes", help="only exits inside x0-x1,y0-y1")
    args = ap.parse_args()
    gi = GameIndex(pristine=False)
    return cmd_add(args, gi) if args.cmd == "add-exits" else cmd_retarget(args, gi)


if __name__ == "__main__":
    sys.exit(main())
