#!/usr/bin/env python3
"""
find_tile.py - list FOC floor/roof tile ids by name prefix (ids below 4096,
the only ones gecko and the engine can address).

    python tools/find_tile.py cav mine dirt
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib import GameIndex, parse_lst  # noqa: E402


def main() -> int:
    gi = GameIndex(pristine=False)
    tiles = parse_lst(gi.vfs.read("art\\tiles\\tiles.lst"))
    for pre in sys.argv[1:]:
        hits = [(i, n) for i, n in enumerate(tiles) if n and n.lower().startswith(pre.lower()) and i < 4096]
        print(f"{pre}: {len(hits)}")
        print("  " + " ".join(f"{n[:-4]}={i}" for i, n in hits[:60]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
