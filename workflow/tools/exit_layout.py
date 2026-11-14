#!/usr/bin/env python3
"""
exit_layout.py - lay out each FOC map's exits and edge the way Fallout 2 does:
Fallout 2's own exit prefabs (tools/exit_templates.json) at the edge box where each
road leaves, a row of invisible block hexes along the rest of the box edge, and the
arrival hexes a few steps in from the matching exit, facing into the map.

    python tools/exit_layout.py --preview build/layout     # renders + checks, nothing written
    python tools/exit_layout.py --write

Owner, 2026-10-18: "fix the exit grids in each location, examine how they look in the
originals and TCs and copy that exact same layout"; "make sure the player spawn
location is correct for when they enter and leave each map".

Per map (elevation 0; its edge box from maps/<name>.edg, tools/map_edges.py):
  * each old exit patch (touching exit hexes, one destination) keeps its destination
    and the road it sat on: the side of the box it is nearest to picks the prefab
    (bottom, top, left, right), and the prefab is centred where the road crosses that
    side (the road's own squares near the side, else the old patch), kept inside the
    box's corners; the prefab sits as far from the box edge as it does on its own
    Fallout 2 map
  * Block Hex Auto Inviso (scenery 0x02000158, the piece Fallout 2 and the TCs line
    their box edges with) on every floored hex of a one-hex band just inside the box
    edge, except where a prefab's own exit grids are: the row is unbroken, as in
    Fallout 2, and the player gets out by crossing a prefab's inner outline
  * arrival: for every exit on another map that leads here, its destination becomes a
    hex ARRIVE_IN px in from this map's exit back the other way (on open floor, two
    hexes clear of exits and blocks), facing into the map; a map entered from the world
    map (Clairemont, Lamesa) gets its start hex the same way from its world-map exit
Checked: nothing outside the box can be walked to from any arrival except through an
exit; every exit can be reached; every arrival is on open floor inside the box.
Both copies of each map are written (old ones to _safety_backups/exit_layout/);
refuses while gecko or the game is running.
"""

from __future__ import annotations

import argparse
import json
import shutil
import struct
import subprocess
import sys
import time
from collections import deque, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib.paths import bare_name, inside  # noqa: E402
from foclib import GameIndex, parse_lst  # noqa: E402
from foclib.formats import MapFile  # noqa: E402
from mapbuild import MapBuilder, MapObjectSpec  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
from foclib import config as _config  # noqa: E402
AUTH = ROOT / "mod" / "data" / "maps"
DEPL = ROOT / "base" / "mods" / "foc" / "maps"
BACKUP = ROOT / "_safety_backups" / "exit_layout"
TEMPLATES = json.loads((Path(__file__).resolve().parent / "exit_templates.json").read_text(encoding="utf-8"))
W = 200
DIRS = {0: (-1, W - 1, W, W + 1, 1, -W), 1: (-W - 1, -1, W, 1, 1 - W, -W)}
BLOCK_PID = 0x02000158
EXIT_FIRST, EXIT_LAST = 0x05000010, 0x05000017
ARRIVE_IN = 220                 # px in from the box edge for an arrival
FACING = {"bottom": 0, "top": 2, "left": 1, "right": 4}
WORLD_START_MAPS = {"CLAIREMONT.MAP", "LAMESA.MAP"}   # entered from the world map at their start hex
ROAD = ("road", "curb", "crbm", "line", "stop")


