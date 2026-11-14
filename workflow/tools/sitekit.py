#!/usr/bin/env python3
"""
sitekit.py - design a FOC map in code and write it as a compose_map.py
recipe. Nothing is copied from another map: buildings are drawn with wall
families learned from the original games (wallkit.py), ground is grown from
Fallout 2's own tile rules (groundkit.py), rooms are furnished for their job,
clutter gathers along walls the way it does in the originals, and the story
scenes come from templates drawn from docs/MAP_STORYTELLING_GUIDE.md.

Every scenery pid here is Fallout 2's own (below FOC's first id), so no
Olympus asset can get in; compose_map.py refuses Olympus objects anyway.

A design script (tools/designs/<map>.py) builds a Site and calls save().

Coordinates: buildings and ground in SQUARES (100 x 100); objects in HEXES
(200 x 200). A square (sx, sy) covers hexes (2sx..2sx+1, 2sy..2sy+1).
"""

from __future__ import annotations

import json
import random
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# ---------------------------------------------------------------- catalogue
# Fallout 2's own scenery (pid < 0x020009C4)
P = {
    "bones": [0x02000052, 0x02000053, 0x02000054, 0x02000055, 0x02000056],
    "skeleton": [0x0200037A],
    "body": [0x02000281, 0x02000282, 0x02000283, 0x02000284],
    "blood": [0x02000047, 0x02000529, 0x0200052A, 0x0200052B],
    "tires": [0x02000007, 0x02000065],
    "junk": [0x0200006A, 0x0200006B, 0x0200006D, 0x02000074, 0x02000075, 0x02000076],
    "trash": [0x02000137, 0x02000138, 0x02000139, 0x0200013A],
    "trashcan": [0x0200013B],
    "car": [0x020000E0, 0x020000E1, 0x020000E2, 0x020000E3, 0x020000E4],
    "cart": [0x02000170],
    "truck": [0x02000143, 0x02000144],
    "graffiti": [0x02000357, 0x02000358, 0x02000359, 0x0200035A, 0x0200035B, 0x0200035C],
    "mattress": [0x020000CE, 0x020000CF],
    "firepit": [0x02000264],
    "firebarrel": [0x02000001],
    "grave": [0x020003F4],
    "cross": [0x020004D5, 0x020004D6, 0x020004D7, 0x020004D8],
    "headstone": [0x020003F5, 0x020003F6, 0x020003F7, 0x020003F8],
    "pot": [0x0200016D, 0x02000174],
    "watertank": [0x0200014B],
    "pump": [0x02000388],
    "rubble": [0x02000119],
    "debris": [0x020002D8, 0x020002DC, 0x020002DE],
    "scrub": [0x02000066, 0x02000067, 0x02000068, 0x02000069],
    "tumbleweed": [0x0200003F],
    "bush": [0x0200023D, 0x0200023E, 0x0200023F],
    "cactus": [0x02000040, 0x02000041],
    "tree": [0x02000042],
    "flagpole": [0x02000679],
    "lamp": [0x020000F4, 0x020000F5, 0x020000F6],
    "streetlight": [0x02000123, 0x02000124, 0x02000125],
    "desk": [0x02000194, 0x02000195],
    "bunk": [0x020000BE, 0x020000BF, 0x020000C0],
    "stove": [0x020002AA, 0x020002AB],
    "shelf_books": [0x0200090F, 0x02000912],
    "shelf_goods": [0x02000910, 0x02000913],
    "shelf_parts": [0x02000911, 0x02000914],
    "barrel": [0x02000005, 0x02000006, 0x0200018C, 0x0200018D],
    "barrels": [0x0200018E, 0x0200018F],
    "crate": [0x020000B2, 0x020000B3, 0x020000B4, 0x020000C1, 0x02000191, 0x02000192, 0x02000193],
    "boxes": [0x020000DC, 0x020000DD, 0x020000DE, 0x020000DF],
    "sacks": [0x02000722, 0x02000723, 0x02000724, 0x02000725],
    "generator": [0x02000290],
    "generator_dead": [0x02000655],
    "computer": [0x02000009, 0x0200000B, 0x0200000C],
    "console": [0x020000B6, 0x020000B7],
    "table": [0x02000003, 0x02000004],
    "chair": [0x02000027, 0x02000028, 0x02000029, 0x0200002A],
    "sink": [0x0200011A, 0x0200011B],
    "tanks": [0x02000494, 0x02000495, 0x02000496, 0x02000497],
    "gaspump": [0x020004DB, 0x020004DC],
    "scrap_tank": [0x0200022C],
    "statue": [0x02000588],
    "chair_any": [0x02000027, 0x02000029, 0x02000033],
    "pole": [0x02000180, 0x02000181],
    "goo": [0x020002B9, 0x020002BA, 0x020002BB, 0x020002BC, 0x020002BD, 0x020002BE, 0x020002BF],
    "mushroom": [0x02000378, 0x02000379],
    "pipes": [0x0200001C, 0x0200019E, 0x0200019F, 0x020001A0],
    "waterpump": [0x02000388],
    "pumpctl": [0x020003EF],
    "puddle": [0x020003FB],
    "drain": [0x02000087, 0x02000088, 0x02000089, 0x0200008A],
    "suspension_tank": [0x0200022B, 0x0200022C],
    "watertank_s": [0x0200014B],
}

