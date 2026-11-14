#!/usr/bin/env python3
"""
rock_field.py - lay Fallout 2's underground rock field round the rooms of an
underground map, the way the originals do.

    python tools/rock_field.py RWFAC1.MAP RWFAC2.MAP RWFAC3.MAP --dry-run
    python tools/rock_field.py RWFAC1.MAP RWFAC2.MAP RWFAC3.MAP

WHY
---
Underground, Fallout 2 never leaves the void round its rooms: the vaults, the
Arroyo caves, the military base and Navarro's bunker all stand in a field of
dark rock floor (cav4009 and cav4010, about 3 to 1), and the map's edge box
(maps/<name>.edg) sits on that field round every room, so nothing in the box
is void (tools/edge_survey.py: void inside 0.0% on all of them). The field is
laid in one fixed pattern on every one of those maps: cav4010 on the squares
of even rows and odd columns, cav4009 on all the rest.

WHAT
----
Every empty floor square (tile 0 or 1) whose rhombus comes within GROW px of
the elevation's edge box gets the field tile for its square. Painted squares
are never touched. Run tools/map_edges.py first (it boxes an UNDERGROUND map
round everything walkable and does not count the field as walkable ground),
then this. Squares listed in <stem>.voids.json beside the map (or in mod/maps_src) (an open pit, as
tools/build_rwfac1.py writes) are left empty. Writes mod/data/maps and the deployed copy; close gecko and the
game first. --authored-only writes mod/data/maps alone, for while the game is
running: the next deploy.py run copies it over, since the authored copy is then
the newer one (do not run sync_maps.py on those maps first, which would copy
the old deployed map back).
"""

from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib import GameIndex, parse_lst  # noqa: E402
from foclib.formats import MapFile  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
AUTH = ROOT / "mod" / "data" / "maps"
DEPL = ROOT / "base" / "mods" / "foc" / "maps"
W = 200
GROW = 96                     # px past the box the field reaches, so the cut-off never shows void
FIELD = ("cav4009.frm", "cav4010.frm")


def hex_xy(t):
    x, y = t % W, t // W
    v3 = W - 1 - x
    return 48 * (v3 // 2) + 32 * (v3 & 1) + 16 * y, -12 * (v3 // 2) + 12 * y


def sq_bounds(sq):
    """Screen bounds of a floor square's rhombus (tile.cc squareTileToScreenXY)."""
    c, r = sq % 100, sq // 100
    hx, hy = hex_xy(2 * r * W + 2 * c)
    sx, sy = hx - 48, hy - 2
    return sx, sy, sx + 80, sy + 36


def field_tile(sq, rock, rock_b):
    c, r = sq % 100, sq // 100
    return rock_b if (r % 2 == 0 and c % 2 == 1) else rock


def edg_zones(name):
    p = next((q for q in AUTH.iterdir() if q.suffix.lower() == ".edg" and q.stem.lower() == Path(name).stem.lower()), None)
    if p is None:
        sys.exit(f"{name}: no edge file; run tools/map_edges.py first")
    v = struct.unpack(">%di" % (p.stat().st_size // 4), p.read_bytes())
    if v[0] != 0x45444745 or v[1] != 1:
        sys.exit(f"{p.name}: not a v1 edge file")
    zones, k = {}, 3
    for e in (0, 1, 2):
        L, T, R, B = v[k:k + 4]
        x0, x1 = sorted((hex_xy(L)[0], hex_xy(R)[0]))
        y0, y1 = sorted((hex_xy(T)[1], hex_xy(B)[1]))
        zones[e] = None if (L, T, R, B) == (199, 0, 39800, 39999) else (x0, y0, x1, y1)
        k += 5
    return zones


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("maps", nargs="+")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--authored-only", action="store_true", help="leave base/mods/foc alone")
    ap.add_argument("--dir", type=Path, help="work on the maps in this folder (a layout being built)")
    a = ap.parse_args()
    global AUTH, DEPL
    if a.dir:
        AUTH, DEPL = a.dir, a.dir / "_no_deployed_copy"
    if not a.dry_run and not a.authored_only and not a.dir:
        running = subprocess.run(["tasklist"], capture_output=True, text=True).stdout.lower()
        if "gecko" in running or "fallout2" in running:
            sys.exit("close gecko and the game first")
    gi = GameIndex(pristine=False)
    names = [n.lower() for n in parse_lst(gi.vfs.read(r"art\tiles\tiles.lst"))]
    rock, rock_b = (names.index(n) for n in FIELD)
    for name in a.maps:
        path = AUTH / name
        raw = path.read_bytes()
        dep = DEPL / name
        if dep.exists() and dep.read_bytes() != raw:
            sys.exit(f"{name}: the deployed copy differs from mod/data/maps; run tools/sync_maps.py first")
        m = MapFile.parse(raw, gi.proto)
        zones = edg_zones(name)
        vf = AUTH / (Path(name).stem + ".voids.json")
        if not vf.exists():
            vf = ROOT / "mod" / "maps_src" / (Path(name).stem + ".voids.json")
        voids = {int(e): {c + 100 * r for c, r in v} for e, v in json.loads(vf.read_text()).items()} if vf.exists() else {}
        out = bytearray(raw)
        print(name)
        for i, (e, fl) in enumerate(zip(m.elevations, m.floors)):
            box = zones.get(e)
            if box is None or not any((v & 0xFFF) > 1 for v in fl):
                continue
            x0, y0, x1, y1 = box[0] - GROW, box[1] - GROW, box[2] + GROW, box[3] + GROW
            n = 0
            for sq in range(10000):
                if (fl[sq] & 0xFFF) > 1 or sq in voids.get(e, ()):
                    continue
                sx0, sy0, sx1, sy1 = sq_bounds(sq)
                if sx1 < x0 or sx0 > x1 or sy1 < y0 or sy0 > y1:
                    continue
                struct.pack_into(">H", out, m.tiles_offset + 40000 * i + 4 * sq + 2, field_tile(sq, rock, rock_b))
                n += 1
            print(f"   e{e}: {n} empty squares laid with the rock field")
        check = MapFile.parse(bytes(out), gi.proto)
        assert len(check.objects) == len(m.objects)
        if not a.dry_run:
            path.write_bytes(bytes(out))
            if dep.exists() and not a.authored_only:
                dep.write_bytes(bytes(out))
    if a.dry_run:
        print("--dry-run: nothing written")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
