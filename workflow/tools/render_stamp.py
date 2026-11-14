#!/usr/bin/env python3
"""
render_stamp.py - render gecko stamps (pattern JSON) with FOC's data, the way
gecko would stamp them, to check them without opening the editor.

    python tools/render_stamp.py STAMP.json [...] -o out.png          # one image per stamp
    python tools/render_stamp.py --sheet DIR -o sheet.png [--limit 30]  # contact sheet of a folder
"""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from PIL import Image, ImageDraw  # noqa: E402

import render  # noqa: E402
from foclib.paths import inside  # noqa: E402
from foclib import GameIndex, MapFile  # noqa: E402
from foclib.formats import pid_type  # noqa: E402
from mapbuild import HEX_W, MapBuilder, MapObjectSpec, extras_for  # noqa: E402


def stamp_image(gi: GameIndex, path: Path) -> Image.Image:
    d = json.loads(path.read_text(encoding="utf-8"))
    v = d["variants"][0]
    ax, ay = 100, 100            # stamp in the middle of the grid, on even parity
    mb = MapBuilder("STAMP.MAP", elevations=(0,), map_id=0)
    mb.floors[0] = [1] * 10000
    mb.roofs[0] = [1] * 10000
    for t in v.get("floor", []):
        mb.set_floor(0, ax // 2 + t["dxTile"], ay // 2 + t["dyTile"], t["tileId"])
    for o in v.get("objects", []):
        pr = gi.proto(o["proPid"])
        if pr is None:
            continue
        tile = (ay + o["dyHex"]) * HEX_W + ax + o["dxHex"]
        mb.add(MapObjectSpec(pid=o["proPid"], fid=o["frmPid"], tile=tile, elevation=0,
                             rotation=o.get("direction", 0), frame=0,
                             extras=extras_for(pid_type(o["proPid"]), o["proPid"], pr.subtype)))
    m = MapFile.parse(mb.to_bytes(), gi.proto)
    with contextlib.redirect_stdout(io.StringIO()):
        return render.render_map(gi, m, 0, True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("stamps", nargs="*", type=Path)
    ap.add_argument("--sheet", type=Path)
    ap.add_argument("--limit", type=int, default=30)
    ap.add_argument("-o", "--out", type=Path, required=True)
    a = ap.parse_args()
    stamps = [inside(p) for p in a.stamps]
    sheet = inside(a.sheet) if a.sheet else None
    out = inside(a.out)
    gi = GameIndex(pristine=False)
    if sheet:
        paths = sorted(sheet.rglob("*.json"))[: a.limit]
        cw, ch, cols = 420, 300, 5
        s = Image.new("RGB", (cw * cols, ch * ((len(paths) + cols - 1) // cols)), (20, 20, 20))
        dr = ImageDraw.Draw(s)
        for i, p in enumerate(paths):
            im = stamp_image(gi, p).convert("RGB")
            im.thumbnail((cw - 8, ch - 22))
            x, y = (i % cols) * cw, (i // cols) * ch
            s.paste(im, (x + 4, y + 2))
            dr.text((x + 4, y + ch - 16), p.stem, fill=(255, 220, 60))
        s.save(out)
    else:
        for p in stamps:
            stamp_image(gi, p).save(out if len(stamps) == 1 else out.with_name(p.stem + ".png"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
