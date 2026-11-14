#!/usr/bin/env python3
"""
exit_sheet.py - render maps with exit grids painted red and scroll blockers
blue, to see how they are laid out.

    python tools/exit_sheet.py fo2:klamall.map sonora:cggrande.map ... -o build/map_survey/exits_sheet.png
"""

from __future__ import annotations

import argparse
import contextlib
import io
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from PIL import Image, ImageDraw  # noqa: E402

import port  # noqa: E402
import render  # noqa: E402
from foclib.formats import MapFile, is_exit_grid  # noqa: E402
from map_exits_to_edge import object_spans  # noqa: E402

DEST_COLOURS = {-2: (255, 40, 40), -1: (255, 200, 0)}


def one(src: str, mp: str, width: int) -> Image.Image:
    gi = port.open_source(src)
    raw = gi.vfs.read("maps\\" + mp)
    m = MapFile.parse(raw, gi.proto)
    spans, *_ = object_spans(raw, m, gi)
    dest = {o.tile: struct.unpack_from(">i", raw, e - 16)[0] for o, s, e in spans if is_exit_grid(o.pid)}
    with contextlib.redirect_stdout(io.StringIO()):
        img = render.render_map(gi, m, 0, True).convert("RGB")
    pts = []
    for sq, tid in enumerate(m.floors[0]):
        if tid > 1:
            pts.append(render.square_xy(sq))
    for o in m.objects:
        if o.elevation == m.elevations[0] and 0 <= o.tile < 40000:
            x, y = render.hex_xy_screen(o.tile)
            pts.append((x - 100, y - 200))
    minx = min(p[0] for p in pts)
    miny = min(p[1] for p in pts)
    d = ImageDraw.Draw(img)
    for o in m.objects:
        if o.elevation != m.elevations[0] or not 0 <= o.tile < 40000:
            continue
        x, y = render.hex_xy_screen(o.tile)
        px, py = x + 16 - minx, y + 8 - miny
        if is_exit_grid(o.pid):
            c = DEST_COLOURS.get(dest.get(o.tile), (60, 255, 60))
            d.ellipse((px - 7, py - 4, px + 7, py + 4), fill=c)
        elif o.pid == 0x500000C:
            d.ellipse((px - 4, py - 2, px + 4, py + 2), fill=(60, 120, 255))
    img.thumbnail((width, width))
    d = ImageDraw.Draw(img)
    d.text((6, 6), f"{src}:{mp}", fill=(255, 255, 255))
    return img


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("maps", nargs="+")
    ap.add_argument("-o", "--out", type=Path, required=True)
    ap.add_argument("--cols", type=int, default=3)
    ap.add_argument("--width", type=int, default=700)
    a = ap.parse_args()
    ims = [one(*s.split(":"), a.width) for s in a.maps]
    h = max(i.height for i in ims)
    sheet = Image.new("RGB", (a.width * a.cols, h * ((len(ims) + a.cols - 1) // a.cols)), (0, 0, 0))
    for k, im in enumerate(ims):
        sheet.paste(im, ((k % a.cols) * a.width, (k // a.cols) * h))
    sheet.save(a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
