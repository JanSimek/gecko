#!/usr/bin/env python3
"""
map_survey.py - measure how the maps of Fallout 1, Fallout 2 and the total
conversions are laid out, so a generated FOC map can follow the same habits.

    python tools/map_survey.py                 # every source -> build/map_survey/
    python tools/map_survey.py --sources fo1,fo2

Writes build/map_survey/maps.csv (one row per map elevation) and
build/map_survey/summary.json (medians per source and per kind). The
findings are written up in docs/MAP_LAYOUT_GUIDE.md.

WHAT A ROW MEASURES (squares are 100x100; a hex is half a square)
  kind            town (outdoors, 3+ buildings), outdoor (outdoors, fewer),
                  interior (mostly roofed or building floor), cave
  floored         squares with a floor tile; bbox of them
  content bbox    bbox of every visible object (no blockers, exits, lights)
  margin          squares between the content bbox and the floored bbox,
                  per side (how much bare ground rings the content)
  buildings       roof components of 24+ squares: count, median size,
                  roofed share of the floor, median gap to the nearest
                  other building (bbox to bbox, squares)
  roads           share of floor squares with a road/curb/sidewalk tile,
                  and how many of the four floored-bbox sides a road touches
  exits           exit-grid hexes, on how many sides, and whether they lie
                  on the floored edge
  start           player start: distance to the content centre and to the
                  nearest exit, in squares
  scatter         outdoor objects (not under a roof) per open square, split
                  into plants, rocks, junk, lamps, fences
  empty           share of floored squares with no visible object within
                  4 squares
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import re
import statistics
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import port  # noqa: E402
from buildings import roof_components  # noqa: E402
from foclib import GameIndex, parse_lst  # noqa: E402
from foclib.formats import MapFile, is_exit_grid, pid_type  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "map_survey"
SOURCES = ["fo1", "fo2", "sonora", "nevada", "nevada_extended", "olympus", "resurrection", "sotj", "yesterday"]

ROAD_RE = re.compile(r"^(road|curb|crb|sdwk|swalk|side|street|strt|asph|hwy|tp1|pave)", re.I)
CAVE_RE = re.compile(r"^(cav|mine|rock|cve)", re.I)
PLANT_RE = re.compile(r"(weed|bush|tree|tre\d|grass|cact|shrub|plant|flower|brush|corn|crop|reed)", re.I)
ROCK_RE = re.compile(r"(rock|roks|boulder|stone|pebbl|rubbl)", re.I)
JUNK_RE = re.compile(r"(junk|trash|tire|debris|barrel|crate|box|car\d|wreck|scrap|drum|pipe|bone|skull|skel|can)", re.I)
LAMP_RE = re.compile(r"(lamp|light|strlit|lite|fire|torch|candle)", re.I)
FENCE_RE = re.compile(r"(fen\d|fence|rail|wire)", re.I)
# eggs: FO2's invisible see-through markers; black: fill that hides map edges
SKIP_RE = re.compile(r"^(block|blk|scrblk|wallblk|light\d|exitgrid|ext2grd|lightsrc|eggs?\d|black\d)", re.I)


def list_maps(gi: GameIndex) -> list[str]:
    names = set()
    for prov in gi.vfs.providers:
        if prov.kind == "dat":
            for n in prov.dat.names():
                low = n.lower()
                if low.startswith("maps\\") and low.endswith(".map"):
                    names.add(low)
        else:
            d = prov.root / "maps"
            if d.is_dir():
                for f in os.listdir(d):
                    if f.lower().endswith(".map"):
                        names.add("maps\\" + f.lower())
    return sorted(names)


def med(v):
    v = [x for x in v if x is not None]
    return round(statistics.median(v), 2) if v else None


def survey_elev(m: MapFile, ei: int, art, tiles) -> dict | None:
    floors, roofs = m.floors[ei], m.roofs[ei]
    elev = m.elevations[ei]
    floored = [i for i, v in enumerate(floors) if (v & 0xFFF) > 1]
    if len(floored) < 100:
        return None
    fx = [i % 100 for i in floored]
    fy = [i // 100 for i in floored]
    fb = (min(fx), min(fy), max(fx), max(fy))

    names = [tiles[v & 0xFFF].lower() if (v & 0xFFF) < len(tiles) else "" for v in floors]
    road = sum(1 for i in floored if ROAD_RE.match(names[i]))
    cave = sum(1 for i in floored if CAVE_RE.match(names[i]))
    roofed = sum(1 for i in floored if (roofs[i] & 0xFFF) > 1)

    comps = [c for c in roof_components(m, ei) if len(c) >= 24]
    boxes = []
    for c in comps:
        xs = [s % 100 for s in c]; ys = [s // 100 for s in c]
        boxes.append((min(xs), min(ys), max(xs), max(ys)))
    gaps = []
    for i, a in enumerate(boxes):
        best = None
        for j, b in enumerate(boxes):
            if i == j:
                continue
            dx = max(0, b[0] - a[2], a[0] - b[2])
            dy = max(0, b[1] - a[3], a[1] - b[3])
            d = math.hypot(dx, dy)
            best = d if best is None else min(best, d)
        if best is not None:
            gaps.append(best)

    under_roof = {s for c in comps for s in c}
    objs = [o for o in m.objects if o.elevation == elev and 0 <= o.tile < 40000]
    vis, exits = [], []
    scatter = Counter()
    crit_in = crit_out = 0
    for o in objs:
        t = pid_type(o.pid)
        sq = (o.tile // 200 // 2) * 100 + (o.tile % 200) // 2
        if t == 5 and is_exit_grid(o.pid):
            exits.append(sq)
            continue
        lst = art.get((o.fid >> 24) & 0xF, [])
        a = lst[o.fid & 0xFFF].lower() if (o.fid & 0xFFF) < len(lst) else ""
        if SKIP_RE.match(a) or t == 5:
            continue
        if t == 1:
            if sq in under_roof:
                crit_in += 1
            else:
                crit_out += 1
            continue
        vis.append(sq)
        if sq not in under_roof and t in (0, 2):
            for k, rx in (("plants", PLANT_RE), ("rocks", ROCK_RE), ("junk", JUNK_RE),
                          ("lamps", LAMP_RE), ("fences", FENCE_RE)):
                if rx.search(a):
                    scatter[k] += 1
                    break
            else:
                scatter["other"] += 1
    if not vis:
        return None
    vx = [s % 100 for s in vis]; vy = [s // 100 for s in vis]
    cb = (min(vx), min(vy), max(vx), max(vy))
    margins = (cb[0] - fb[0], cb[1] - fb[1], fb[2] - cb[2], fb[3] - cb[3])

    # roads touching the floored bbox sides
    road_sides = 0
    for side in range(4):
        hit = False
        for i in floored:
            if not ROAD_RE.match(names[i]):
                continue
            x, y = i % 100, i // 100
            if (side == 0 and x <= fb[0] + 2) or (side == 1 and y <= fb[1] + 2) or \
               (side == 2 and x >= fb[2] - 2) or (side == 3 and y >= fb[3] - 2):
                hit = True
                break
        road_sides += hit

    exit_sides = set()
    exits_on_edge = 0
    for s in exits:
        x, y = s % 100, s // 100
        d = [x - fb[0], y - fb[1], fb[2] - x, fb[3] - y]
        k = d.index(min(d))
        exit_sides.add(k)
        exits_on_edge += min(d) <= 3

    # empty: floored squares with no visible object within 4 squares
    occ = set(vis)
    near = set()
    for s in occ:
        x, y = s % 100, s // 100
        for dx in range(-4, 5):
            for dy in range(-4, 5):
                near.add((y + dy) * 100 + x + dx)
    empty = sum(1 for i in floored if i not in near)

    # the play area: what the exit grids ring (they sit at the scroll limit)
    if len(exits) >= 8 and len(exit_sides) >= 2:
        ex = [s % 100 for s in exits]; ey = [s // 100 for s in exits]
        play = (min(ex), min(ey), max(ex), max(ey))
        play_w, play_h = play[2] - play[0] + 1, play[3] - play[1] + 1
        exit_out = med([max(cb[0] - s % 100, s % 100 - cb[2], cb[1] - s // 100, s // 100 - cb[3], 0) for s in exits])
    else:
        play_w = play_h = exit_out = None

    # clutter hugging buildings: visible outdoor objects within 3 squares of a
    # building's bbox, per square of that ring, against open ground elsewhere
    ring = set()
    for b0 in boxes:
        for x in range(b0[0] - 3, b0[2] + 4):
            for y in range(b0[1] - 3, b0[3] + 4):
                k = y * 100 + x
                if 0 <= k < 10000 and k not in under_roof:
                    ring.add(k)
    out_vis = [s for s in vis if s not in under_roof]
    in_ring = sum(1 for s in out_vis if s in ring)
    ring_density = round(in_ring / len(ring), 3) if ring else None
    far = len(floored) - roofed - len(ring & set(floored))
    far_density = round((len(out_vis) - in_ring) / max(1, far), 3)
    ground_kinds = len({floors[i] & 0xFFF for i in floored if not ROAD_RE.match(names[i])})

    open_sq = len(floored) - roofed
    st = m.default_tile
    sx, sy = (st % 200) // 2, (st // 200) // 2
    ccx, ccy = (cb[0] + cb[2]) / 2, (cb[1] + cb[3]) / 2
    start_to_exit = min((math.hypot(sx - s % 100, sy - s // 100) for s in exits), default=None) if ei == 0 else None

    n_b = len(comps)
    if cave > 0.4 * len(floored):
        kind = "cave"
    elif roofed > 0.5 * len(floored):
        kind = "interior"
    elif n_b >= 3:
        kind = "town"
    else:
        kind = "outdoor"

    return {
        "elev": elev, "kind": kind,
        "floored": len(floored), "floor_w": fb[2] - fb[0] + 1, "floor_h": fb[3] - fb[1] + 1,
        "content_w": cb[2] - cb[0] + 1, "content_h": cb[3] - cb[1] + 1,
        "margin_min": min(margins), "margin_mean": round(sum(margins) / 4, 1),
        "content_share": round((cb[2] - cb[0] + 1) * (cb[3] - cb[1] + 1) / len(floored), 3),
        "buildings": n_b, "bld_median_sq": med([len(c) for c in comps]),
        "roofed_share": round(roofed / len(floored), 3),
        "bld_gap_median": med(gaps), "bld_gap_min": min(gaps) if gaps else None,
        "road_share": round(road / len(floored), 3), "road_sides": road_sides,
        "exits": len(exits), "exit_sides": len(exit_sides),
        "exits_on_edge": round(exits_on_edge / len(exits), 2) if exits else None,
        "start_to_centre": round(math.hypot(sx - ccx, sy - ccy), 1) if ei == 0 else None,
        "start_to_exit": round(start_to_exit, 1) if start_to_exit is not None else None,
        "objects": len(vis), "obj_per_sq": round(len(vis) / len(floored), 3),
        "scatter_per_open_sq": round(sum(scatter.values()) / max(1, open_sq), 3),
        **{f"sc_{k}": round(scatter[k] / max(1, open_sq), 3) for k in ("plants", "rocks", "junk", "lamps", "fences", "other")},
        "critters_in": crit_in, "critters_out": crit_out,
        "empty_share": round(empty / len(floored), 3),
        "play_w": play_w, "play_h": play_h, "exit_out_of_content": exit_out,
        "near_building_density": ring_density, "open_ground_density": far_density,
        "ground_tile_kinds": ground_kinds,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--sources", default=",".join(SOURCES))
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    rows = []
    for src in args.sources.split(","):
        try:
            gi = port.open_source(src)
        except SystemExit as e:
            print(f"skip {src}: {e}")
            continue
        art = {}
        for t, d in ((0, "items"), (1, "critters"), (2, "scenery"), (3, "walls"), (5, "misc")):
            try:
                art[t] = parse_lst(gi.vfs.read(f"art\\{d}\\{d}.lst"))
            except FileNotFoundError:
                art[t] = []
        tiles = parse_lst(gi.vfs.read("art\\tiles\\tiles.lst"))
        maps = list_maps(gi)
        ok = 0
        for mp in maps:
            try:
                m = MapFile.parse(gi.vfs.read(mp), gi.proto)
            except Exception:
                continue
            for ei in range(len(m.elevations)):
                try:
                    r = survey_elev(m, ei, art, tiles)
                except Exception:
                    r = None
                if r:
                    rows.append({"source": src, "map": mp.split("\\")[-1], **r})
                    ok += 1
        print(f"{src}: {len(maps)} maps, {ok} elevations surveyed")

    with (OUT / "maps.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    keys = [k for k in rows[0] if k not in ("source", "map", "elev", "kind")]
    summary = defaultdict(dict)
    for kind in ("town", "outdoor", "interior", "cave"):
        sel = [r for r in rows if r["kind"] == kind]
        summary["ALL"][kind] = {"n": len(sel), **{k: med([r[k] for r in sel]) for k in keys}}
        for src in {r["source"] for r in rows}:
            s2 = [r for r in sel if r["source"] == src]
            if s2:
                summary[src][kind] = {"n": len(s2), **{k: med([r[k] for r in s2]) for k in keys}}
    (OUT / "summary.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")
    print(json.dumps(summary["ALL"], indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
