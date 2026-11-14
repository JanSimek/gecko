#!/usr/bin/env python3
"""
find_proto.py - search FOC's protos by display name or art name.

    python tools/find_proto.py scenery winch panel computer
    python tools/find_proto.py items keycard wire rope
    python tools/find_proto.py critters rat roach gecko --limit 40

Prints pid, display name, art file and (scenery/items) subtype. Read-only.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib import GameIndex, parse_lst  # noqa: E402

TYPES = {"items": (0, "pro_item.msg"), "critters": (1, "pro_crit.msg"),
         "scenery": (2, "pro_scen.msg"), "walls": (3, "pro_wall.msg"), "misc": (5, "pro_misc.msg")}
ART = {0: "items", 1: "critters", 2: "scenery", 3: "walls", 5: "misc"}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("type", choices=TYPES)
    ap.add_argument("words", nargs="+")
    ap.add_argument("--limit", type=int, default=25)
    args = ap.parse_args()
    t, msgf = TYPES[args.type]
    gi = GameIndex(pristine=False)
    msg = {}
    for m in re.finditer(r"\{(\d+)\}\{[^}]*\}\{([^}]*)\}",
                         gi.vfs.read(f"text\\english\\game\\{msgf}").decode("cp1252", "replace")):
        msg[int(m.group(1))] = m.group(2)
    art = parse_lst(gi.vfs.read(f"art\\{ART[t]}\\{ART[t]}.lst"))
    lst = gi.proto_lst(t)
    words = [w.lower() for w in args.words]
    hits = {w: [] for w in words}
    for i in range(1, len(lst) + 1):
        pid = (t << 24) | i
        try:
            b = gi.vfs.read(gi.proto_path(pid))
        except Exception:  # noqa: BLE001
            continue
        name = msg.get(struct.unpack_from(">i", b, 4)[0], "")
        fid = struct.unpack_from(">i", b, 8)[0]
        a = art[fid & 0xFFF] if (fid & 0xFFF) < len(art) else "?"
        sub = struct.unpack_from(">i", b, 0x20)[0] if t in (0, 2) else ""
        for w in words:
            if w in name.lower() or w in (a or "").lower():
                hits[w].append(f"0x{pid:08X} {name!r:32} {a} {sub}")
    for w, h in hits.items():
        print(f"== {w}: {len(h)}")
        for line in h[:args.limit]:
            print("  " + line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
