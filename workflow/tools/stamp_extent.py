#!/usr/bin/env python3
"""
stamp_extent.py - print the hex footprint of gecko stamps (dx/dy range of
their objects), for laying them out with compose_map.py.

    python tools/stamp_extent.py "Ruins/Nevada SLSHADOW 3 1st Floor.json" ...
    python tools/stamp_extent.py --folder Tents
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

from foclib import config as _config  # noqa: E402
STAMPS = _config.stamps_dir()


def main() -> int:
    args = sys.argv[1:]
    files = []
    if args and args[0] == "--folder":
        files = sorted((STAMPS / args[1]).glob("*.json"))
    else:
        files = [STAMPS / a for a in args]
    for f in files:
        d = json.loads(f.read_text(encoding="utf-8"))
        v = d["variants"][0]
        xs = [o["dxHex"] for o in v["objects"]] or [0]
        ys = [o["dyHex"] for o in v["objects"]] or [0]
        print(f"{f.relative_to(STAMPS)}: dx {min(xs)}..{max(xs)}  dy {min(ys)}..{max(ys)}  "
              f"({max(xs) - min(xs) + 1}x{max(ys) - min(ys) + 1} hexes, {len(v['objects'])} objects)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
