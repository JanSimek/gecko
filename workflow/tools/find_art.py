#!/usr/bin/env python3
"""
find_art.py - list FOC art list entries (index, name) matching regexes.

    python tools/find_art.py critters "g(h)?ul" mut robe
    python tools/find_art.py scenery cask barrel
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib import GameIndex, parse_lst  # noqa: E402


def main() -> int:
    d = sys.argv[1]
    gi = GameIndex(pristine=False)
    lst = parse_lst(gi.vfs.read(f"art\\{d}\\{d}.lst"))
    for pat in sys.argv[2:]:
        rx = re.compile(pat, re.I)
        hits = [(i, n) for i, n in enumerate(lst) if n and rx.search(n) and i < 4096]
        print(f"{pat}: {len(hits)}")
        print("  " + "  ".join(f"{i}:{n}" for i, n in hits[:80]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
