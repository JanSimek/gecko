#!/usr/bin/env python3
"""
remove_black_frames.py - take the black frame pieces (RPU's BLACK002 / BLACK003
scenery, drawn with gecko's Black Edge tool) off FOC's maps.

    python tools/remove_black_frames.py --dry-run
    python tools/remove_black_frames.py

Owner, 2026-10-18: "you can also remove all black bars that surround the maps too".
The frames hid the ragged end of the floor; the Hi-Res Patch's edge box
(maps/<name>.edg, tools/map_edges.py) now cuts the view off cleanly instead, and
tools/paint_corners.py paints any void the frames were covering inside the box.
Both copies of each map are written (old ones to _safety_backups/black_frames/);
refuses while gecko or the game is running.
"""

from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib import GameIndex  # noqa: E402
from foclib.formats import MapFile  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
AUTH = ROOT / "mod" / "data" / "maps"
DEPL = ROOT / "base" / "mods" / "foc" / "maps"
BACKUP = ROOT / "_safety_backups" / "black_frames"
FRAME_PIDS = (0x020008F8, 0x020008F9)        # BLACK002 (vertical runs), BLACK003 (horizontal runs)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    if not args.dry_run:
        running = subprocess.run(["tasklist"], capture_output=True, text=True).stdout.lower()
        if "gecko" in running or "fallout2" in running:
            sys.exit("close gecko and the game first")
    gi = GameIndex(pristine=False)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    for p in sorted(AUTH.iterdir()):
        if p.suffix.lower() != ".map" or not (DEPL / p.name).exists():
            continue
        raw = p.read_bytes()
        if (DEPL / p.name).read_bytes() != raw:
            sys.exit(f"{p.name}: the deployed copy differs from mod/data/maps; run tools/sync_maps.py first")
        m = MapFile.parse(raw, gi.proto)
        drop = [o for o in m.objects if o.pid in FRAME_PIDS]
        if not drop:
            continue
        ends = {o.off: MapFile._read_object(raw, o.off, gi.proto)[1] for o in m.objects}
        groups = {0: [], 1: [], 2: []}
        for o in m.objects:
            if o.pid not in FRAME_PIDS:
                groups[o.elevation].append(raw[o.off:ends[o.off]])
        out = bytearray(raw[:m.objects[0].off - 8])
        out += struct.pack(">i", sum(len(g) for g in groups.values()))
        for e in (0, 1, 2):
            out += struct.pack(">i", len(groups[e]))
            for d in groups[e]:
                out += d
        out += raw[max(ends.values()):]
        MapFile.parse(bytes(out), gi.proto)
        print(f"{p.name}: {len(drop)} black frame pieces" + (" (dry run)" if args.dry_run else " removed"))
        if not args.dry_run:
            BACKUP.mkdir(parents=True, exist_ok=True)
            shutil.copy2(p, BACKUP / f"{p.name}.{stamp}")
            p.write_bytes(bytes(out))
            (DEPL / p.name).write_bytes(bytes(out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
