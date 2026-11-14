#!/usr/bin/env python3
"""
bind_one.py - bind a script to one specific object on a map, by tile.

    python tools/bind_one.py --tile 10896 --pid 0x000000F5 --script 1610

--script is the 1-based scripts.lst line (matches the SCRIPT_* defines in
foc_ids.h directly); this converts to the engine's 0-based index itself.

Every object's EXISTING script binding is preserved exactly - each is
re-registered into the rebuilt script table with its own fresh SID (a script
table entry is a script INSTANCE, with its own local-var storage, so a
shared program index still needs one entry per object, matching how
objectLoadAllInternal expects one sid per object). Only the target object at
--tile (cross-checked against --pid so a stray duplicate at the same hex
does not get bound by mistake) gets the new binding.

WHY A ROUND-TRIP. objectLoadAllInternal looks a script up by the object's
SID in the map's script table and then overwrites scriptIndex from it, so
the table and the SID have to exist and agree - the table is a whole map
section, so adding one entry means rewriting the file.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib import (  # noqa: E402
    OBJ_TYPE_CRITTER,
    GameIndex,
    MapFile,
    hex_xy,
    pid_type,
)
from mapbuild import MapBuilder, MapObjectSpec, extras_for  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
SCRIPT_TYPE_SCENERY = 3   # items and scenery share this script-table section
SCRIPT_TYPE_CRITTER = 4


def read_critter_extras(data: bytes, off: int):
    """A critter's own combat block, copied verbatim so it keeps the proto's
    hp rather than defaulting to 0 - a critter with hp=0 renders as a corpse."""
    base = off + 72 + 12
    return struct.unpack_from(">10i", data, base + 4)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--map", default="mod/data/maps/REDWELL.MAP")
    ap.add_argument("--tile", type=int, required=True)
    ap.add_argument("--pid", type=lambda s: int(s, 0), required=True,
                    help="the object's pid, e.g. 0x000000F5 - cross-checked "
                         "against --tile so the right object is bound")
    ap.add_argument("--script", type=int, required=True,
                    help="1-based scripts.lst line, i.e. the SCRIPT_* define")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    gi = GameIndex()
    path = ROOT / args.map
    raw = path.read_bytes()
    m = MapFile.parse(raw, gi.proto)

    target = next((o for o in m.objects
                   if o.tile == args.tile and o.pid == args.pid), None)
    if target is None:
        candidates = [o for o in m.objects if o.tile == args.tile]
        sys.exit(f"no object with pid 0x{args.pid:08X} at tile {args.tile}. "
                 f"objects actually at that tile: "
                 f"{[hex(o.pid) for o in candidates] or 'none'}")

    hx, hy = hex_xy(target.tile)
    already = target.script_index not in (-1, 0xFFFFFFFF)
    print(f"target: square ({hx // 2},{hy // 2}), pid 0x{target.pid:08X}"
          f"{'  (already scripted, index ' + str(target.script_index) + ')' if already else ''}")

    new_index = args.script - 1
    print(f"binding: scripts.lst line {args.script} -> engine index {new_index}")

    mb = MapBuilder(path.name, elevations=tuple(m.elevations), map_id=200)
    for i, _e in enumerate(m.elevations):
        mb.floors[i] = list(m.floors[i])
        mb.roofs[i] = list(m.roofs[i])
    mb.start_tile = m.default_tile
    mb.start_elevation = getattr(m, "default_elevation", 0)
    mb.start_rotation = getattr(m, "default_rotation", 0)

    kept = 0
    for o in m.objects:
        if not (0 <= o.tile < 40000):
            continue
        pr = gi.proto(o.pid)
        if pr is None:
            continue
        t = pid_type(o.pid)
        extras = (read_critter_extras(raw, o.off) if t == OBJ_TYPE_CRITTER
                  else extras_for(t, o.pid, pr.subtype))

        sid, idx = -1, -1
        if o is target:
            stype = SCRIPT_TYPE_CRITTER if t == OBJ_TYPE_CRITTER else SCRIPT_TYPE_SCENERY
            sid = mb.add_script(stype, new_index)
            idx = new_index
        elif o.script_index not in (-1, 0xFFFFFFFF):
            stype = SCRIPT_TYPE_CRITTER if t == OBJ_TYPE_CRITTER else SCRIPT_TYPE_SCENERY
            sid = mb.add_script(stype, o.script_index)
            idx = o.script_index
            kept += 1

        mb.add(MapObjectSpec(
            pid=o.pid, fid=o.fid, tile=o.tile, elevation=o.elevation,
            rotation=o.rotation, frame=o.frame,
            sid=sid, script_index=idx, extras=extras))

    print(f"preserved {kept} pre-existing script binding(s)")

    out = mb.to_bytes()
    if args.dry_run:
        print(f"dry run: would write {len(out)} bytes to {path}")
        return 0

    backup = path.with_suffix(path.suffix + ".bak")
    backup.write_bytes(raw)
    path.write_bytes(out)

    check = MapFile.parse(path.read_bytes(), gi.proto)
    scripted = sum(1 for o in check.objects
                   if o.script_index not in (-1, 0xFFFFFFFF))
    verify = next((o for o in check.objects
                   if o.tile == args.tile and o.pid == args.pid), None)
    print(f"wrote {path.name}: {len(check.objects)} objects, "
          f"{scripted} carrying a script")
    print(f"  target now: script_index={verify.script_index if verify else '???'}")
    print(f"  backup: {backup.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
