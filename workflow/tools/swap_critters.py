#!/usr/bin/env python3
"""
swap_critters.py - turn critters of some protos on a map into another creature,
keeping each one where it stands, its facing, its script and its inventory.

    python tools/swap_critters.py CLCOURT.MAP --from 0x01000070,0x01000071,0x0100006E,0x01000075 --to 0x01000354 --dry-run

Each matching critter gets the new proto's PID and art (the art index in its
FID; the facing and animation bits are kept), the new proto's AI packet and
its full HP in the map's critter block (the block's ai and hp fields, at
offsets 104 and 116 from the object's start; tools/map_crop.py once wrote the
AI into the results field, see the critter block memory). Writes mod/data/maps
and the deployed copy; close gecko and the game first.
"""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib import GameIndex  # noqa: E402
from foclib.formats import MapFile  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
AUTH = ROOT / "mod" / "data" / "maps"
DEPL = ROOT / "base" / "mods" / "foc" / "maps"
OFF_AI, OFF_HP = 104, 116


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("map")
    ap.add_argument("--from", dest="src", required=True, help="comma-separated PIDs")
    ap.add_argument("--to", dest="dst", required=True)
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    if not a.dry_run:
        running = subprocess.run(["tasklist"], capture_output=True, text=True).stdout.lower()
        if "gecko" in running or "fallout2" in running:
            sys.exit("close gecko and the game first")
    src = {int(p, 16) for p in a.src.split(",")}
    dst = int(a.dst, 16)
    gi = GameIndex(pristine=False)
    pr = gi.proto(dst)
    if pr is None or (dst >> 24) != 1:
        sys.exit(f"0x{dst:08X} is not a critter proto")
    path = next(q for q in AUTH.iterdir() if q.name.upper() == a.map.upper())
    raw = path.read_bytes()
    dep = DEPL / path.name
    if dep.exists() and dep.read_bytes() != raw:
        sys.exit(f"{path.name}: the deployed copy differs from mod/data/maps; run tools/sync_maps.py first")
    m = MapFile.parse(raw, gi.proto)
    out = bytearray(raw)
    n = 0
    for o in m.objects:
        if o.pid not in src:
            continue
        fid = (o.fid & 0xFFFFF000) | (pr.fid & 0xFFF)
        struct.pack_into(">I", out, o.pid_offset, dst)
        struct.pack_into(">I", out, o.fid_offset, fid)
        struct.pack_into(">i", out, o.off + OFF_AI, pr.ai_packet)
        struct.pack_into(">i", out, o.off + OFF_HP, pr.max_hp)
        print(f"  ({o.tile % 200},{o.tile // 200}) e{o.elevation}: 0x{o.pid:08X} -> 0x{dst:08X}, hp {pr.max_hp}, ai {pr.ai_packet}")
        n += 1
    check = MapFile.parse(bytes(out), gi.proto)
    assert sum(1 for o in check.objects if o.pid == dst) >= n
    print(f"{path.name}: {n} critters swapped" + (" (dry run)" if a.dry_run else ""))
    if not a.dry_run and n:
        path.write_bytes(bytes(out))
        if dep.exists():
            dep.write_bytes(bytes(out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
