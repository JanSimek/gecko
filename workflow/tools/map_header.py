#!/usr/bin/env python3
"""
map_header.py - set a FOC map's header script and/or player start, in place.

    python tools/map_header.py LAMESA.MAP --script SCRIPT_LAMESA
    python tools/map_header.py LAMESA.MAP --start 168,176 --rot 5
    python tools/map_header.py LAMESA.MAP --show

Header (big-endian, map.cc): default tile @0x14, default elevation @0x18,
orientation @0x1C, map script @0x24 (the script's scripts.lst line, as the
SCRIPT_* defines in foc_ids.h). Both copies (base/mods/foc/maps and
mod/data/maps) are written; they must match first (map_fixup.py after a
gecko save). The old file goes to _safety_backups/map_header/.
"""

from __future__ import annotations

import argparse
import filecmp
import re
import shutil
import struct
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEPLOYED = ROOT / "base" / "mods" / "foc" / "maps"
AUTHORED = ROOT / "mod" / "data" / "maps"
IDS_H = ROOT / "scripts_src" / "foc" / "foc_ids.h"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("map")
    ap.add_argument("--script")
    ap.add_argument("--start", help="hex x,y")
    ap.add_argument("--rot", type=int)
    ap.add_argument("--elev", type=int)
    ap.add_argument("--show", action="store_true")
    args = ap.parse_args()
    dep = next((p for p in DEPLOYED.glob("*") if p.name.upper() == args.map.upper()), None)
    if dep is None:
        sys.exit(f"no {args.map} in {DEPLOYED}")
    aut = AUTHORED / dep.name
    raw = bytearray(dep.read_bytes())
    tile, elev, rot = struct.unpack_from(">3i", raw, 0x14)
    script = struct.unpack_from(">i", raw, 0x24)[0]
    print(f"{dep.name}: start {tile} ({tile % 200},{tile // 200}) elev {elev} rot {rot}, script {script}")
    if args.show:
        return 0
    if aut.exists() and not filecmp.cmp(dep, aut, shallow=False):
        sys.exit("the two copies differ; run map_fixup.py first")
    if args.script:
        m = re.search(rf"#define\s+{re.escape(args.script)}\s+\((\d+)\)", IDS_H.read_text(encoding="utf-8"))
        if not m:
            sys.exit(f"{args.script} not in foc_ids.h")
        struct.pack_into(">i", raw, 0x24, int(m.group(1)))
    if args.start:
        x, y = map(int, args.start.split(","))
        struct.pack_into(">i", raw, 0x14, y * 200 + x)
    if args.rot is not None:
        struct.pack_into(">i", raw, 0x1C, args.rot)
    if args.elev is not None:
        struct.pack_into(">i", raw, 0x18, args.elev)
    bk = ROOT / "_safety_backups" / "map_header"
    bk.mkdir(parents=True, exist_ok=True)
    shutil.copy2(dep, bk / f"{dep.name}.{time.strftime('%Y%m%d_%H%M%S')}")
    dep.write_bytes(bytes(raw))
    if aut.exists():
        aut.write_bytes(bytes(raw))
    tile, elev, rot = struct.unpack_from(">3i", raw, 0x14)
    print(f"  now start {tile} ({tile % 200},{tile // 200}) elev {elev} rot {rot}, "
          f"script {struct.unpack_from('>i', raw, 0x24)[0]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
