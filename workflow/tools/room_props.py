#!/usr/bin/env python3
"""
room_props.py - how the originals and the total conversions furnish the rooms of
a kind of place, measured room by room, so a FOC map can be furnished the way
theirs are (owner, 2026-10-20: "those objects you placed do not fit with those
locations, how are you determining where to place objects?").

    python tools/room_props.py fo2:GECKPWPL:0 fo2:GECKTUNL:0 fo2:EPAMAIN1:0 ...
        -> build/room_props/rooms.json, report.txt, and a crop of every room in build/room_props/

For each level:
  * rooms: the floored hexes split by walls (any wall object, the invisible
    blocking walls too) and by doors; the rock field round an underground
    level (cav4009 / cav4010) and anything bigger than MAX_ROOM hexes is left
    out
  * a room's kind, from what is in it (KINDS: consoles make a control room,
    pumps, tanks and pipes a machine room, shelves, crates and lockers a store,
    desks and bookcases an office, beds and lockers quarters, rubble and bones a
    ruin), by the proto names in that game's pro_scen / pro_item messages
  * every object in it (scenery, items, containers; not walls, blockers, exit
    grids, lights or critters): its art family (the art name less its
    digits), its place against the walls - on a wall hex; against the back
    walls (an x-run one or two hexes above it, a y-run one or two hexes at
    lower x); against the front walls; in a corner (two walls); in the open -
    whether a blocker stands next to it (a piece bigger than its hex), and the
    objects within two hexes of it (what it is grouped with)
The report gives, per kind, rooms, objects per room and per 100 hexes, each
family's count and placement, and the commonest groups.
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
from collections import Counter, defaultdict, deque
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import port  # noqa: E402
from foclib import MapFile, parse_lst  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "room_props"
W = 200
DIRS = {0: (-1, W - 1, W, W + 1, 1, -W), 1: (-W - 1, -1, W, 1, 1 - W, -W)}
MAX_ROOM = 2500
MIN_ROOM = 12
BLOCKERS = {0x02000043, 0x02000158}
KINDS = [            # first match wins, by the share of a room's objects whose name matches
    ("control", re.compile(r"computer|console|terminal|control|monitor|panel|radio", re.I)),
    ("machine", re.compile(r"pump|tank|pipe|generator|turbine|reactor|vat|boiler|motor|machine|valve|fan|compressor|transformer|distributor", re.I)),
    ("store", re.compile(r"shel|crate|box|locker|barrel|footlock|cabinet|pallet|sack|bag|chest", re.I)),
    ("office", re.compile(r"desk|bookcase|bookshel|chair|table|filing|file|cabinet", re.I)),
    ("quarters", re.compile(r"bed|cot|dresser|toilet|sink|shower|couch|stove|fridge", re.I)),
    ("ruin", re.compile(r"rubble|debris|bone|skelet|junk|trash|blood|corpse", re.I)),
]


def msgs(src, name):
    try:
        t = src.vfs.read(f"text\\english\\game\\{name}").decode("cp1252", "replace")
    except Exception:
        return {}
    return {int(a): b for a, b in re.findall(r"\{(\d+)\}\{[^}]*\}\{([^}]*)\}", t)}


def family(art):
    return re.sub(r"\d+[a-z]?$", "", art.lower().replace(".frm", ""))


def survey(spec, out_dir):
    sname, mname, elev = spec.split(":")
    elev = int(elev)
    src = port.open_source(sname)
    raw = src.vfs.read(f"maps\\{mname}.map") if not mname.lower().endswith(".map") else src.vfs.read(f"maps\\{mname}")
    m = MapFile.parse(raw, src.proto)
    tiles = [x.split(",")[0].lower().replace(".frm", "") for x in parse_lst(src.vfs.read("art\\tiles\\tiles.lst"))]
    lsts = {0: [x.split(",")[0].lower() for x in parse_lst(src.vfs.read("art\\items\\items.lst"))],
            2: [x.split(",")[0].lower() for x in parse_lst(src.vfs.read("art\\scenery\\scenery.lst"))]}
    names = {0: msgs(src, "pro_item.msg"), 2: msgs(src, "pro_scen.msg")}
    fl = m.floors[m.elevations.index(elev)]
    walls, doors, blockers, objs = set(), set(), set(), []
    for o in m.objects:
        if o.elevation != elev or not (0 <= o.tile < W * W):
            continue
        t = o.pid >> 24
        x, y = o.tile % W, o.tile // W
        if t == 3:
            walls.add((x, y))
        elif o.pid in BLOCKERS:
            blockers.add((x, y))
        elif t in (0, 2):
            pr = src.proto(o.pid)
            if t == 2 and pr is not None and pr.subtype == 0:
                doors.add((x, y))
                continue
            a = o.fid & 0xFFF
            art = lsts[t][a] if a < len(lsts[t]) else "?"
            if re.match(r"^(light|egg|scrblk|block|exitgrd|ext2grd)", art):
                continue
            nm = names[t].get(pr.message_id, "") if pr is not None else ""
            objs.append(dict(x=x, y=y, art=art, fam=family(art), name=nm, type="item" if t == 0 else "scenery",
                             container=bool(t == 0 and pr is not None and pr.subtype == 1)))

    def floored(x, y):
        v = fl[(y // 2) * 100 + x // 2] & 0xFFF
        return v > 1 and not tiles[v].startswith(("cav4009", "cav4010")) if v < len(tiles) else False
    seen, rooms = set(), []
    for y0 in range(W):
        for x0 in range(W):
            if (x0, y0) in seen or (x0, y0) in walls or (x0, y0) in doors or not floored(x0, y0):
                continue
            comp, q = [], deque([(x0, y0)])
            seen.add((x0, y0))
            while q:
                x, y = q.popleft()
                comp.append((x, y))
                for d in DIRS[x & 1]:
                    n = y * W + x + d
                    nx, ny = n % W, n // W
                    if 0 <= n < W * W and abs(nx - x) <= 1 and (nx, ny) not in seen and (nx, ny) not in walls \
                            and (nx, ny) not in doors and floored(nx, ny):
                        seen.add((nx, ny))
                        q.append((nx, ny))
            if MIN_ROOM <= len(comp) <= MAX_ROOM:
                rooms.append(comp)
    out = []
    for i, comp in enumerate(rooms):
        cs = set(comp)
        inside = [o for o in objs if (o["x"], o["y"]) in cs or ((o["x"], o["y"]) in walls and
                  any((o["x"] + dx, o["y"] + dy) in cs for dx in (-1, 0, 1) for dy in (-1, 0, 1)))]
        for o in inside:
            x, y = o["x"], o["y"]
            if (x, y) in walls:
                o["place"] = "on wall"
            else:
                n = any((x, y - k) in walls for k in (1, 2))
                e = any((x - k, y) in walls for k in (1, 2))
                s = any((x, y + k) in walls for k in (1, 2))
                w = any((x + k, y) in walls for k in (1, 2))
                back, front = n or e, s or w
                o["place"] = ("corner" if (n and e) or (s and w) or (n and w) or (s and e) else
                              "back wall" if back else "front wall" if front else "open")
            o["blocker"] = any((x + dx, y + dy) in blockers for dx in (-2, -1, 0, 1, 2) for dy in (-2, -1, 0, 1, 2))
            o["with"] = sorted({p["fam"] for p in inside if p is not o and abs(p["x"] - x) <= 2 and abs(p["y"] - y) <= 2})
        text = " ".join(o["name"] + " " + o["art"] for o in inside)
        kind = "empty"
        if inside:
            scores = [(sum(1 for o in inside if rx.search(o["name"] + " " + o["art"])), k) for k, rx in KINDS]
            best = max(scores)
            kind = best[1] if best[0] else "other"
        xs = [p[0] for p in comp]
        ys = [p[1] for p in comp]
        out.append(dict(map=spec, room=i, kind=kind, hexes=len(comp), box=(min(xs), min(ys), max(xs), max(ys)),
                        objects=[{k: v for k, v in o.items()} for o in inside]))
    # crops of every furnished room, rendered as preview_source.py renders (tools/render.py)
    try:
        crop_rooms(src, m, elev, out, out_dir, sname, mname)
    except Exception as ex:   # noqa: BLE001 - a render failure is reported, the numbers still stand
        print(f"  (no crops for {spec}: {ex})")
    return out


def crop_rooms(src, m, elev, rooms, out_dir, sname, mname):
    import render
    ei = m.elevations.index(elev)
    img = render.render_map(src, m, ei).convert("RGB")
    pts = [render.square_xy(sq) for sq, tid in enumerate(m.floors[ei]) if tid > 1]
    for o in m.objects:
        if o.elevation == elev and 0 <= o.tile < W * W:
            x, y = render.hex_xy_screen(o.tile)
            pts += [(x - 100, y - 200), (x + 100, y + 60)]
    minx, miny = min(p[0] for p in pts), min(p[1] for p in pts)
    for r in rooms:
        if not r["objects"]:
            continue
        x0, y0, x1, y1 = r["box"]
        corners = [render.hex_xy_screen(y * W + x) for x in (x0, x1) for y in (y0, y1)]
        px0 = min(c[0] for c in corners) - minx - 40
        px1 = max(c[0] for c in corners) - minx + 80
        py0 = min(c[1] for c in corners) - miny - 150
        py1 = max(c[1] for c in corners) - miny + 60
        img.crop((int(px0), int(py0), int(px1), int(py1))).save(
            out_dir / f"{sname}_{mname}_{elev}_r{r['room']}_{r['kind']}.png")


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    rooms = []
    for spec in sys.argv[1:]:
        r = survey(spec, OUT)
        print(f"{spec}: {len(r)} rooms, {sum(1 for x in r if x['objects'])} furnished")
        rooms += r
    (OUT / "rooms.json").write_text(json.dumps(rooms, indent=1), encoding="utf-8")
    by_kind = defaultdict(list)
    for r in rooms:
        by_kind[r["kind"]].append(r)
    rep = []
    for kind, rs in sorted(by_kind.items(), key=lambda kv: -len(kv[1])):
        n_obj = [len(r["objects"]) for r in rs]
        dens = [100 * len(r["objects"]) / r["hexes"] for r in rs]
        rep.append(f"== {kind}: {len(rs)} rooms, {sorted(r['hexes'] for r in rs)[len(rs) // 2]} hexes median, "
                   f"objects/room median {sorted(n_obj)[len(n_obj) // 2]}, per 100 hexes median {sorted(dens)[len(dens) // 2]:.1f}")
        fam = defaultdict(Counter)
        names = {}
        groups = Counter()
        for r in rs:
            for o in r["objects"]:
                fam[o["fam"]][o["place"]] += 1
                fam[o["fam"]]["blocker" if o["blocker"] else "no blocker"] += 1
                names.setdefault(o["fam"], o["name"])
                if o["with"]:
                    groups[(o["fam"],) + tuple(o["with"][:3])] += 1
        for f, c in sorted(fam.items(), key=lambda kv: -(sum(kv[1].values()) // 2))[:18]:
            tot = sum(v for k, v in c.items() if "blocker" not in k)
            places = ", ".join(f"{k} {v}" for k, v in c.most_common() if "blocker" not in k)
            rep.append(f"   {f:12s} {tot:4d}  {names[f][:28]:28s}  {places}; blocker beside {c['blocker']}")
        rep.append("   groups: " + "; ".join(f"{'+'.join(g)} x{n}" for g, n in groups.most_common(8)))
        rep.append("   maps: " + ", ".join(sorted({r['map'] for r in rs})))
    (OUT / "report.txt").write_text("\n".join(rep), encoding="utf-8")
    print("\n".join(rep))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