# what each kind of room holds, against its walls (pools) and in its middle
ROOMS = {
    "barracks": {"wall": ["bunk", "crate", "bunk", "barrel", "bunk", "crate"], "every": 3, "grid": ["table"], "gstep": 8},
    "office": {"wall": ["shelf_books", "desk", "shelf_books", "computer"], "every": 3, "grid": ["desk"], "gstep": 6},
    "store": {"wall": ["shelf_goods", "crate", "barrels", "sacks", "crate"], "every": 2, "grid": ["crate", "boxes", "crate"], "gstep": 3},
    "armory": {"wall": ["shelf_parts", "crate", "crate", "barrel"], "every": 2, "grid": ["table"], "gstep": 6},
    "kitchen": {"wall": ["stove", "shelf_goods", "barrel", "sacks", "sink", "stove"], "every": 3, "grid": ["table"], "gstep": 6},
    "mess": {"wall": ["barrel", "shelf_goods"], "every": 5, "grid": ["table", "chair"], "gstep": 4},
    "workshop": {"wall": ["shelf_parts", "tires", "barrels", "crate", "junk"], "every": 3, "grid": ["car", "cart", "tires"], "gstep": 8},
    "quarters": {"wall": ["mattress", "shelf_books", "barrel", "pot"], "every": 3, "grid": ["table"], "gstep": 8},
    "squat": {"wall": ["mattress", "junk", "trash", "barrel", "mattress"], "every": 3, "grid": ["firebarrel"], "gstep": 9},
    "machine": {"wall": ["generator", "console", "shelf_parts", "barrel"], "every": 3, "grid": ["generator"], "gstep": 8},
    "lab": {"wall": ["computer", "console", "shelf_parts", "computer"], "every": 3, "grid": ["table"], "gstep": 6},
    "ruin": {"wall": ["rubble", "debris", "junk", "scrub", "trash"], "every": 4, "grid": ["debris"], "gstep": 10},
    "empty": {"wall": [], "every": 9, "grid": [], "gstep": 9},
}

