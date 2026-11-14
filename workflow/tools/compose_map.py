#!/usr/bin/env python3
"""
compose_map.py - build a FOC map from a recipe (mod/maps_src/<name>.compose.json)
by lifting whole levels or boxes out of other games' maps, converting their
ids into FOC's (port_stamp.Converter), and adding FOC's own exits.

    python tools/compose_map.py mod/maps_src/rwfac1.compose.json --preview
    python tools/compose_map.py mod/maps_src/rwfac1.compose.json --write [--force]

Each FOC elevation is built from one or more `lift`s. A lift copies the
floor, the roof and the scenery and walls of a box of a source map (all of
it when no rect is given), shifted by `shift` squares. What never comes
across: critters (FOC's scripts put people in), misc objects (the source's
exit grids and scroll blockers), loose items (the source's loot), and stairs,
ladders and elevators (they carry the source game's map links). Containers
come across empty. `drop` lists art-name regexes to leave out (logos,
signs, things that name the source game's places).

Recipe:
  {
    "name": "RWFAC1", "id": 214, "script": "SCRIPT_RWFAC1",
    "start": {"hex": [x, y], "elev": 0, "rot": 2},
    "levels": [
      {"elev": 0,
       "lift": [{"source": "olympus", "map": "lvfilter", "elev": 0,
                 "rect": [x0, y0, x1, y1], "shift": [dx, dy]}],
       "drop": ["^logo"],
       "exits": [{"hexes": [x0, y0, x1, y1], "to": 200, "tile": 12345,
                  "elev": 0, "rot": 2}]}
    ]
  }

Squares are 0..99 (a hex is half a square each way). `to` is a maps.txt id,
-2 the world map. The written map goes to mod/data/maps and
base/mods/foc/maps; ported art lands above the 4096 cap, so run
tools/repack_art.py after, then map_fixup.py (flags) and the blocker tools.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import port  # noqa: E402
from foclib.paths import inside  # noqa: E402
from foclib import GameIndex, MapFile, parse_lst  # noqa: E402
from foclib.formats import hex_xy, pid_type  # noqa: E402
from mapbuild import HEX_W, MapBuilder, MapObjectSpec, extras_for  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
AUTHORED = ROOT / "mod" / "data" / "maps"
DEPLOYED = ROOT / "base" / "mods" / "foc" / "maps"
IDS_H = ROOT / "scripts_src" / "foc" / "foc_ids.h"
EXIT_GRID = 0x05000010
BLOCKING_HEX = 0x02000043                  # scenery 67, Secret Blocking Hex
SCROLL_BLOCKER = 0x0500000C
from foclib import config as _config  # noqa: E402
STAMPS = _config.stamps_dir()
DEFAULT_ROCKS = [0x0200004A, 0x0200004B, 0x0200004E, 0x0200004F, 0x02000050, 0x02000051,
                 0x020001AC, 0x020001AD, 0x020001AE, 0x020001AF, 0x02000116, 0x02000117]
DIRS = {0: (-1, HEX_W - 1, HEX_W, HEX_W + 1, 1, -HEX_W),
        1: (-HEX_W - 1, -1, HEX_W, 1, 1 - HEX_W, -HEX_W)}
LINKED_SCENERY = (1, 2, 3, 4)          # stairs, elevator, ladder up, ladder down


def script_number(name: str) -> int:
    m = re.search(rf"#define\s+{re.escape(name)}\s+\((\d+)\)", IDS_H.read_text(encoding="utf-8"))
    if not m:
        sys.exit(f"{name} is not in foc_ids.h (register the script and run gen_ids.py)")
    return int(m.group(1))


class Composer:
    def __init__(self, recipe: dict, dry_run: bool):
        self.r = recipe
        self.dry = dry_run
        self.gi = GameIndex(pristine=False)
        levels = [lv["elev"] for lv in recipe["levels"]]
        st = recipe.get("start", {})
        sx, sy = st.get("hex", [100, 100])
        script = script_number(recipe["script"]) if recipe.get("script") else -1
        self.mb = MapBuilder(recipe["name"], elevations=tuple(levels),
                             start_tile=sy * HEX_W + sx, start_elevation=st.get("elev", levels[0]),
                             start_orientation=st.get("rot", 0), map_id=recipe.get("id", -1),
                             map_script_index=script)
        self.converters: dict[str, object] = {}
        self.stats: dict[str, int] = {}
        self._matcher = None

    def matcher(self):
        """port_stamp.TileMatcher: the closest-looking FOC tile for a source tile
        FOC lacks (tiles cannot be ported past gecko's 12-bit tile ids)."""
        if self._matcher is None:
            from port_stamp import Foc, TileMatcher
            self._matcher = TileMatcher(Foc())
        return self._matcher

    def converter(self, source: str):
        if source not in self.converters:
            from port_stamp import Converter, Foc
            src = port.open_source(source)
            self.converters[source] = (Converter(Foc(), source, src, self.dry, self.r["name"].lower()), src)
        return self.converters[source]

    def convert_pid(self, source: str, pid: int):
        """A source game's pid -> (FOC pid, FOC fid), or None. Fallout 2's
        own pids are FOC's already."""
        key = (source, pid)
        if not hasattr(self, "_pidcache"):
            self._pidcache = {}
        if key in self._pidcache:
            return self._pidcache[key]
        out = None
        if source == "fo2":
            pr = self.gi.proto(pid)
            out = (pid, pr.fid) if pr is not None else None
        else:
            conv, src = self.converter(source)
            spr = src.proto(pid)
            if spr is not None:
                np = conv.map_pid(pid)
                nf = conv.map_frm(pid, spr.fid, np) if np else None
                out = (np, nf) if np and nf is not None else None
        self._pidcache[key] = out
        return out

    def floor_tiles(self, level: dict):
        """Tiles solved by tools/tilewfc.py: "floor_tiles": [[sx, sy, tile id], ...]."""
        for sx, sy, t in level.get("floor_tiles", []):
            self.mb.set_floor(level["elev"], sx, sy, t)
        if level.get("floor_tiles"):
            print(f"  floor tiles: {len(level['floor_tiles'])} squares (tile grammar)")

    def grown_ground(self, level: dict):
        """Ground grown square by square from Fallout 2's own tile rules
        (tools/groundkit.py): {"rect": [sx0, sy0, sx1, sy1], "classes":
        [{"class": "road"|"walk"|"edge"|"dirt", "rect": [...]}, ...],
        "decay": {"crumble": 0.18, "drift": 0.12}, "palette": {class: [tile names]},
        "seed": n}. Squares not named are dirt."""
        gg = level.get("grown_ground")
        if not gg:
            return
        import random
        import groundkit
        rng = random.Random(gg.get("seed", self.r.get("seed", 1)))
        x0, y0, x1, y1 = gg["rect"]
        target = {(x, y): "dirt" for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)}
        for c in gg.get("classes", []):
            cx0, cy0, cx1, cy1 = c["rect"]
            for y in range(cy0, cy1 + 1):
                for x in range(cx0, cx1 + 1):
                    if (x, y) in target:
                        target[(x, y)] = c["class"]
        if gg.get("paint"):
            from foclib import parse_lst
            if not hasattr(self, "_tiles_lst"):
                self._tiles_lst = [x.split(",")[0].lower() for x in parse_lst(self.gi.vfs.read("art\\tiles\\tiles.lst"))]
            tl = self._tiles_lst
            lines = set()
            for ln in gg.get("lines", []):
                lx0, ly0, lx1, ly1 = ln
                lines |= {(x, y) for y in range(ly0, ly1 + 1) for x in range(lx0, lx1 + 1)}
            names = groundkit.paint(target, rng, gg.get("wear", 0.35), gg.get("style", "ruin_town"), lines)
            for (x, y), nm in names.items():
                if nm.lower() in tl:
                    self.mb.set_floor(level["elev"], x, y, tl.index(nm.lower()))
            print(f"  painted ground: {len(names)} squares")
            return
        if gg.get("decay", True):
            d = gg.get("decay") if isinstance(gg.get("decay"), dict) else {}
            target = groundkit.decay_target(target, rng, d.get("crumble", 0.18), d.get("drift", 0.12))
        target = groundkit.edge_ring(target)
        pal = None
        if gg.get("palette"):
            G = groundkit.g(gg.get("style", "ruin_town"))
            byname = {n.lower(): t for t, n in G["names"].items()}
            pal = {c: [byname[n.lower()] for n in names if n.lower() in byname] for c, names in gg["palette"].items()}
        tiles = groundkit.grow(target, rng, pal, (x0, y0, x1, y1), gg.get("style", "ruin_town"))
        for (x, y), t in tiles.items():
            self.mb.set_floor(level["elev"], x, y, t)
        print(f"  grown ground: {len(tiles)} squares")

    def lines(self, level: dict):
        """Fences and walls along a line: "lines": [{"cells": [[hx, hy], ...],
        "family": "fence", "source": "fo2", "seed": n}]."""
        import random
        import wallkit
        for i, ln in enumerate(level.get("lines", [])):
            rng = random.Random(ln.get("seed", i))
            doors = [tuple(d) for d in ln.get("doors", [])]
            cells = {tuple(c) for c in ln["cells"]} | set(doors)
            n = wallkit.draw(self.add, self.convert_pid, ln["family"], cells, level["elev"], rng, doors,
                             ln.get("door_pref"), ln.get("source", "fo2"), None)
            print(f"  line {ln.get('name', i)} ({ln['family']}): {n} pieces")

    def floor_fill(self, level: dict):
        """Plain floors: "floor_fill": [{"rect": [sx0, sy0, sx1, sy1], "tiles": [names]}]."""
        import random
        from foclib import parse_lst
        if not level.get("floor_fill"):
            return
        if not hasattr(self, "_tiles_lst"):
            self._tiles_lst = [x.split(",")[0].lower() for x in parse_lst(self.gi.vfs.read("art\\tiles\\tiles.lst"))]
        tl = self._tiles_lst
        rng = random.Random(self.r.get("seed", 1) * 53 + level["elev"])
        for ff in level["floor_fill"]:
            ids = [tl.index(n.lower()) for n in ff["tiles"] if n.lower() in tl]
            x0, y0, x1, y1 = ff["rect"]
            for y in range(y0, y1 + 1):
                for x in range(x0, x1 + 1):
                    self.mb.set_floor(level["elev"], x, y, rng.choice(ids))

    def walls(self, level: dict):
        """Buildings drawn with a wall family learned from the original
        games (tools/wallkit.py), never copied: "buildings": [{"rect":
        [sx0, sy0, sx1, sy1] (squares, outer walls), "family": "nec",
        "doors": [[hx, hy], ...], "inner": [[hx0, hy0, hx1, hy1], ...] (inner
        wall lines, hexes), "floor": "tile name", "roof": "tile name",
        "seed": n, "broken": 0.0-1.0 (share of wall hexes left out, for
        ruins), "no_roof": true}]."""
        import random
        import wallkit
        de = level["elev"]
        G = None
        for i, bd in enumerate(level.get("buildings", [])):
            rng = random.Random(bd.get("seed", self.r.get("seed", 1) * 31 + i))
            sx0, sy0, sx1, sy1 = bd["rect"]
            hx0, hy0, hx1, hy1 = sx0 * 2, sy0 * 2, sx1 * 2 + 1, sy1 * 2 + 1
            cells = set()
            for x in range(hx0, hx1 + 1):
                cells.add((x, hy0))
                cells.add((x, hy1))
            for y in range(hy0, hy1 + 1):
                cells.add((hx0, y))
                cells.add((hx1, y))
            for (ax, ay, bx, by) in bd.get("inner", []):
                if ay == by:
                    for x in range(min(ax, bx), max(ax, bx) + 1):
                        cells.add((x, ay))
                else:
                    for y in range(min(ay, by), max(ay, by) + 1):
                        cells.add((ax, y))
            doors = [tuple(d) for d in bd.get("doors", [])]
            gaps = {tuple(g) for g in bd.get("gaps", [])}
            broken = bd.get("broken", 0)
            if broken:
                # ruins: whole stretches gone, not single hexes
                runs = sorted(cells)
                for c in runs:
                    if rng.random() < broken / 4 and c not in doors:
                        for k in range(rng.randint(2, 6)):
                            gaps.add((c[0] + k, c[1]))
                            gaps.add((c[0], c[1] + k))
            draw_cells = (cells - gaps) | set(doors)
            n = wallkit.draw(self.add, self.convert_pid, bd["family"], draw_cells, de, rng, doors,
                             bd.get("door_pref"), bd.get("source", "fo2"), bd.get("style", 0))
            # floor and roof
            if bd.get("floor") or bd.get("roof"):
                import groundkit
                from foclib import parse_lst
                if not hasattr(self, "_tiles_lst"):
                    self._tiles_lst = [x.split(",")[0].lower() for x in parse_lst(self.gi.vfs.read("art\\tiles\\tiles.lst"))]
                tl = self._tiles_lst

                def tid(name):
                    names = name if isinstance(name, list) else [name]
                    ids = [tl.index(nm.lower()) for nm in names if nm.lower() in tl]
                    return ids
                fl = tid(bd["floor"]) if bd.get("floor") else []
                rf = tid(bd["roof"]) if bd.get("roof") and not bd.get("no_roof") else []
                wear = bd.get("floor_wear", 0)
                nz = None
                if wear:
                    import groundkit
                    nz = groundkit.Noise(rng, 3)
                for sy in range(sy0, sy1 + 1):
                    for sx in range(sx0, sx1 + 1):
                        if fl and not (nz and 0.7 * nz(sx, sy) + 0.3 * rng.random() < wear):
                            self.mb.set_floor(de, sx, sy, rng.choice(fl))
                        if rf:
                            self.mb.set_roof(de, sx, sy, rng.choice(rf))
            print(f"  building {bd.get('name', i)} ({bd['family']}): {n} wall pieces, {len(doors)} doors")

    def add(self, pid, fid, tile, elev, rot=0, frame=0, extras=None):
        pr = self.gi.proto(pid)
        if extras is None:
            extras = extras_for(pid_type(pid), pid, pr.subtype if pr else None)
        self.mb.add(MapObjectSpec(pid=pid, fid=fid, tile=tile, elevation=elev,
                                  rotation=rot, frame=frame, extras=extras))

    def lift(self, level: dict, spec: dict):
        conv, src = self.converter(spec["source"])
        name = spec["map"] if spec["map"].lower().endswith(".map") else spec["map"] + ".map"
        m = MapFile.parse(src.vfs.read(f"maps\\{name}"), src.proto)
        se = spec.get("elev", 0)
        ei = m.elevations.index(se)
        x0, y0, x1, y1 = spec.get("rect", [0, 0, 99, 99])
        dx, dy = spec.get("shift", [0, 0])
        de = level["elev"]
        drop = [re.compile(p, re.I) for p in level.get("drop", []) + spec.get("drop", [])]
        # "keep": [regex, ...] lifts only scenery/walls whose art matches (a
        # plane out of a boneyard); "no_floor": true leaves floor and roof behind
        keep = [re.compile(p, re.I) for p in spec.get("keep", [])]
        sc_art = parse_lst(src.vfs.read("art\\scenery\\scenery.lst"))
        wl_art = parse_lst(src.vfs.read("art\\walls\\walls.lst"))

        def inside(sx, sy):
            return x0 <= sx <= x1 and y0 <= sy <= y1

        # Most levels pave the whole grid with one background tile around the
        # rooms; leave it behind so a lifted room keeps only its own floor.
        filler = None
        if not spec.get("keep_filler"):
            from collections import Counter
            cnt = Counter(v & 0xFFF for v in m.floors[ei] if (v & 0xFFF) > 1)
            if cnt:
                tid, c = cnt.most_common(1)[0]
                if c > 0.4 * sum(cnt.values()):
                    filler = tid
        # floored squares of the box, less isolated specks (background tiles
        # some maps sprinkle round their rooms): a square needs two floored
        # neighbours, applied twice
        fl = {k for k in range(10000) if inside(k % 100, k // 100)
              and (m.floors[ei][k] & 0xFFF) > 1 and (m.floors[ei][k] & 0xFFF) != filler}
        for _ in range(2):
            fl = {k for k in fl if sum((k + d) in fl for d in (-1, 1, -100, 100)) >= 2}
        tiles = 0
        for k in range(10000 if not spec.get("no_floor") else 0):
            sx, sy = k % 100, k // 100
            if not inside(sx, sy):
                continue
            tx, ty = sx + dx, sy + dy
            if not (0 <= tx < 100 and 0 <= ty < 100):
                continue
            v = m.floors[ei][k] & 0xFFF
            if k in fl:
                t = conv.map_tile(v, self.matcher)
                if t:
                    self.mb.set_floor(de, tx, ty, t)
                    tiles += 1
            v = m.roofs[ei][k] & 0xFFF
            if v > 1:
                t = conv.map_tile(v, self.matcher)
                if t:
                    self.mb.set_roof(de, tx, ty, t)
        n = skipped = 0
        for o in m.objects:
            if o.elevation != se or not (0 <= o.tile < 40000):
                continue
            hx, hy = hex_xy(o.tile)
            if not inside(hx // 2, hy // 2):
                continue
            t = pid_type(o.pid)
            if t in (1, 4, 5):
                continue
            pr = src.proto(o.pid)
            if t == 0 and not (pr and pr.subtype == 1):        # loose items (containers stay)
                continue
            if t == 2 and pr is not None and pr.subtype in LINKED_SCENERY:
                skipped += 1
                continue
            lst = sc_art if t == 2 else wl_art if t == 3 else None
            art = lst[o.fid & 0xFFF].lower() if lst and (o.fid & 0xFFF) < len(lst) else ""
            if art and any(p.search(art) for p in drop):
                continue
            if keep and not (art and any(p.search(art) for p in keep)):
                continue
            pid = conv.map_pid(o.pid)
            fid = conv.map_frm(o.pid, o.fid, pid) if pid else None
            if pid is None or fid is None:
                skipped += 1
                continue
            nx, ny = hx + dx * 2, hy + dy * 2
            if not (0 <= nx < 200 and 0 <= ny < 200):
                continue
            self.add(pid, fid, ny * HEX_W + nx, de, o.rotation, o.frame)
            n += 1
        print(f"  lift {spec['source']}:{name} elev {se} -> elev {de}: {tiles} tiles, {n} objects ({skipped} left out)")

    def exits(self, level: dict):
        de = level["elev"]
        pr = self.gi.proto(EXIT_GRID)
        for ex in level.get("exits", []):
            x0, y0, x1, y1 = ex["hexes"]
            placed = 0
            for hy in range(y0, y1 + 1):
                for hx in range(x0, x1 + 1):
                    sq = (hy // 2) * 100 + hx // 2
                    if self.mb.floors[de][sq] <= 1:
                        continue
                    self.add(EXIT_GRID, pr.fid, hy * HEX_W + hx, de,
                             extras=(ex["to"], ex.get("tile", -1), ex.get("elev", 0), ex.get("rot", 0)))
                    placed += 1
            print(f"  exits to {ex['to']}: {placed} hexes")

    def tunnels(self, level: dict):
        """Rock-lined passages between lifted pieces. Each tunnel is a path of
        square points joined by L-shaped runs (x first, then y), `width`
        squares wide. Its squares get cave floor (`floor` tile ids); wall and
        blocking scenery standing on them is removed, so a tunnel breaks into
        any room it reaches; every hex next to it with no floor gets a Secret
        Blocking Hex, and about one in `rock_every` of those a rock."""
        import random
        de = level["elev"]
        rng = random.Random(self.r.get("seed", 1) * 7 + de)
        for tn in level.get("tunnels", []):
            w = tn.get("width", 2)
            floor = tn.get("floor", [21, 22, 23, 24])
            rocks = [int(x, 16) if isinstance(x, str) else x for x in tn.get("rocks", DEFAULT_ROCKS)]
            pts = tn["path"]
            sq = set()

            def stamp(x, y):
                for ox in range(-(w // 2), w - w // 2):
                    for oy in range(-(w // 2), w - w // 2):
                        if 0 <= x + ox < 100 and 0 <= y + oy < 100:
                            sq.add((x + ox, y + oy))
            for (ax, ay), (bx, by) in zip(pts, pts[1:]):
                step = 1 if bx >= ax else -1
                for x in range(ax, bx + step, step):
                    stamp(x, ay)
                step = 1 if by >= ay else -1
                for y in range(ay, by + step, step):
                    stamp(bx, y)
            keep_floor = tn.get("keep_room_floor", True)
            for (x, y) in sq:
                if keep_floor and self.mb.floors[de][y * 100 + x] > 1 and (x, y) not in self._tunnel_sq.get(de, set()):
                    continue
                self.mb.set_floor(de, x, y, rng.choice(floor))
            self._tunnel_sq.setdefault(de, set()).update(sq)
            hexes = {(x * 2 + a, y * 2 + b) for (x, y) in sq for a in (0, 1) for b in (0, 1)}
            before = len(self.mb.objects)
            kept = []
            for o in self.mb.objects:
                if o.elevation == de and (o.tile % HEX_W, o.tile // HEX_W) in hexes:
                    t = pid_type(o.pid)
                    if t == 3 or (t == 2 and o.pid not in rocks):
                        pr = self.gi.proto(o.pid)
                        if t == 3 or (pr is not None and not (pr.flags & 0x10)):
                            continue
                kept.append(o)
            self.mb.objects = kept
            breached = before - len(kept)
            border = set()
            for (hx, hy) in hexes:
                tile = hy * HEX_W + hx
                for d in (DIRS[hx & 1]):
                    n = tile + d
                    nx, ny = n % HEX_W, n // HEX_W
                    if not (0 <= n < 40000) or abs(nx - hx) > 1 or (nx, ny) in hexes:
                        continue
                    if self.mb.floors[de][(ny // 2) * 100 + nx // 2] <= 1:
                        border.add(n)
            blk = self.gi.proto(BLOCKING_HEX)
            for k, n in enumerate(sorted(border)):
                self.add(BLOCKING_HEX, blk.fid, n, de)
                if rng.random() < 1.0 / tn.get("rock_every", 2):
                    rp = rng.choice(rocks)
                    pr = self.gi.proto(rp)
                    if pr is not None:
                        self.add(rp, pr.fid, n, de, rng.randrange(6))
            print(f"  tunnel {pts}: {len(sq)} squares, {breached} wall pieces broken through, {len(border)} border hexes")

    def scatter(self, level: dict):
        """Scatter scenery over floored, empty hexes of a square rect:
        {"pids": [...], "count": N, "rect": [x0, y0, x1, y1]}."""
        import random
        de = level["elev"]
        rng = random.Random(self.r.get("seed", 1) * 13 + de)
        taken = {o.tile for o in self.mb.objects if o.elevation == de}
        walls = [(o.tile % HEX_W, o.tile // HEX_W) for o in self.mb.objects
                 if o.elevation == de and pid_type(o.pid) == 3]
        for sc in level.get("scatter", []):
            pids = [int(x, 16) if isinstance(x, str) else x for x in sc["pids"]]
            x0, y0, x1, y1 = sc["rect"]
            # "clear_walls": N keeps the scatter N hexes off any wall, so no bush
            # lands in a doorway
            cw = sc.get("clear_walls", 0)
            near = {(wx + a, wy + b) for wx, wy in walls for a in range(-cw, cw + 1)
                    for b in range(-cw, cw + 1)} if cw else set()
            cells = [(hx, hy) for hy in range(y0 * 2, y1 * 2 + 2) for hx in range(x0 * 2, x1 * 2 + 2)
                     if self.mb.floors[de][(hy // 2) * 100 + hx // 2] > 1 and hy * HEX_W + hx not in taken
                     and (hx, hy) not in near]
            rng.shuffle(cells)
            n = 0
            for hx, hy in cells[:sc.get("count", 20)]:
                pid = rng.choice(pids)
                pr = self.gi.proto(pid)
                if pr is None:
                    continue
                t = hy * HEX_W + hx
                # facing: 0 unless the recipe lists real ones ("rots": [0, 2] for
                # cars, whose other directions are placeholder frames)
                self.add(pid, pr.fid, t, de, rng.choice(sc.get("rots", [0])))
                taken.add(t)
                n += 1
            print(f"  scatter {len(pids)} kinds in {sc['rect']}: {n}")

    def openings(self, level: dict):
        """Knock doorways through stamped walls: "openings": [[hx, hy], ...]
        removes the walls and blocking scenery on those hexes (not doors)."""
        de = level["elev"]
        holes = {hy * HEX_W + hx for hx, hy in level.get("openings", [])}
        # "clear": [[hx0, hy0, hx1, hy1], ...] empties rooms of their furniture
        # (scenery other than doors and blockers) so they can be furnished anew
        clear = level.get("clear", [])
        if not holes and not clear:
            return

        def in_clear(tile):
            x, y = tile % HEX_W, tile // HEX_W
            return any(x0 <= x <= x1 and y0 <= y <= y1 for x0, y0, x1, y1 in clear)
        kept = []
        cleared = 0
        for o in self.mb.objects:
            if o.elevation == de and o.tile in holes:
                t = pid_type(o.pid)
                pr = self.gi.proto(o.pid)
                if t == 3 or (t == 2 and pr is not None and pr.subtype != 0 and not (pr.flags & 0x10)):
                    continue
            if o.elevation == de and clear and pid_type(o.pid) in (0, 2) and in_clear(o.tile):
                pr = self.gi.proto(o.pid)
                if pr is not None and not (pid_type(o.pid) == 2 and pr.subtype == 0)                         and self.scenery_art_name(o.fid).lower() != "block.frm":
                    cleared += 1
                    continue
            kept.append(o)
        print(f"  openings: {len(self.mb.objects) - len(kept) - cleared} pieces removed on {len(holes)} hexes; "
              f"{cleared} furnishings cleared")
        self.mb.objects = kept

    def placed(self, level: dict):
        de = level["elev"]
        for ob in level.get("objects", []):
            pid = int(ob["pid"], 16) if isinstance(ob["pid"], str) else ob["pid"]
            pr = self.gi.proto(pid)
            if pr is None:
                sys.exit(f"no proto 0x{pid:08X}")
            hx, hy = ob["hex"]
            self.add(pid, pr.fid, hy * HEX_W + hx, de, ob.get("rot", 0))

    def ground(self, level: dict):
        """Outdoor ground: {"rect": [x0, y0, x1, y1], "tiles": [...], "weights": [...]}
        and road strips {"rect": [...], "tile": id} (squares, inclusive)."""
        import random
        de = level["elev"]
        rng = random.Random(self.r.get("seed", 1) * 17 + de)
        g = level.get("ground")
        if g:
            x0, y0, x1, y1 = g["rect"]
            for sy in range(y0, y1 + 1):
                for sx in range(x0, x1 + 1):
                    self.mb.set_floor(de, sx, sy, rng.choices(g["tiles"], g.get("weights"))[0])
        for rd in level.get("roads", []):
            x0, y0, x1, y1 = rd["rect"]
            for sy in range(y0, y1 + 1):
                for sx in range(x0, x1 + 1):
                    self.mb.set_floor(de, sx, sy, rd["tile"])

    def stamps(self, level: dict):
        """Gecko stamps from the library (FOC ids): {"file": "Ruins/....json",
        "at": [hx, hy], "variant": 0}. Objects only; a stamp's floor/roof, if
        any, is laid too."""
        de = level["elev"]
        for st in level.get("stamps", []):
            path = STAMPS / st["file"]
            d = json.loads(path.read_text(encoding="utf-8"))
            v = d["variants"][st.get("variant", 0)]
            ax, ay = st["at"]
            # "shell": true keeps the building and leaves its furniture behind
            # (walls, doors, and scenery whose art matches "shell_keep"), so
            # the rooms can be furnished for their use here
            shell = st.get("shell", False)
            shell_keep = [re.compile(x, re.I) for x in st.get("shell_keep", [])]
            n = 0
            for o in v["objects"]:
                pid, fid = o["proPid"], o["frmPid"]
                if (pid >> 24) in (1, 5):
                    continue
                pr = self.gi.proto(pid)
                if pr is None:
                    continue
                if shell and (pid >> 24) == 2 and pr.subtype != 0:
                    art = self.scenery_art_name(fid)
                    if not any(k.search(art) for k in shell_keep):
                        continue
                hx, hy = ax + o["dxHex"], ay + o["dyHex"]
                if 0 <= hx < 200 and 0 <= hy < 200:
                    self.add(pid, fid, hy * HEX_W + hx, de, o.get("direction", 0))
                    n += 1
            print(f"  stamp {st['file']} at {st['at']}: {n} objects")

    def scenery_art_name(self, fid: int) -> str:
        if not hasattr(self, "_sc_lst"):
            self._sc_lst = parse_lst(self.gi.vfs.read("art\\scenery\\scenery.lst"))
        i = fid & 0xFFF
        return self._sc_lst[i] if i < len(self._sc_lst) else ""

    def stamp_fill(self, level: dict):
        """Pack stamps into hex blocks, shelf by shelf, `gap` hexes apart:
        {"rect": [hx0, hy0, hx1, hy1], "files": [...], "gap": 3}. Stamps that
        don't fit are left out (and said so)."""
        for blk in level.get("stamp_fill", []):
            x0, y0, x1, y1 = blk["rect"]
            gap = blk.get("gap", 3)
            x, y, shelf = x0, y0, 0
            placed = []
            for f in blk["files"]:
                d = json.loads((STAMPS / f).read_text(encoding="utf-8"))
                v = d["variants"][0]
                xs = [o["dxHex"] for o in v["objects"]] or [0]
                ys = [o["dyHex"] for o in v["objects"]] or [0]
                w, h = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
                if x + w > x1:
                    x, y, shelf = x0, y + shelf + gap, 0
                if y + h > y1:
                    print(f"  stamp_fill {blk['rect']}: no room for {f}")
                    continue
                placed.append({"file": f, "at": [x - min(xs), y - min(ys)]})
                x += w + gap
                shelf = max(shelf, h)
            self.stamps({"elev": level["elev"], "stamps": placed})

    def scroll_ring(self, level: dict):
        """Scroll blockers (misc 0x0500000C) on a double hex-rect outline
        {"rect": [hx0, hy0, hx1, hy1]}: the camera can't centre past it."""
        de = level["elev"]
        sr = level.get("scroll_ring")
        if not sr:
            return
        pr = self.gi.proto(SCROLL_BLOCKER)
        x0, y0, x1, y1 = sr["rect"]
        hexes = set()
        for k in (0, 1):
            for x in range(x0 - k, x1 + k + 1):
                hexes.add((x, y0 - k))
                hexes.add((x, y1 + k))
            for y in range(y0 - k, y1 + k + 1):
                hexes.add((x0 - k, y))
                hexes.add((x1 + k, y))
        for hx, hy in sorted(hexes):
            if 0 <= hx < 200 and 0 <= hy < 200:
                self.add(SCROLL_BLOCKER, pr.fid, hy * HEX_W + hx, de)
        print(f"  scroll ring: {len(hexes)} blockers")

    def build(self) -> bytes:
        self._tunnel_sq = {}
        for level in self.r["levels"]:
            print(f"elevation {level['elev']}")
            self.ground(level)
            self.grown_ground(level)
            self.floor_fill(level)
            self.floor_tiles(level)
            for spec in level.get("lift", []):
                self.lift(level, spec)
            self.stamps(level)
            self.walls(level)
            self.lines(level)
            self.stamp_fill(level)
            self.openings(level)
            self.tunnels(level)
            self.placed(level)
            self.scatter(level)
            self.exits(level)
            self.scroll_ring(level)
        for src, (conv, _s) in self.converters.items():
            s = conv.stats
            print(f"  converter {src}: kept {s['kept']}, ledger {s['ledger']}, art match {s['artmatch']}, "
                  f"ported {s['ported']}, unresolved {s['unresolved']}")
        return self.mb.to_bytes()


def olympus_pids() -> set[int]:
    """Protos ported from Olympus 2207 (mod/assets.csv). Those are the owner's
    to use; a composed map may not contain any."""
    import csv
    out = set()
    with (ROOT / "mod" / "assets.csv").open(encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if r["source"] == "olympus" and r["kind"] == "proto":
                try:
                    out.add(int(r["dest_id"], 16))
                except ValueError:
                    pass
    return out


def write(name: str, data: bytes, force: bool):
    for d in (AUTHORED, DEPLOYED):
        p = d / name
        if p.exists() and not force:
            sys.exit(f"{p} exists; --force replaces it (the old copy goes to _safety_backups/composed)")
    for d in (AUTHORED, DEPLOYED):
        p = d / name
        if p.exists():
            bk = ROOT / "_safety_backups" / "composed"
            bk.mkdir(parents=True, exist_ok=True)
            shutil.copy2(p, bk / f"{name}.{time.strftime('%Y%m%d_%H%M%S')}")
        d.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
        print(f"wrote {p.relative_to(ROOT)}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("recipe")
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--preview", action="store_true", help="render build/compose/<name>_<elev>.png")
    args = ap.parse_args()
    recipe = json.loads(inside(args.recipe).read_text(encoding="utf-8"))
    c = Composer(recipe, dry_run=not args.write)
    data = c.build()
    olym = olympus_pids()
    n_olym = sum(1 for o in c.mb.objects if o.pid in olym)
    if n_olym:
        msg = f"{n_olym} objects are Olympus assets, which are the owner's only"
        if args.write:
            sys.exit(f"refusing to write {name}: {msg}")
        print(f"WARNING {msg}")
    name = recipe["name"].upper() + ".MAP"
    if args.write:
        write(name, data, args.force)
    try:
        m = MapFile.parse(data, c.gi.proto)
    except ValueError as e:
        print(f"{name}: not readable with the deployed protos yet ({e}). "
              f"Next: python tools/deploy.py, python tools/repack_art.py, python tools/deploy.py")
        return 0
    print(f"{name}: {len(m.objects)} objects, elevations {m.elevations}")
    if args.preview:
        import render
        from PIL import Image  # noqa: F401
        out = ROOT / "build" / "compose"
        out.mkdir(parents=True, exist_ok=True)
        from PIL import ImageDraw
        for ei, e in enumerate(m.elevations):
            img = render.render_map(c.gi, m, ei)
            pts = []
            for sq, tid in enumerate(m.floors[ei]):
                if tid > 1:
                    pts.append(render.square_xy(sq))
            for o in m.objects:
                if o.elevation == e and 0 <= o.tile < 40000:
                    x, y = render.hex_xy_screen(o.tile)
                    pts.append((x - 100, y - 200))
                    pts.append((x + 100, y + 60))
            if pts:
                minx = min(p[0] for p in pts)
                miny = min(p[1] for p in pts)
                dr = ImageDraw.Draw(img)
                for sy in range(0, 100, 4):
                    for sx in range(0, 100, 4):
                        if m.floors[ei][sy * 100 + sx] <= 1:
                            continue
                        x, y = render.hex_xy_screen(sy * 2 * 200 + sx * 2)
                        cx, cy = x + 16 - minx, y + 8 - miny
                        dr.rectangle([cx - 2, cy - 2, cx + 2, cy + 2], fill=(255, 255, 0, 255))
                        dr.text((cx + 4, cy - 6), f"{sx},{sy}", fill=(255, 255, 0, 255))
            img = img.convert("RGB")
            img = img.resize((img.width // 2, img.height // 2))
            p = out / f"{recipe['name'].lower()}_{e}.png"
            img.save(p)
            print(f"preview {p.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
