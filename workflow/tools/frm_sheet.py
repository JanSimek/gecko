#!/usr/bin/env python3
"""
frm_sheet.py - contact sheet of FOC art by name, first frame of direction 0.

    python tools/frm_sheet.py scenery geckflr1 arhole1 traphol -o build/sheet.png

Read-only; uses the game palette.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib import GameIndex  # noqa: E402


def frame0(data: bytes):
    w, h, size = struct.unpack_from(">HHI", data, 0x3E)
    return w, h, data[0x3E + 12:0x3E + 12 + w * h]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("names", nargs="+")
    ap.add_argument("-o", "--out", required=True)
    args = ap.parse_args()
    from PIL import Image, ImageDraw
    gi = GameIndex(pristine=False)
    pal = gi.vfs.read("color.pal")
    rgb = [(min(255, pal[i * 3] * 4), min(255, pal[i * 3 + 1] * 4), min(255, pal[i * 3 + 2] * 4)) for i in range(256)]
    tiles = []
    for n in args.names:
        n = n if n.lower().endswith(".frm") else n + ".frm"
        try:
            w, h, px = frame0(gi.vfs.read(f"art\\{args.dir}\\{n}"))
        except Exception as e:  # noqa: BLE001
            print(f"{n}: {e}")
            continue
        im = Image.new("RGB", (w, h), (40, 40, 40))
        im.putdata([rgb[p] if p else (40, 40, 40) for p in px])
        tiles.append((n, im))
    if not tiles:
        return 1
    cw = max(t[1].width for t in tiles) + 10
    ch = max(t[1].height for t in tiles) + 24
    sheet = Image.new("RGB", (cw * len(tiles), ch), (20, 20, 20))
    dr = ImageDraw.Draw(sheet)
    for i, (n, im) in enumerate(tiles):
        sheet.paste(im, (i * cw + 5, 18))
        dr.text((i * cw + 5, 2), n, fill=(255, 255, 0))
    sheet.save(args.out)
    print(args.out, sheet.size)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