# small scenes (docs/MAP_STORYTELLING_GUIDE.md §4): (pool, dx, dy) in hexes
SCENES = {
    "driver": [("car", 0, 0), ("skeleton", 2, 1), ("barrel", -2, 2), ("barrel", -3, 3), ("junk", 3, -1)],
    "ambush": [("cart", 0, 0), ("crate", 2, 1), ("crate", 3, 3), ("tires", -2, 2), ("bones", 1, 3), ("bones", -1, -2), ("blood", 1, 2)],
    "camp": [("firepit", 0, 0), ("mattress", -3, 1), ("mattress", 2, 3), ("mattress", 3, -2), ("pot", 1, 0), ("crate", -2, -3), ("barrel", 4, 1)],
    "firebarrel_crowd": [("firebarrel", 0, 0), ("crate", 2, 0), ("crate", -1, 2), ("trash", 2, 2), ("junk", -2, -1)],
    "squatters": [("mattress", 0, 0), ("mattress", 2, 1), ("firebarrel", 4, 0), ("junk", -2, 1), ("trash", 1, 3), ("barrel", 5, 2)],
    "collapse": [("rubble", 0, 0), ("rubble", 2, 1), ("debris", -2, 1), ("bones", 1, 2), ("debris", 3, -1)],
    "graves": [],          # rows, built by graves()
    "salvage": [("tires", 0, 0), ("junk", 2, 0), ("junk", 2, 2), ("crate", -2, 1), ("barrel", 3, -2), ("junk", -3, -2)],
    "body_at_wall": [("body", 0, 0), ("blood", 1, 0), ("blood", 0, 1)],
    "supply": [("crate", 0, 0), ("crate", 1, 1), ("barrels", 3, 0), ("sacks", 0, 3), ("boxes", 2, 3)],
    "water_point": [("watertank", 0, 0), ("barrel", 3, 1), ("barrel", -2, 2), ("pot", 2, 3)],
}


