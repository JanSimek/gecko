#!/usr/bin/env python3
"""
strip_blockers.py - remove stray invisible blocking hexes from a map.

    python tools/strip_blockers.py --dry-run
    python tools/strip_blockers.py --radius 2

"Secret Blocking Hex" (scenery proto 67) is an INVISIBLE object whose only job
is to make a hex unwalkable, so that a wall sprite reads as solid instead of
something you can stroll through. Gecko paints its wall-blocker overlay on
anything that blocks movement, which is why a stray one shows up in the editor
as a blocker floating in the open with no wall attached.

They arrive by the dozen inside extracted stamps: lift a hut out of Arroyo and
its blocking hexes come with it, whether or not the walls they belonged to
survived the cut. The ones still next to a wall are doing their job. The ones
out in the open are debris.

WHAT COUNTS AS STRAY: no wall object within `--radius` squares. Default 2,
which is forgiving - a blocker one square off a wall corner is still plausibly
sealing it. Run --dry-run first and look at the count before committing.

The map is rewritten through our own writer, so the object table, script table
and tile layers are rebuilt from the parse. A .bak copy is kept beside it.
"""

from __future__ import annotations

import argparse
import shutil
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib import (  # noqa: E402
    OBJ_TYPE_CRITTER,
    GameIndex,
    MapFile,
    TYPE_NAME,
    hex_xy,
    pid_type,
)
from mapbuild import MapBuilder, MapObjectSpec, extras_for  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
BLOCKING_HEX_PID = (2 << 24) | 67          # scenery 67, "Secret Blocking Hex"


def read_critter_extras(data: bytes, off: int):
    """Keep a critter's own combat block, or it loads with hp=0 and renders as
    a corpse."""
    base = off + 72 + 12
    return struct.unpack_from(">10i", data, base + 4)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--map", default="mod/data/maps/REDWELL.MAP")
    ap.add_argument("--radius", type=int, default=2,
                    help="a blocker with no wall within this many squares is stray")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    gi = GameIndex()
    path = ROOT / args.map
    raw = path.read_bytes()
    m = MapFile.parse(raw, gi.proto)

    walls = set()
    for o in m.objects:
        if 0 <= o.tile < 40000 and TYPE_NAME.get(pid_type(o.pid)) == "walls":
            hx, hy = hex_xy(o.tile)
            walls.add((hx // 2, hy // 2))

    def near_wall(sq):
        c, r = sq
        rad = args.radius
        return any((c + dc, r + dr) in walls
                   for dc in range(-rad, rad + 1)
                   for dr in range(-rad, rad + 1))

    drop = set()
    kept = 0
    for o in m.objects:
        if o.pid != BLOCKING_HEX_PID or not (0 <= o.tile < 40000):
            continue
        hx, hy = hex_xy(o.tile)
        if near_wall((hx // 2, hy // 2)):
            kept += 1
        else:
            drop.add(id(o))

    print(f"{path.name}: {len(m.objects)} objects, {len(walls)} wall squares")
    print(f"  blocking hexes kept (within {args.radius} of a wall): {kept}")
    print(f"  blocking hexes to REMOVE (stray):                    {len(drop)}")
    if args.dry_run:
        print("  dry run - nothing written")
        return 0
    if not drop:
        print("  nothing to do")
        return 0

    mb = MapBuilder(path.name, elevations=tuple(m.elevations), map_id=200)
    for i, _e in enumerate(m.elevations):
        mb.floors[i] = list(m.floors[i])
        mb.roofs[i] = list(m.roofs[i])
    mb.start_tile = m.default_tile
    mb.start_elevation = getattr(m, "default_elevation", 0)
    mb.start_rotation = getattr(m, "default_rotation", 0)

    # Rebuild the script table alongside the objects so SIDs stay consistent:
    # an object's sid indexes this table, so dropping objects without rebuilding
    # it would leave every later script bound to the wrong thing.
    sid_map = {}
    for o in m.objects:
        if id(o) in drop or not (0 <= o.tile < 40000):
            continue
        pr = gi.proto(o.pid)
        if pr is None:
            continue
        t = pid_type(o.pid)
        extras = (read_critter_extras(raw, o.off) if t == OBJ_TYPE_CRITTER
                  else extras_for(t, o.pid, pr.subtype))

        sid = -1
        idx = o.script_index
        if idx not in (-1, 0xFFFFFFFF):
            stype = 4 if t == OBJ_TYPE_CRITTER else 3
            key = (stype, idx)
            if key not in sid_map:
                sid_map[key] = mb.add_script(stype, idx)
            sid = sid_map[key]

        mb.add(MapObjectSpec(
            pid=o.pid, fid=o.fid, tile=o.tile, elevation=o.elevation,
            rotation=o.rotation, frame=o.frame,
            sid=sid, script_index=idx if sid != -1 else -1, extras=extras))

    backup = path.with_suffix(path.suffix + ".bak")
    shutil.copy2(path, backup)
    path.write_bytes(mb.to_bytes())

    check = MapFile.parse(path.read_bytes(), gi.proto)
    remaining = sum(1 for o in check.objects if o.pid == BLOCKING_HEX_PID)
    scripted = sum(1 for o in check.objects
                   if o.script_index not in (-1, 0xFFFFFFFF))
    print(f"  wrote {path.name}: {len(check.objects)} objects "
          f"({len(m.objects) - len(check.objects)} removed)")
    print(f"  blocking hexes remaining: {remaining}")
    print(f"  objects still carrying a script: {scripted}")
    print(f"  backup: {backup.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