def screen(t):
    x, y = t % W, t // W
    v3 = W - 1 - x
    return 48 * (v3 // 2) + 32 * (v3 & 1) + 16 * y, -12 * (v3 // 2) + 12 * y


def neighbours(t):
    for d in DIRS[(t % W) & 1]:
        n = t + d
        if 0 <= n < W * W and abs(n % W - t % W) <= 1:
            yield n


def map_ids():
    out = {}
    for line in (ROOT / "mod" / "ids" / "maps.txt").read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if parts and parts[0].isdigit():
            out[int(parts[0])] = parts[1].upper()
    return out


def edge_box(name):
    for p in AUTH.iterdir():
        if p.suffix.lower() == ".edg" and p.stem.lower() == Path(name).stem.lower():
            v = struct.unpack(">%di" % (p.stat().st_size // 4), p.read_bytes())
            L, T, R, B = v[3:7]
            x0, x1 = sorted((screen(L)[0], screen(R)[0]))
            y0, y1 = sorted((screen(T)[1], screen(B)[1]))
            return x0, y0, x1, y1
    sys.exit(f"{name}: no edge file; run tools/map_edges.py first")


class Map:
    def __init__(self, name, gi, tiles):
        self.name = name
        self.raw = (AUTH / name).read_bytes()
        self.m = MapFile.parse(self.raw, gi.proto)
        self.box = edge_box(name)
        self.ends = {o.off: MapFile._read_object(self.raw, o.off, gi.proto)[1] for o in self.m.objects}
        fl = self.m.floors[self.m.elevations.index(0)]
        self.fl = fl
        self.floor = lambda t: (fl[(t // W // 2) * 100 + (t % W) // 2] & 0xFFF) > 1
        self.road = lambda t: (lambda v: v < len(tiles) and tiles[v].lower().startswith(ROAD))(
            fl[(t // W // 2) * 100 + (t % W) // 2] & 0xFFF)
        self.exits = {}
        self.blocked = set()
        self.old_blocks = set()
        for o in self.m.objects:
            if o.elevation != 0 or not (0 <= o.tile < W * W):
                continue
            t = o.pid >> 24
            if EXIT_FIRST <= o.pid <= EXIT_LAST:
                self.exits[o.tile] = tuple(struct.unpack_from(">4i", self.raw, self.ends[o.off] - 16))
            elif o.pid == BLOCK_PID:
                self.old_blocks.add(o.tile)
            elif t in (2, 3) and not struct.unpack_from(">I", self.raw, o.off + 0x24)[0] & 0x10:
                pr = gi.proto(o.pid) if t == 2 else None
                if pr is not None and pr.subtype == 0:      # a door
                    continue
                self.blocked.add(o.tile)

    def inside(self, t, pad=0):
        x, y = screen(t)
        x0, y0, x1, y1 = self.box
        return x0 - pad <= x <= x1 + pad and y0 - pad <= y <= y1 + pad

    def patches(self):
        seen, out = set(), []
        for t in sorted(self.exits):
            if t in seen:
                continue
            dest = self.exits[t]
            comp, stack = [], [t]
            seen.add(t)
            while stack:
                a = stack.pop()
                comp.append(a)
                ax, ay = a % W, a // W
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        b = (ay + dy) * W + ax + dx
                        if b in self.exits and b not in seen and self.exits[b][:2] == dest[:2]:
                            seen.add(b)
                            stack.append(b)
            out.append((dest, comp))
        return out


def place_prefab(mp, comp):
    """Side of the box and the prefab's hexes (tile -> variant pid) for an old patch."""
    x0, y0, x1, y1 = mp.box
    pts = [screen(t) for t in comp]
    cx = sum(p[0] for p in pts) / len(pts)
    cy = sum(p[1] for p in pts) / len(pts)
    # nearest side, measured from the patch's centre clamped into the box
    qx, qy = min(max(cx, x0), x1), min(max(cy, y0), y1)
    side = min((("left", abs(qx - x0) + max(0, x0 - cx)), ("right", abs(x1 - qx) + max(0, cx - x1)),
                ("top", abs(qy - y0) + max(0, y0 - cy)), ("bottom", abs(y1 - qy) + max(0, cy - y1))),
               key=lambda kv: kv[1])[0]
    tpl = TEMPLATES[side]
    # where the road crosses that side, near the old patch
    horiz = side in ("top", "bottom")
    near = []
    for t in range(W * W):
        if not mp.floor(t) or not mp.road(t) or not mp.inside(t):
            continue
        x, y = screen(t)
        d_edge = {"top": y - y0, "bottom": y1 - y, "left": x - x0, "right": x1 - x}[side]
        along = x if horiz else y
        if 0 <= d_edge <= 160 and abs(along - (cx if horiz else cy)) <= 480:
            near.append(along)
    cross = sum(near) / len(near) if near else (cx if horiz else cy)
    # keep the prefab's span inside the box's corners
    half = tpl["span_px"][0 if horiz else 1] / 2
    lo, hi = (x0, x1) if horiz else (y0, y1)
    cross = min(max(cross, lo + half + 32), hi - half - 32)
    edge = {"top": y0, "bottom": y1, "left": x0, "right": x1}[side]
    if horiz:
        AX, AY = cross + tpl["anchor_from_centre_px"], edge + tpl["anchor_to_edge_px"]
    else:
        AX, AY = edge + tpl["anchor_to_edge_px"], cross + tpl["anchor_from_centre_px"]
    anchor = min((t for t in range(W * W) if (t % W) % 2 == 0),
                 key=lambda t: abs(screen(t)[1] - AY) * 4 + abs(screen(t)[0] - AX))
    ax, ay = anchor % W, anchor // W
    hexes = {}
    for dx, dy, pid in tpl["objects"]:
        x, y = ax + dx, ay + dy
        if 0 <= x < W and 0 <= y < W:
            hexes[y * W + x] = 0x05000000 | pid
    return side, hexes


def block_band(mp, openings):
    """Floored hexes in a one-hex band just inside the box edge, minus the openings
    (per side, the screen span of each prefab, one hex wider)."""
    x0, y0, x1, y1 = mp.box
    out = set()
    for t in range(W * W):
        if not mp.floor(t):
            continue
        x, y = screen(t)
        if not (x0 <= x <= x1 and y0 <= y <= y1):
            continue
        sides = []
        if y - y0 < 12:
            sides.append(("top", x))
        if y1 - y < 12:
            sides.append(("bottom", x))
        if x - x0 < 32:
            sides.append(("left", y))
        if x1 - x < 32:
            sides.append(("right", y))
        for side, along in sides:
            if any(s == side and lo - 32 <= along <= hi + 32 for s, lo, hi in openings):
                break
        else:
            if sides:
                out.add(t)
    return out


def arrival_near(mp, side, hexes, stops):
    """An open hex ARRIVE_IN px in from the box edge, level with the prefab's middle."""
    x0, y0, x1, y1 = mp.box
    xs = [screen(t)[0] for t in hexes]
    ys = [screen(t)[1] for t in hexes]
    mx, my = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    depth = max(ARRIVE_IN, (max(ys) - min(ys) if side in ("top", "bottom") else max(xs) - min(xs)) + 96)
    tx, ty = {"top": (mx, y0 + depth), "bottom": (mx, y1 - depth),
              "left": (x0 + depth, my), "right": (x1 - depth, my)}[side]
    clear = set(stops)
    for _ in range(2):
        clear |= {n for s in clear for n in neighbours(s)}
    best = None
    for t in range(W * W):
        if not mp.floor(t) or t in mp.blocked or t in clear or not mp.inside(t, -48):
            continue
        x, y = screen(t)
        d = abs(x - tx) + abs(y - ty) * 1.5
        if best is None or d < best[0]:
            best = (d, t)
    return best[1] if best else None


def flood(mp, seeds, stops):
    seen = {s for s in seeds if mp.floor(s) and s not in mp.blocked}
    q = deque(seen)
    while q:
        t = q.popleft()
        if t in stops:
            continue
        for n in neighbours(t):
            if n not in seen and mp.floor(n) and n not in mp.blocked:
                seen.add(n)
                q.append(n)
    return seen


def write_map(mp, gi, new_exits, blocks):
    """Old exits and old edge blocks out; the new ones in, on elevation 0."""
    builder = MapBuilder(mp.name, elevations=tuple(mp.m.elevations), map_id=0)
    groups = {0: [], 1: [], 2: []}
    for o in mp.m.objects:
        if o.elevation == 0 and (EXIT_FIRST <= o.pid <= EXIT_LAST or o.pid == BLOCK_PID):
            continue
        groups[o.elevation].append(mp.raw[o.off:mp.ends[o.off]])
    bp = gi.proto(BLOCK_PID)
    sflags = lambda f: f - (1 << 32) if f >= 1 << 31 else f  # noqa: E731
    for t in sorted(blocks):
        buf = bytearray()
        builder._write_object(buf, MapObjectSpec(pid=BLOCK_PID, fid=bp.fid, tile=t, elevation=0,
                                                 flags=sflags(bp.flags), extras=()))
        groups[0].append(bytes(buf))
    for t, (pid, dest) in sorted(new_exits.items()):
        pr = gi.proto(pid)
        art = (pid & 0xFF) + 1 + (16 if dest[0] < 0 else 0)
        buf = bytearray()
        builder._write_object(buf, MapObjectSpec(pid=pid, fid=0x05000000 | art, tile=t, elevation=0,
                                                 flags=sflags(pr.flags), extras=tuple(dest)))
        groups[0].append(bytes(buf))
    raw = mp.raw
    first = mp.m.objects[0].off
    out = bytearray(raw[:first - 8])
    out += struct.pack(">i", sum(len(g) for g in groups.values()))
    for e in (0, 1, 2):
        out += struct.pack(">i", len(groups[e]))
        for d in groups[e]:
            out += d
    out += raw[max(mp.ends.values()):]
    return out


def draw_debug(path, out, box, exits, blocks, outside, arrivals):
    """gecko render of the map with the box (magenta), block hexes (yellow), exits
    (green, red to the world map), arrivals (white) and any ground outside the box the
    player could reach (cyan) drawn on."""
    import re
    from PIL import Image, ImageDraw
    D = []
    order = (ROOT / "base" / "mods" / "mods_order.txt").read_text().split()
    for dd in ["base/master.dat", "base/critter.dat"] + [f"base/mods/{n}" for n in order] + ["base/mods/foc"]:
        if (ROOT / dd).exists():
            D += ["--data", str(ROOT / dd)]
    r = subprocess.run([str(_config.gecko_cli()), "map", "render", "--map", str(path),  # NOSONAR - paths checked by foclib.paths.inside() and bare_name()
                        "--elevation", "0", "--out", str(out), "--max-dim", "2400", "--full"] + D, capture_output=True, text=True)
    m = re.search(r"origin=(-?\d+),(-?\d+) scale=([\d.]+)", r.stdout + r.stderr)
    ox, oy, sc = int(m.group(1)), int(m.group(2)), float(m.group(3))
    im = Image.open(out).convert("RGB")
    dr = ImageDraw.Draw(im)
    P = lambda t: ((screen(t)[0] + 32 + 16 - ox) * sc, (screen(t)[1] + 1212 + 8 - oy) * sc)  # noqa: E731
    x0, y0, x1, y1 = box
    dr.rectangle([(x0 + 32 - ox) * sc, (y0 + 1212 - oy) * sc, (x1 + 32 - ox) * sc, (y1 + 1212 - oy) * sc], outline=(255, 0, 255), width=2)
    for t in blocks:
        x, y = P(t); dr.rectangle([x - 1, y - 1, x + 1, y + 1], fill=(255, 220, 0))
    for t, (pid, dest) in exits.items():
        x, y = P(t); dr.rectangle([x - 2, y - 2, x + 2, y + 2], fill=(255, 60, 60) if dest[0] < 0 else (60, 255, 60))
    for t in outside:
        x, y = P(t); dr.point((x, y), fill=(0, 255, 255))
    for t in arrivals:
        x, y = P(t); dr.ellipse([x - 5, y - 5, x + 5, y + 5], outline=(255, 255, 255), width=2)
    bbox = im.convert("L").point(lambda v: 255 if v > 12 else 0).getbbox()
    if bbox:
        im = im.crop((max(0, bbox[0] - 20), max(0, bbox[1] - 20), min(im.width, bbox[2] + 20), min(im.height, bbox[3] + 20)))
    im.save(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("maps", nargs="*")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--preview", metavar="DIR")
    g.add_argument("--write", action="store_true")
    args = ap.parse_args()
    if args.write:
        running = subprocess.run(["tasklist"], capture_output=True, text=True).stdout.lower()
        if "gecko" in running or "fallout2" in running:
            sys.exit("close gecko and the game first")
    gi = GameIndex(pristine=False)
    tiles = parse_lst(gi.vfs.read(r"art\tiles\tiles.lst"))
    ids = map_ids()
    by_name = {v: k for k, v in ids.items()}
    names = [bare_name(n) for n in args.maps] or sorted(p.name for p in AUTH.iterdir()
                                if p.suffix.lower() == ".map" and (DEPL / p.name).exists())
    for n in names:
        if (DEPL / n).read_bytes() != (AUTH / n).read_bytes():
            sys.exit(f"{n}: the deployed copy differs from mod/data/maps; run tools/sync_maps.py first")
    maps = {n.upper(): Map(n, gi, tiles) for n in names}
    # 1. prefabs and blocks per map
    plan = {}
    for key, mp in maps.items():
        zones = []
        # pieces of one old exit (same destination, same side, close together) make one prefab
        groups = []
        for dest, comp in mp.patches():
            side, _ = place_prefab(mp, comp)
            pts = [screen(t) for t in comp]
            c = (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts))
            for gr in groups:
                if gr["dest"][:2] == list(dest)[:2] and gr["side"] == side and                         abs(gr["c"][0] - c[0]) + abs(gr["c"][1] - c[1]) < 700:
                    gr["comp"] += comp
                    break
            else:
                groups.append({"dest": list(dest), "side": side, "comp": list(comp), "c": c})
        for gr in groups:
            side, hexes = place_prefab(mp, gr["comp"])
            zones.append({"dest": gr["dest"], "side": side, "hexes": hexes})
        openings = []
        for z in zones:
            pts = [screen(t) for t in z["hexes"]]
            if z["side"] in ("top", "bottom"):
                openings.append((z["side"], min(p[0] for p in pts), max(p[0] for p in pts)))
            else:
                openings.append((z["side"], min(p[1] for p in pts), max(p[1] for p in pts)))
        all_exit = {t for z in zones for t in z["hexes"]}
        # as in Fallout 2 the row of blocks runs unbroken along the edge; the prefab's own
        # hexes on it are the way out, reached by crossing the prefab's inner outline
        blocks = block_band(mp, []) - all_exit
        plan[key] = {"zones": zones, "blocks": blocks}
    # 2. arrivals: a zone on map B leading to map A gives the arrival on B for A's exits to B
    arrive = {}                     # (map B, map A id) -> (tile, facing)
    for key, mp in maps.items():
        stops = {t for z in plan[key]["zones"] for t in z["hexes"]} | plan[key]["blocks"]
        for z in plan[key]["zones"]:
            t = arrival_near(mp, z["side"], z["hexes"], stops)
            if t is not None:
                arrive.setdefault((key, z["dest"][0]), (t, FACING[z["side"]]))
    report = defaultdict(list)
    new_start = {}
    for key, mp in maps.items():
        for z in plan[key]["zones"]:
            dm = z["dest"][0]
            if dm >= 0 and dm in ids and ids[dm] in maps:
                back = arrive.get((ids[dm], by_name.get(key, -999)))
                if back:
                    z["dest"][1], z["dest"][2], z["dest"][3] = back[0], 0, back[1]
                else:
                    report[key].append(f"   no exit on {ids[dm]} leads back here: arrival there kept at {z['dest'][1]}")
            elif dm < 0 and key in WORLD_START_MAPS:
                a = arrive.get((key, dm))
                if a:
                    new_start[key] = a
    # 3. checks and output
    results = {}
    debug = {}
    bad = False
    for key, mp in maps.items():
        zones, blocks = plan[key]["zones"], plan[key]["blocks"]
        new_exits = {t: (pid, tuple(z["dest"])) for z in zones for t, pid in z["hexes"].items()}
        stops = set(new_exits) | blocks
        seeds = {mp.m.default_tile} | {a[0] for (b, _), a in arrive.items() if b == key}
        if key in new_start:
            seeds.add(new_start[key][0])
        mp_blocked = mp.blocked
        mp.blocked = mp_blocked | blocks
        reach = flood(mp, seeds, set(new_exits))
        mp.blocked = mp_blocked
        outside = [t for t in reach if not mp.inside(t, 24) and t not in new_exits]
        lines = []
        for z in zones:
            ok = any(t in reach for t in z["hexes"])
            lines.append(f"   {z['side']:6s} prefab, {len(z['hexes'])} exit grids -> map {z['dest'][0]} "
                         f"tile {z['dest'][1]} ({z['dest'][1] % W},{z['dest'][1] // W}) facing {z['dest'][3]}"
                         + ("" if ok else "   PROBLEM: can't be reached"))
            bad |= not ok
        lines.append(f"   {len(blocks)} block hexes along the box edge (was {len(mp.old_blocks)} on the map)")
        if key in new_start:
            t, f = new_start[key]
            lines.append(f"   start hex (world map) -> ({t % W},{t // W}) facing {f}")
        if outside:
            lines.append(f"   PROBLEM: {len(outside)} hexes outside the box can be walked to, e.g. "
                         + ", ".join(f"({t % W},{t // W})" for t in outside[:5]))
            bad = True
        results[key] = (mp, new_exits, blocks)
        debug[key] = {"outside": outside, "arrivals": sorted(seeds)}
        print(mp.name)
        for line in lines + report[key]:
            print(line)
    if args.preview:
        d = inside(args.preview)
        d.mkdir(parents=True, exist_ok=True)  # NOSONAR - path checked by foclib.paths.inside()
        for key, (mp, new_exits, blocks) in results.items():
            out = write_map(mp, gi, new_exits, blocks)
            if key in new_start:
                struct.pack_into(">i", out, 0x14, new_start[key][0])
                struct.pack_into(">i", out, 0x1C, new_start[key][1])
            MapFile.parse(bytes(out), gi.proto)
            (d / f"new_{mp.name}").write_bytes(bytes(out))  # NOSONAR - path checked by foclib.paths.inside()
            draw_debug(d / f"new_{mp.name}", d / f"{Path(mp.name).stem}.png", mp.box, new_exits, blocks,
                       debug[key]["outside"], debug[key]["arrivals"])
        print(f"--preview: test copies and renders in {d}; nothing written")
        return 1 if bad else 0
    if bad:
        sys.exit("not writing: fix the problems above first")
    BACKUP.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    for key, (mp, new_exits, blocks) in results.items():
        out = write_map(mp, gi, new_exits, blocks)
        if key in new_start:
            struct.pack_into(">i", out, 0x14, new_start[key][0])
            struct.pack_into(">i", out, 0x1C, new_start[key][1])
        MapFile.parse(bytes(out), gi.proto)
        shutil.copy2(AUTH / mp.name, BACKUP / f"{mp.name}.{stamp}")
        (AUTH / mp.name).write_bytes(bytes(out))
        (DEPL / mp.name).write_bytes(bytes(out))
        print(f"wrote {mp.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
