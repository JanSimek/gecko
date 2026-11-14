#!/usr/bin/env python3
"""
stamps.py - lift a multi-object STRUCTURE out of a real map so it can be
stamped down elsewhere.

    python tools/stamps.py ARVILLAG.MAP ybk        # find Arroyo's tents

A fragment is a rectangle of map. A stamp is smaller and more useful: the
objects that make up one *thing* - a tent, a fence run, a shack - captured as
relative hex offsets with their pids and rotations, so it can be placed
anywhere.

This exists because wall art has an assembly grammar (which piece runs along
+x, which is a corner, which is an end cap) that is tedious and error-prone to
reverse-engineer. Lifting a structure someone already assembled skips the
problem: the grammar comes along with the geometry.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib import TYPE_NAME, GameIndex, hex_xy, parse_lst, pid_type  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "corpus" / "stamps.json"


class Stamp:
    """One structure: objects at hex offsets from the cluster's top-left."""

    def __init__(self, name, objs, source):
        self.name = name
        self.objs = objs          # [{dx, dy, pid, rot, art, proto}]
        self.source = source

    @property
    def extent(self):
        xs = [o["dx"] for o in self.objs]
        ys = [o["dy"] for o in self.objs]
        return (max(xs) - min(xs) + 1, max(ys) - min(ys) + 1)

    def to_dict(self):
        w, h = self.extent
        return {"name": self.name, "source": self.source,
                "hex_w": w, "hex_h": h, "count": len(self.objs),
                "objects": self.objs}


def cluster(objs, gap: int = 3):
    """Group objects whose hexes are within `gap` of each other (8-connected)."""
    remaining = list(objs)
    out = []
    while remaining:
        seed = remaining.pop()
        group = [seed]
        changed = True
        while changed:
            changed = False
            for o in list(remaining):
                ox, oy = hex_xy(o.tile)
                for g in group:
                    gx, gy = hex_xy(g.tile)
                    if abs(ox - gx) <= gap and abs(oy - gy) <= gap:
                        group.append(o)
                        remaining.remove(o)
                        changed = True
                        break
        out.append(group)
    return out


def extract(gi: GameIndex, map_name: str, art_prefix: str,
            min_objs: int = 4) -> list[Stamp]:
    data = gi.vfs.read(f"maps/{map_name}")
    m = gi.load_map(data)
    walls = parse_lst(gi.vfs.read("art/walls/walls.lst"))
    scen = parse_lst(gi.vfs.read("art/scenery/scenery.lst"))

    def art_of(o):
        lst = walls if pid_type(o.pid) == 3 else scen
        i = o.fid & 0xFFF
        return lst[i] if 0 <= i < len(lst) and lst[i] else ""

    hits = [o for o in m.objects
            if pid_type(o.pid) in (2, 3)
            and art_of(o).lower().startswith(art_prefix.lower())
            and 0 <= o.tile < 40000]
    if not hits:
        return []

    stamps = []
    for i, group in enumerate(cluster(hits)):
        if len(group) < min_objs:
            continue
        xs = [hex_xy(o.tile)[0] for o in group]
        ys = [hex_xy(o.tile)[1] for o in group]
        x0, y0 = min(xs), min(ys)
        objs = []
        for o in sorted(group, key=lambda o: o.tile):
            hx, hy = hex_xy(o.tile)
            objs.append({
                "dx": hx - x0, "dy": hy - y0,
                "pid": o.pid, "rot": o.rotation,
                "art": art_of(o),
                "proto": gi.proto_filename(o.pid),
            })
        stamps.append(Stamp(f"{art_prefix}_{i:02d}", objs,
                            f"fo2 maps/{map_name}"))
    return stamps


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("map")
    ap.add_argument("prefix")
    ap.add_argument("--min", type=int, default=4)
    ap.add_argument("--save", action="store_true")
    args = ap.parse_args()

    gi = GameIndex()
    stamps = extract(gi, args.map, args.prefix, args.min)
    print(f"{len(stamps)} structure(s) of art '{args.prefix}*' in {args.map}")
    for s in stamps:
        w, h = s.extent
        arts = sorted({o["art"] for o in s.objs})
        print(f"  {s.name:14s} {len(s.objs):3d} objects, {w}x{h} hexes")
        print(f"      {', '.join(arts[:8])}{' ...' if len(arts) > 8 else ''}")

    if args.save and stamps:
        OUT.parent.mkdir(parents=True, exist_ok=True)
        existing = {}
        if OUT.is_file():
            existing = json.loads(OUT.read_text(encoding="utf-8"))
        for s in stamps:
            existing[s.name] = s.to_dict()
        OUT.write_text(json.dumps(existing, indent=1), encoding="utf-8")
        print(f"\nsaved {len(stamps)} stamp(s) -> {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
