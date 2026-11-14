#!/usr/bin/env python3
"""
preview_source.py - render any source game's map elevation with a square-grid
overlay, to choose regions for compose_map.py to lift.

    python tools/preview_source.py fo2 geckpwpl 0 -o build/preview/geckpwpl0.png
    python tools/preview_source.py nevada SFCHINA 0 --scale 0.5 --grid 10

Labels every --grid squares with the square (x, y) under it, in the same
square coordinates compose_map.py's `lift` rects use. Read-only.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import port  # noqa: E402
import render  # noqa: E402
from foclib import GameIndex, MapFile  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("source", help="sources.json name, fo2 (the pristine base) or foc (FOC's maps)")
    ap.add_argument("map")
    ap.add_argument("elev", type=int)
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--scale", type=float, default=0.5)
    ap.add_argument("--grid", type=int, default=10)
    args = ap.parse_args()

    # "foc" = FOC's own maps as deployed (base/mods/foc); "fo2" = the pristine base
    gi = (GameIndex() if args.source == "fo2" else GameIndex(pristine=False) if args.source == "foc"
          else port.open_source(args.source))
    name = args.map if args.map.lower().endswith(".map") else args.map + ".map"
    raw = gi.vfs.read(f"maps\\{name}")
    m = MapFile.parse(raw, gi.proto)
    if args.elev not in m.elevations:
        sys.exit(f"{name} has elevations {m.elevations}")
    ei = m.elevations.index(args.elev)
    img = render.render_map(gi, m, ei)
    # same bounds render_map used
    pts = []
    for sq, tid in enumerate(m.floors[ei]):
        if tid > 1:
            pts.append(render.square_xy(sq))
    for o in m.objects:
        if o.elevation == args.elev and 0 <= o.tile < 40000:
            x, y = render.hex_xy_screen(o.tile)
            pts.append((x - 100, y - 200))
            pts.append((x + 100, y + 60))
    minx = min(p[0] for p in pts)
    miny = min(p[1] for p in pts)
    from PIL import ImageDraw
    dr = ImageDraw.Draw(img)
    g = args.grid
    for sy in range(0, 100, g):
        for sx in range(0, 100, g):
            if m.floors[ei][sy * 100 + sx] <= 1:
                continue
            x, y = render.hex_xy_screen((sy * 2) * 200 + sx * 2)
            cx, cy = x + 16 - minx, y + 8 - miny
            dr.rectangle([cx - 2, cy - 2, cx + 2, cy + 2], fill=(255, 255, 0, 255))
            dr.text((cx + 4, cy - 6), f"{sx},{sy}", fill=(255, 255, 0, 255))
    img = img.convert("RGB")
    img = img.resize((max(1, int(img.width * args.scale)), max(1, int(img.height * args.scale))))
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out)
    print(f"{out} {img.size} objects={sum(1 for o in m.objects if o.elevation == args.elev)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
