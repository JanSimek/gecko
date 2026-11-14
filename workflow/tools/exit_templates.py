#!/usr/bin/env python3
"""
exit_templates.py - copy exit-grid prefabs out of Fallout 2's own maps, object for
object, into tools/exit_templates.json, for tools/exit_layout.py to stamp onto FOC's
maps.

    python tools/exit_templates.py

Fallout 2 builds an exit as the OUTLINE of a zone hanging in from the map's edge box
(maps/<name>.edg): each side of the outline is drawn in the exit-grid variant whose
art overhangs into the zone, so the zone reads as one translucent patch, and the row
on the box edge itself is spaced out. The rest of the box edge is a row of invisible
block hexes ("Block Hex Auto Inviso", scenery 0x02000158), so a player leaves the
screen only through an exit (tools/edge_survey.py; FO2's Modoc and Gecko carry the
very same world-map prefab).

One prefab per side of the box:
  bottom  modmain.map, the world-map exit (the same prefab is on gecksetl.map)
  top     modmain.map, the exit to map 19
  left    gecksetl.map, the exit to map 33
  right   klamall.map, the exit to map 9
Each is stored as hex offsets from an anchor hex with an even x (a shift by an even
number of columns keeps a hex shape exactly), the variant of each object, and where
the anchor sits against its own box edge in screen px.
"""

from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import port  # noqa: E402
from foclib.formats import MapFile  # noqa: E402
from map_survey import list_maps  # noqa: E402
import edge_survey as es  # noqa: E402

OUT = Path(__file__).resolve().parent / "exit_templates.json"
W = 200
SOURCES = {
    "bottom": ("fo2", "modmain.map", -2),
    "top": ("fo2", "modmain.map", 19),
    "left": ("fo2", "gecksetl.map", 33),
    "right": ("fo2", "klamall.map", 9),
}


def main() -> int:
    gi = port.open_source("fo2")
    maps = list_maps(gi)
    out = {}
    for side, (src, name, dest) in SOURCES.items():
        mp = next(m for m in maps if m.endswith("\\" + name))
        raw = gi.vfs.read(mp)
        m = MapFile.parse(raw, gi.proto)
        L, T, R, B = es.parse_edg(gi.vfs.read(mp[:-4] + ".edg"))[0][0]
        x0, x1 = sorted((es.hex_xy(L)[0], es.hex_xy(R)[0]))
        y0, y1 = sorted((es.hex_xy(T)[1], es.hex_xy(B)[1]))
        objs = []
        for o in m.objects:
            if o.elevation == 0 and 0x05000010 <= o.pid <= 0x05000017:
                _, end = MapFile._read_object(raw, o.off, gi.proto)
                if struct.unpack_from(">i", raw, end - 16)[0] == dest:
                    objs.append((o.tile, o.pid & 0xFF))
        xs = [es.hex_xy(t)[0] for t, _ in objs]
        ys = [es.hex_xy(t)[1] for t, _ in objs]
        cx = (min(xs) + max(xs)) / 2
        cy = (min(ys) + max(ys)) / 2
        anchor = min((t for t, _ in objs if (t % W) % 2 == 0),
                     key=lambda t: abs(es.hex_xy(t)[0] - cx) + abs(es.hex_xy(t)[1] - cy))
        ax, ay = anchor % W, anchor // W
        AX, AY = es.hex_xy(anchor)
        edge = {"bottom": AY - y1, "top": AY - y0, "left": AX - x0, "right": AX - x1}[side]
        along = AX - cx if side in ("top", "bottom") else AY - cy
        out[side] = {
            "source": f"{src}:{name} exit to map {dest}",
            "objects": [[t % W - ax, t // W - ay, p] for t, p in sorted(objs)],
            "anchor_to_edge_px": edge,
            "anchor_from_centre_px": along,
            "span_px": [max(xs) - min(xs), max(ys) - min(ys)],
        }
        print(f"{side:6s} {len(objs):3d} objects from {name}, anchor {edge:+d} px from the {side} edge, "
              f"span {max(xs) - min(xs)}x{max(ys) - min(ys)} px, variants {sorted(set(p for _, p in objs))}")
    OUT.write_text(json.dumps(out, indent=1), encoding="utf-8")
    print(f"wrote {OUT.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
