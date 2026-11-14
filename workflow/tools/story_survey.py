#!/usr/bin/env python3
"""
story_survey.py - how the original games and the total conversions tell
stories with what they put on a map, measured over every map in every source.

    python tools/story_survey.py                     # all sources
    python tools/story_survey.py --sources fo2,sonora
        -> build/story_survey/objects.csv     every classified object
           build/story_survey/vignettes.csv   every cluster of story objects
           build/story_survey/summary.json    the patterns across all maps

Every scenery, item and critter is named by its own game's message files
(pro_scen.msg, pro_item.msg, pro_crit.msg) and its art name, and put in a
story class:

  remains     skeletons, bones, skulls, corpses, blood
  wreck       cars, trucks, buses, aircraft, vehicle parts
  defence     sandbags, barricades, barbed wire, spikes, gun posts
  fire        campfires, burning barrels, torches, braziers
  camp        bedrolls, mattresses, tents, cots, cooking pots
  message     signs, graffiti, posters, notes, billboards, warnings
  decay       rubble, debris, holes, craters, cracked and broken things
  trash       trash, junk, cans, paper, tires
  store       crates, boxes, barrels, lockers, footlockers, chests
  growth      weeds, bushes, trees, dead trees, cactus, crops
  machine     pipes, machinery, generators, consoles, computers, tanks
  furniture   tables, chairs, beds, shelves, desks, counters
  grave       graves, crosses, tombstones
  person      critters (people and creatures placed on the map)

A vignette is a group of story objects within 3 squares of each other that
draws on at least two classes: the small scenes a designer composes (a body
by a wreck, a bedroll by a fire). The summary lists which combinations every
game uses most, where they sit (on a road, beside a building, in the open,
at the map edge) and how far each class keeps from buildings and roads.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import port  # noqa: E402
from foclib import MapFile, parse_lst  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "story_survey"
SOURCES = ["fo1", "fo2", "sonora", "nevada", "resurrection", "sotj", "yesterday", "olympus"]

CLASSES = [
    ("grave", r"grave|tomb|headstone|cross\d|crosses|cemeter|burial"),
    ("remains", r"skelet|bones?\b|bone\d|skull|corpse|dead (man|body|woman|guy)|body\b|bodies|blood|gore|carcass|remains|cadaver"),
    ("wreck", r"\bcar\b|car\d|wreck|truck|\bbus\b|vertibird|plane|aircraft|bomber|chassis|jeep|tractor|motorcycle|bike|hulk|tank tread|highwayman|cart"),
    ("defence", r"sandbag|barricade|barbed|razor|spike|tank trap|bunker|gun ?post|turret|watchtower|guard ?tower|pillbox"),
    ("fire", r"fire|burn|torch|brazier|flame|camp ?fire|cooking fire|embers?"),
    ("camp", r"bedroll|sleeping|mattress|\btent|cot\b|hay ?bed|bed ?roll|pot\b|kettle|cookpot|lean-?to|blanket"),
    ("message", r"sign|graffiti|poster|note\b|billboard|warning|banner|plaque|message|writing|painted|placard"),
    ("decay", r"rubble|debris|crater|hole\b|crack|broken|collapsed|ruin|shattered|fallen|destroyed|burnt|burned|rusted|rusty"),
    ("trash", r"trash|junk|garbage|\bcans?\b|paper|tire|litter|refuse|scrap"),
    ("store", r"crate|\bbox|boxes|barrel|locker|chest|footlocker|container|sack|bag\b|drum"),
    ("growth", r"weed|bush|tree|cact|shrub|plant|flower|grass|crop|corn|vine|stump|mushroom|brush"),
    ("machine", r"pipe|machin|generator|console|computer|terminal|tank\b|pump|reactor|turbine|valve|engine|panel|transformer|antenna|radio"),
    ("furniture", r"table|chair|\bbed\b|shelf|shelves|desk|counter|bench|stool|cabinet|dresser|couch|sofa|bookcase|stove|toilet|sink"),
]
CLASS_RE = [(k, re.compile(rx, re.I)) for k, rx in CLASSES]
SKIP = re.compile(r"^(block|blk|scrblk|wallblk|light\d|exitgrid|ext2grd|lightsrc|eggs?\d|black\d|scroll)", re.I)
ROAD_RE = re.compile(r"^(road|curb|crb|sdwk|swalk|side|street|strt|asph|hwy|tp1|pave)", re.I)
STORY = {"remains", "wreck", "defence", "fire", "camp", "message", "decay", "grave", "store", "trash"}


def msg_table(src, name):
    try:
        t = src.vfs.read(f"text\\english\\game\\{name}").decode("cp1252", "replace")
    except Exception:
        return {}
    return {int(m.group(1)): m.group(2) for m in re.finditer(r"\{(\d+)\}\{[^}]*\}\{([^}]*)\}", t)}


def list_maps(src):
    names = set()
    for prov in src.vfs.providers:
        if prov.kind == "dat":
            for n in prov.dat.names():
                if n.lower().startswith("maps\\") and n.lower().endswith(".map"):
                    names.add(n.split("\\")[-1])
        else:
            d = prov.root / "maps"
            if d.is_dir():
                names |= {f for f in os.listdir(d) if f.lower().endswith(".map")}
    seen, out = set(), []
    for n in sorted(names):
        if n.lower() not in seen:
            seen.add(n.lower())
            out.append(n)
    return out


def classify(name: str, art: str) -> str | None:
    text = f"{name} {art}"
    for k, rx in CLASS_RE:
        if rx.search(text):
            return k
    return None


def cluster(points, radius=3):
    """Single-link clusters of (x, y, ...) points within `radius` squares."""
    par = list(range(len(points)))

    def f(i):
        while par[i] != i:
            par[i] = par[par[i]]
            i = par[i]
        return i
    grid = defaultdict(list)
    for i, p in enumerate(points):
        grid[(p[0] // radius, p[1] // radius)].append(i)
    for i, p in enumerate(points):
        gx, gy = p[0] // radius, p[1] // radius
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for j in grid.get((gx + dx, gy + dy), []):
                    if j < i and abs(points[j][0] - p[0]) <= radius and abs(points[j][1] - p[1]) <= radius:
                        par[f(i)] = f(j)
    groups = defaultdict(list)
    for i in range(len(points)):
        groups[f(i)].append(points[i])
    return list(groups.values())


def survey_map(src, name, msgs, lsts, tiles_lst, writer_o, writer_v, stats):
    try:
        m = MapFile.parse(src.vfs.read(f"maps\\{name}"), src.proto)
    except Exception:
        return 0
    n_v = 0
    for ei, elev in enumerate(m.elevations):
        floors, roofs = m.floors[ei], m.roofs[ei]
        floored = [i for i, v in enumerate(floors) if (v & 0xFFF) > 1]
        if len(floored) < 100:
            continue
        roofed = {i for i, v in enumerate(roofs) if (v & 0xFFF) > 1}
        road = {i for i in floored if (floors[i] & 0xFFF) < len(tiles_lst) and ROAD_RE.match(tiles_lst[floors[i] & 0xFFF])}
        walls = set()
        objs = []
        for o in m.objects:
            if o.elevation != elev or not (0 <= o.tile < 40000):
                continue
            t = o.pid >> 24
            sx, sy = (o.tile % 200) // 2, (o.tile // 200) // 2
            if t == 3:
                walls.add((sx, sy))
                continue
            if t not in (0, 1, 2):
                continue
            lst = lsts.get(t, [])
            art = lst[o.fid & 0xFFF] if (o.fid & 0xFFF) < len(lst) else ""
            if SKIP.match(art):
                continue
            pr = src.proto(o.pid)
            nm = msgs.get(t, {}).get(pr.message_id, "") if pr is not None else ""
            k = "person" if t == 1 else classify(nm, art)
            if k is None:
                continue
            objs.append((sx, sy, k, nm or art))
        if not objs:
            continue
        # building squares: roofed floor, and walls
        bld = roofed | {y * 100 + x for x, y in walls}
        xs = [i % 100 for i in floored]
        ys = [i // 100 for i in floored]
        cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
        half = max(max(xs) - min(xs), max(ys) - min(ys)) / 2 or 1

        def where(x, y):
            s = y * 100 + x
            if s in roofed:
                return "indoors"
            if s in road:
                return "road"
            near_b = any(((y + dy) * 100 + x + dx) in bld for dx in range(-3, 4) for dy in range(-3, 4))
            return "by building" if near_b else "open"
        for x, y, k, nm in objs:
            w = where(x, y)
            edge = math.hypot(x - cx, y - cy) / half
            writer_o.writerow([src.name, name, elev, x, y, k, nm, w, round(edge, 2)])
            stats["class_where"][k][w] += 1
            stats["class_edge"][k].append(edge)
            stats["class_count"][(src.name, k)] += 1
        story = [o for o in objs if o[2] in STORY or o[2] == "person"]
        for g in cluster(story):
            kinds = Counter(o[2] for o in g)
            if len(g) < 3 or len(kinds) < 2:
                continue
            combo = "+".join(sorted(kinds))
            gx = sum(o[0] for o in g) / len(g)
            gy = sum(o[1] for o in g) / len(g)
            w = Counter(where(o[0], o[1]) for o in g).most_common(1)[0][0]
            names = "; ".join(sorted({o[3] for o in g})[:8])
            writer_v.writerow([src.name, name, elev, round(gx), round(gy), len(g), combo, w, names])
            stats["combos"][combo] += 1
            stats["combo_src"][(src.name, combo)] += 1
            for a in kinds:
                for b in kinds:
                    if a < b:
                        stats["pairs"][(a, b)] += 1
            n_v += 1
    return n_v


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--sources", default=",".join(SOURCES))
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    stats = {"class_where": defaultdict(Counter), "class_edge": defaultdict(list), "class_count": Counter(),
             "combos": Counter(), "combo_src": Counter(), "pairs": Counter()}
    fo = open(OUT / "objects.csv", "w", newline="", encoding="utf-8")
    fv = open(OUT / "vignettes.csv", "w", newline="", encoding="utf-8")
    wo, wv = csv.writer(fo), csv.writer(fv)
    wo.writerow(["source", "map", "elev", "x", "y", "class", "name", "where", "edge"])
    wv.writerow(["source", "map", "elev", "x", "y", "objects", "combo", "where", "names"])
    per_src = {}
    for sname in args.sources.split(","):
        try:
            src = port.open_source(sname)
        except Exception as e:
            print(f"{sname}: {e}")
            continue
        src.name = sname
        msgs = {0: msg_table(src, "pro_item.msg"), 1: msg_table(src, "pro_crit.msg"), 2: msg_table(src, "pro_scen.msg")}
        lsts = {}
        for t, d in ((0, "items"), (1, "critters"), (2, "scenery")):
            try:
                lsts[t] = [x.split(",")[0] for x in parse_lst(src.vfs.read(f"art\\{d}\\{d}.lst"))]
            except Exception:
                lsts[t] = []
        tiles_lst = parse_lst(src.vfs.read("art\\tiles\\tiles.lst"))
        n_maps = n_v = 0
        for name in list_maps(src):
            v = survey_map(src, name, msgs, lsts, tiles_lst, wo, wv, stats)
            n_maps += 1
            n_v += v
        per_src[sname] = {"maps": n_maps, "vignettes": n_v}
        print(f"{sname}: {n_maps} maps, {n_v} vignettes")
    fo.close()
    fv.close()
    summary = {
        "sources": per_src,
        "where_each_class_sits": {k: dict(v) for k, v in stats["class_where"].items()},
        "edge_share_outer_third": {k: round(sum(1 for e in v if e > 0.66) / len(v), 3)
                                   for k, v in stats["class_edge"].items() if v},
        "top_combos": stats["combos"].most_common(40),
        "top_pairs": [(f"{a}+{b}", n) for (a, b), n in stats["pairs"].most_common(30)],
        "combos_by_source": {s: [(c, n) for (ss, c), n in stats["combo_src"].most_common() if ss == s][:12]
                             for s in per_src},
        "class_count_by_source": {s: {k: n for (ss, k), n in stats["class_count"].items() if ss == s} for s in per_src},
    }
    (OUT / "summary.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