class Site:
    def __init__(self, name, mid, seed, style="ruin_town"):
        self.name, self.id, self.seed = name, mid, seed
        self.rng = random.Random(seed)
        self.level = {"elev": 0, "buildings": [], "objects": [], "exits": [], "scatter": []}
        self.levels = [self.level]
        self.style = style
        self.start = None
        self.script = None
        self.taken = set()        # hexes with an object or wall
        self.walls = set()        # wall hexes of the current level
        self.doors = set()
        self.notes = []

    # -------------------------------------------------- levels and ground
    def new_level(self, elev):
        self.level = {"elev": elev, "buildings": [], "objects": [], "exits": [], "scatter": []}
        self.levels.append(self.level)
        self.taken, self.walls, self.doors = set(), set(), set()

    def ground(self, rect, classes=(), decay=None, palette=None, style=None, wear=None, lines=()):
        gg = {"rect": list(rect), "classes": [{"class": c, "rect": list(r)} for c, r in classes],
              "seed": self.seed, "style": style or self.style}
        if wear is not None:
            gg["paint"] = True
            gg["wear"] = wear
            gg["lines"] = [list(l) for l in lines]
        gg["decay"] = decay if decay is not None else True
        if palette:
            gg["palette"] = palette
        self.level["grown_ground"] = gg

    def layout(self, rect, wear=0.35, cell=7, base="sand"):
        """Start a tile layout (tools/tilewfc.py): every pair of neighbouring
        tiles will be one Fallout 2's own maps contain."""
        import groundkit
        import tilewfc
        if not hasattr(Site, "_G"):
            Site._G = tilewfc.Grammar()
        self.L = tilewfc.Layout(Site._G, rect, groundkit.Noise(random.Random(self.seed + 7), cell), wear, base)
        return self.L

    def solve_layout(self):
        tiles, relaxed = self.L.solve(random.Random(self.seed + 11 + self.level["elev"]))
        self.level["floor_tiles"] = [[x, y, t] for (x, y), t in sorted(tiles.items())]
        print(f"layout: {len(tiles)} squares, {relaxed} relaxed")

    def floor(self, rect, tiles):
        """A plain floor (caves, interiors) of tile names, squares."""
        self.level.setdefault("floor_fill", []).append({"rect": list(rect), "tiles": tiles})

    # ------------------------------------------------------------- walls
    def building(self, name, rect, family, doors=(), inner=(), rooms=(), floor=None, roof=None,
                 broken=0.0, source="fo2", gaps=()):
        """rect in squares (outer wall line). doors: [(side, offset_hexes)] on
        the outer wall, side N/S/W/E; or (hx, hy) for inner doors. inner:
        [(hx0, hy0, hx1, hy1)] wall lines. rooms: [(kind, (hx0, hy0, hx1, hy1))]
        interior hex rects to furnish."""
        sx0, sy0, sx1, sy1 = rect
        hx0, hy0, hx1, hy1 = sx0 * 2, sy0 * 2, sx1 * 2 + 1, sy1 * 2 + 1
        dh = []
        for d in doors:
            if isinstance(d[0], str):
                side, off = d
                if side == "N":
                    dh.append((hx0 + off, hy0))
                elif side == "S":
                    dh.append((hx0 + off, hy1))
                elif side == "W":
                    dh.append((hx0, hy0 + off))
                else:
                    dh.append((hx1, hy0 + off))
            else:
                dh.append(tuple(d))
        bd = {"name": name, "rect": list(rect), "family": family, "doors": [list(d) for d in dh],
              "inner": [list(i) for i in inner], "source": source, "seed": self.rng.randrange(1 << 30)}
        if floor:
            bd["floor"] = floor
        if roof:
            bd["roof"] = roof
        if broken:
            bd["broken"] = broken
            bd["floor_wear"] = min(0.8, broken + 0.1)
        if gaps:
            bd["gaps"] = [list(g) for g in gaps]
        self.level["buildings"].append(bd)
        cells = set()
        for x in range(hx0, hx1 + 1):
            cells |= {(x, hy0), (x, hy1)}
        for y in range(hy0, hy1 + 1):
            cells |= {(hx0, y), (hx1, y)}
        for (ax, ay, bx, by) in inner:
            if ay == by:
                cells |= {(x, ay) for x in range(min(ax, bx), max(ax, bx) + 1)}
            else:
                cells |= {(ax, y) for y in range(min(ay, by), max(ay, by) + 1)}
        self.walls |= cells
        self.taken |= cells
        self.doors |= set(dh)
        for kind, r in rooms:
            self.furnish(kind, r)
        return (hx0, hy0, hx1, hy1)

    def complex(self, name, rects, family, inner=(), doors=(), rooms=(), floor=None, source="fo2", holes=()):
        """An interior built as one shape: the union of square rects, walled
        round its outside, divided by inner wall lines (hex) with doors.
        floor: a tile family for the layout. holes: square rects left unfloored
        (a shaft, a pit). Returns the set of floor squares."""
        sq = set()
        for (x0, y0, x1, y1) in rects:
            sq |= {(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)}
        for (x0, y0, x1, y1) in holes:
            sq -= {(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)}
        hexes = {(2 * x + a, 2 * y + b) for (x, y) in sq for a in (0, 1) for b in (0, 1)}
        cells = {(x, y) for (x, y) in hexes
                 if any((x + dx, y + dy) not in hexes for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))}
        for (ax, ay, bx, by) in inner:
            if ay == by:
                cells |= {(x, ay) for x in range(min(ax, bx), max(ax, bx) + 1)}
            else:
                cells |= {(ax, y) for y in range(min(ay, by), max(ay, by) + 1)}
        dh = [tuple(d) for d in doors]
        self.level.setdefault("lines", []).append({"name": name, "cells": sorted(cells - set(dh)), "doors": [list(d) for d in dh],
                                                    "family": family, "source": source,
                                                    "seed": self.rng.randrange(1 << 30)})
        self.walls |= cells
        self.taken |= cells
        self.doors |= set(dh)
        if floor and hasattr(self, "L"):
            self.L.floor_cells(sq, floor)
        for kind, r in rooms:
            self.furnish(kind, r)
        return sq

    def cave(self, name, blobs, seed_extra=0, floor="cave", family="ca", doors=()):
        """A natural cave: blobs [(cx, cy, r)] in squares, grown into an
        irregular region, walled with the cave set round its edge."""
        import math
        rng = random.Random(self.seed * 31 + seed_extra)
        sq = set()
        for (cx, cy, r) in blobs:
            # a smooth wobbling outline: radius varies slowly with the angle
            k = [rng.uniform(-0.18, 0.18) for _ in range(4)]
            ph = [rng.uniform(0, 6.3) for _ in range(4)]
            for y in range(cy - r - 3, cy + r + 4):
                for x in range(cx - r - 3, cx + r + 4):
                    a = math.atan2(y - cy, x - cx)
                    rr = r * (1 + sum(k[i] * math.sin((i + 2) * a + ph[i]) for i in range(4)))
                    if math.hypot(x - cx, y - cy) <= rr:
                        sq.add((x, y))
        # passages joining each chamber to the next, so the cave is one run
        for (ax, ay, _), (bx, by, _) in zip(blobs, blobs[1:]):
            n = int(max(abs(bx - ax), abs(by - ay))) + 1
            for i in range(n + 1):
                t = i / n
                px = ax + (bx - ax) * t + math.sin(t * 3.1) * rng.uniform(-1.5, 1.5)
                py = ay + (by - ay) * t
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        sq.add((int(round(px)) + dx, int(round(py)) + dy))
        # close one-square notches, then fill every hole
        for _ in range(2):
            ring = {(x + dx, y + dy) for (x, y) in sq for dx in (-1, 0, 1) for dy in (-1, 0, 1)} - sq
            sq |= {c for c in ring if sum((c[0] + dx, c[1] + dy) in sq for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))) >= 3}
            sq = {c for c in sq if sum((c[0] + dx, c[1] + dy) in sq for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))) >= 2}
        xs = [c[0] for c in sq]; ys = [c[1] for c in sq]
        x0, x1, y0, y1 = min(xs) - 1, max(xs) + 1, min(ys) - 1, max(ys) + 1
        out, stack = set(), [(x0, y0)]
        while stack:
            c = stack.pop()
            if c in out or c in sq or not (x0 <= c[0] <= x1 and y0 <= c[1] <= y1):
                continue
            out.add(c)
            stack += [(c[0] + 1, c[1]), (c[0] - 1, c[1]), (c[0], c[1] + 1), (c[0], c[1] - 1)]
        sq = {(x, y) for x in range(x0, x1 + 1) for y in range(y0, y1 + 1)} - out
        self.wall_squares(name, sq, family, doors, floor)
        return sq

    def wall_squares(self, name, sq, family, doors=(), floor=None, open_to=()):
        """Wall the outside of any set of floor squares. open_to: squares of
        a neighbouring region: no wall where the two touch (a cave opening
        into a room)."""
        hexes = {(2 * x + a, 2 * y + b) for (x, y) in sq for a in (0, 1) for b in (0, 1)}
        other = {(2 * x + a, 2 * y + b) for (x, y) in open_to for a in (0, 1) for b in (0, 1)}
        cells = {(x, y) for (x, y) in hexes
                 if any((x + dx, y + dy) not in hexes and (x + dx, y + dy) not in other
                        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))}
        dh = [tuple(d) for d in doors]
        self.level.setdefault("lines", []).append({"name": name, "cells": sorted(cells - set(dh)), "doors": [list(d) for d in dh],
                                                    "family": family, "source": "fo2",
                                                    "seed": self.rng.randrange(1 << 30)})
        self.walls |= cells
        self.taken |= cells
        self.doors |= set(dh)
        if floor and hasattr(self, "L"):
            self.L.floor_cells(sq, floor)

    def fence(self, name, path, family, gates=(), source="fo2"):
        """A fence or wall line along a path of hex points (axis-aligned
        segments), with gate hexes left open."""
        cells = set()
        for (ax, ay), (bx, by) in zip(path, path[1:]):
            if ay == by:
                cells |= {(x, ay) for x in range(min(ax, bx), max(ax, bx) + 1)}
            else:
                cells |= {(ax, y) for y in range(min(ay, by), max(ay, by) + 1)}
        gates = {tuple(g) for g in gates}
        self.level.setdefault("lines", []).append({"name": name, "cells": sorted(cells - gates), "family": family,
                                                    "source": source, "seed": self.rng.randrange(1 << 30)})
        self.walls |= cells - gates
        self.taken |= cells - gates

    # ----------------------------------------------------------- objects
    def put(self, pool, hx, hy, rot=0, force=False):
        if not force and ((hx, hy) in self.taken or self.near_door(hx, hy)):
            return False
        pid = self.rng.choice(P[pool]) if isinstance(pool, str) else pool
        self.level["objects"].append({"pid": f"0x{pid:08X}", "hex": [hx, hy], "rot": rot})
        self.taken.add((hx, hy))
        return True

    def near_door(self, hx, hy, r=2):
        return any(abs(hx - dx) <= r and abs(hy - dy) <= r for dx, dy in self.doors)

    def furnish(self, kind, r):
        """Furniture against the inside of a room's north and west walls (the
        ones the player sees), every few hexes, clear of doors; a loose grid
        of the room's middle pieces, leaving a walkway."""
        spec = ROOMS[kind]
        x0, y0, x1, y1 = r
        every = spec["every"]
        k = 0
        edge = [(x, y0) for x in range(x0 + 1, x1, every)] + [(x0, y) for y in range(y0 + every, y1, every)]
        for (x, y) in edge:
            if spec["wall"]:
                self.put(spec["wall"][k % len(spec["wall"])], x, y)
                k += 1
        g, step = spec.get("grid", []), spec.get("gstep", 6)
        if g and x1 - x0 >= 5 and y1 - y0 >= 5:
            i = 0
            for y in range(y0 + 3, y1 - 1, step):
                for x in range(x0 + 3, x1 - 1, step):
                    self.put(g[i % len(g)], x, y)
                    i += 1

    def scene(self, kind, hx, hy):
        for pool, dx, dy in SCENES[kind]:
            self.put(pool, hx + dx, hy + dy)

    def graves(self, hx, hy, rows, cols, pool="cross", step=(3, 3)):
        for r in range(rows):
            for c in range(cols):
                self.put(pool, hx + c * step[0], hy + r * step[1], force=True)

    def row(self, pool, hx, hy, n, dx=0, dy=2):
        for i in range(n):
            self.put(pool, hx + i * dx, hy + i * dy)

    def clutter_walls(self, pools, chance=0.12, outside=True, rect=None):
        """Weeds, trash and junk against the outside of every wall, the
        originals' ~0.3 objects per square near buildings."""
        for (x, y) in sorted(self.walls):
            if rect and not (rect[0] <= x <= rect[2] and rect[1] <= y <= rect[3]):
                continue
            if self.rng.random() > chance:
                continue
            for dx, dy in self.rng.sample([(0, 1), (1, 0), (-1, 0), (0, -1), (1, 1), (-1, 1)], 6):
                if (x + dx, y + dy) not in self.walls and self.put(self.rng.choice(pools), x + dx, y + dy):
                    break

    def sprinkle(self, pools, rect, density=0.05):
        """Open ground: sparse (the originals: ~0.05 per square)."""
        x0, y0, x1, y1 = rect
        n = int((x1 - x0 + 1) * (y1 - y0 + 1) / 4 * density)
        for _ in range(n):
            self.put(self.rng.choice(pools), self.rng.randint(x0, x1), self.rng.randint(y0, y1))

    def exit(self, rect, to=-2, tile=None, elev=0, rot=0):
        e = {"hexes": list(rect), "to": to}
        if tile is not None:
            e.update({"tile": tile, "elev": elev, "rot": rot})
        self.level["exits"].append(e)

    # -------------------------------------------------------------- save
    def save(self, scroll=None):
        r = {"name": self.name, "id": self.id, "seed": self.seed,
             "start": {"hex": list(self.start[:2]), "elev": self.start[2] if len(self.start) > 2 else 0,
                       "rot": self.start[3] if len(self.start) > 3 else 0},
             "levels": self.levels}
        if self.script:
            r["script"] = self.script
        if scroll:
            self.levels[0]["scroll_ring"] = {"rect": list(scroll)}
        out = ROOT / "mod" / "maps_src" / f"{self.name.lower()}.compose.json"
        out.write_text(json.dumps(r, indent=1), encoding="utf-8")
        print(f"wrote {out.relative_to(ROOT)}")
        return out
