#!/usr/bin/env python3
"""
stairs_survey.py - how Fallout 1, Fallout 2 and the total conversions (not
Olympus) take the player between floors, measured over every map.

    python tools/stairs_survey.py               -> build/stairs_survey/{stairs.csv,summary.json}

Floors are changed with scenery of the stairs, elevator and ladder types
(proto.cc: SCENERY_TYPE_STAIRS 1, ELEVATOR 2, LADDER_UP 3, LADDER_DOWN 4):
the player uses the object and is moved to the destination it carries
(objectDataRead: destinationBuiltTile = tile | elevation << 29, then
destinationMap). Exit grids that lead to another elevation of the same map
are counted too. For each stair object the survey records its art and name,
where it leads (same map or another, which elevation, up or down), the
walls and scenery within 4 hexes, and whether an exit grid lies within
3 hexes. The summary counts the art per game and lists example maps.
"""

from __future__ import annotations

import csv
import json
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import port  # noqa: E402
from foclib import MapFile, parse_lst  # noqa: E402
from story_survey import list_maps, msg_table  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "stairs_survey"
SOURCES = ["fo1", "fo2", "sonora", "nevada", "resurrection", "sotj", "yesterday"]
W = 200
KIND = {1: "stairs", 2: "elevator", 3: "ladder up", 4: "ladder down"}


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    f = open(OUT / "stairs.csv", "w", newline="", encoding="utf-8")
    w = csv.writer(f)
    w.writerow(["source", "map", "elev", "x", "y", "kind", "art", "name", "dest_map", "dest_elev", "dest_x",
                "dest_y", "same_map", "dir", "grid_near", "nearby"])
    art = defaultdict(Counter)
    same = defaultdict(Counter)
    examples = defaultdict(list)
    for sname in SOURCES:
        try:
            src = port.open_source(sname)
        except Exception as e:
            print(f"{sname}: {e}")
            continue
        scen_msg = msg_table(src, "pro_scen.msg")
        scen_lst = [x.split(",")[0] for x in parse_lst(src.vfs.read("art\\scenery\\scenery.lst"))]
        wall_lst = [x.split(",")[0] for x in parse_lst(src.vfs.read("art\\walls\\walls.lst"))]
        n = 0
        for name in list_maps(src):
            try:
                raw = src.vfs.read(f"maps\\{name}")
                m = MapFile.parse(raw, src.proto)
            except Exception:
                continue
            grids = [(o.elevation, o.tile) for o in m.objects if 0x05000010 <= o.pid <= 0x05000017]
            objs = []
            for o in m.objects:
                t = o.pid >> 24
                if t not in (2, 3) or not (0 <= o.tile < 40000):
                    continue
                lst = scen_lst if t == 2 else wall_lst
                a = lst[o.fid & 0xFFF].lower() if (o.fid & 0xFFF) < len(lst) else ""
                pr = src.proto(o.pid) if t == 2 else None
                objs.append((o, a, pr))
            for o, a, pr in objs:
                if pr is None or pr.subtype not in KIND:
                    continue
                bt, dm = struct.unpack_from(">2i", raw, o.off + 72 + 12 + 4)
                de, dt = (bt >> 29) & 7, bt & 0x3FFFFFF
                nm = scen_msg.get(pr.message_id, "")
                x, y = o.tile % W, o.tile // W
                same_map = dm in (m.map_id, -1) or dm == 0 and bt == 0
                d = "up" if de > o.elevation else "down" if de < o.elevation else "same"
                gnear = any(e == o.elevation and abs(t % W - x) <= 3 and abs(t // W - y) <= 3 for e, t in grids)
                near = Counter(f"{b}" for p, b, _ in objs if p is not o and p.elevation == o.elevation and
                               abs(p.tile % W - x) <= 4 and abs(p.tile // W - y) <= 4 and
                               not b.startswith(("block", "scrblk", "light")))
                w.writerow([sname, name, o.elevation, x, y, KIND[pr.subtype], a, nm, dm, de, dt % W, dt // W,
                            same_map, d, gnear, "; ".join(k for k, _ in near.most_common(8))])
                key = f"{KIND[pr.subtype]}: {a} ({nm})"
                art[sname][key] += 1
                art["all"][key] += 1
                same[sname]["same map" if same_map else "other map"] += 1
                if len(examples[sname]) < 30 and same_map:
                    examples[sname].append(f"{name} e{o.elevation} ({x},{y}) {KIND[pr.subtype]} {a} -> "
                                           f"e{de} ({dt % W},{dt // W}) {d}; grid near: {gnear}")
                n += 1
        print(f"{sname}: {n} stairs/ladders/elevators")
    f.close()
    summary = {"art": {k: v.most_common(30) for k, v in art.items()},
               "same_or_other_map": {k: dict(v) for k, v in same.items()},
               "examples": examples}
    (OUT / "summary.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
