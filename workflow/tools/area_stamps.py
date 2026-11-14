#!/usr/bin/env python3
"""
area_stamps.py - cut graveyards, junkyards and ruins out of the original
Fallouts' maps as gecko stamps, beside building_stamps.py's buildings.

    python tools/area_stamps.py --sources fo1,fo2,sonora,nevada,resurrection,olympus --out "C:/Users/.../Desktop/Gecko Stamps"
    python tools/area_stamps.py --sources fo1,fo2 --portable --out "<dir>/Fallout Stamps"        # shareable, FO2 ids
    python tools/area_stamps.py --sources sonora --native --out "<dir>/Sonora Stamps"           # shareable, the game's own ids

Each kind goes in its own folder (Graveyards, Junkyards, Ruins); the stamp
name is "<Game> <MAP> <n> <floor>", like the buildings. Objects only: no
ground tiles, so a stamp drops onto whatever ground the map already has.

  Graveyard  GRAVE_MIN or more grave markers (graves, headstones, crosses,
             coffins, tombs) within GRAVE_LINK hexes of each other, with
             the fence round them: every fence run (fence walls, and fence,
             gate and post scenery) that comes within FENCE_REACH hexes of
             a grave and does not run more than FENCE_SPREAD hexes past
             them, so a town's long fence stays behind. Everything else
             standing inside the fence comes too.
  Junkyard   JUNK_MIN or more junk objects (piles, tires, wrecks, rubble,
             trash) within JUNK_LINK hexes of each other, at least PILE_MIN
             of them junk piles or tires: a few cars along a road are not a
             junkyard.
  Ruin       a run of walls that is not part of a complete building
             (building_stamps.extract), RUIN_MIN pieces or more, that turns
             a corner (RUIN_SPAN squares both ways), so a straight compound
             wall is not a ruin, and that no longer closes round the ground
             it stands on (an intact roofless building is not a ruin). Only
             on the ground level of maps with roofs: underground maps (vaults,
             basements, bunkers) have no roofs, and their corridors are not
             ruins. Fences, railings, tents, vault and cave walls and
             invisible blockers are not ruin walls.

Hidden objects and placeholder art never come along: the red "UNUSED ART"
box (mstr69 and every FRM byte-identical to it) and the green box that
Fallout 2's scenery copies of furniture draw. (The other mstr*.frm are the
Master's goo and blood decals, real art.) A wall run with roof tiles still over
it is a small building (under building_stamps' MIN_ROOF), not a ruin.

Around each one the stamp also takes what stands within REACH hexes of it:
scenery (bones, dead trees, rubble, signs), containers, and fence walls.
Nothing that belongs to a complete building comes along, and no critters.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import port  # noqa: E402
from building_stamps import (CREDITS, FENCE_FAMILIES, FLOORS, GAME, ITEM_CONTAINER, OFF_FLAGS, SCENERY_DOOR,  # noqa: E402
                             Ids, extract, fam, foc_only)
from foclib import GameIndex, parse_lst  # noqa: E402
from foclib.formats import MapFile, hex_xy, pid_type  # noqa: E402
from map_survey import list_maps  # noqa: E402

GRAVE = {"grave", "gravstn", "tomb", "tombst", "headst", "cross", "coffin"}
JUNK = {"junk", "njunk", "hjunk", "carjunk", "junklit", "tire", "tirs", "car", "shorcar", "carhms", "kizicar",
        "truck", "browreck", "outbus", "rubble", "trash"}
PILES = {"junk", "njunk", "hjunk", "carjunk", "junklit", "tire", "tirs"}
NOT_RUIN = FENCE_FAMILIES | {"ca", "cave", "block", "black",
                              "ybk", "yar", "yrt", "ylf",                      # tents
                              "rl", "srl", "arail", "rustb", "gizw", "fns", "fnc",  # railings and yard fences
                              "sada", "nsada",                                 # Nevada's tanker cars
                              "va", "vb", "vaw", "vcn", "vd", "vt", "ve", "vtx", "vwindw", "vltex", "vltwd"}
OBJECT_HIDDEN = 0x1
RUIN_ROOFED = 0.7        # share of a wall run's box under roof tiles that makes it a building
# areas checked by eye that are not what the rule took them for, by (game, map, kind, n)
SKIP = {("Olympus", "PNCITY02", "Graveyards", 1)}       # a cliff face with Olympus "coffin" art
RUIN_ROOFS = 20          # roof tiles an elevation needs before it counts as outdoors
FENCE_WALLS = FENCE_FAMILIES                      # fence, tfenc
FENCE_SCENERY = {"fence", "gate", "fencdor", "posts", "brokpost"}
FENCE_REACH, FENCE_SPREAD = 8, 12

GRAVE_MIN, GRAVE_LINK = 4, 5
JUNK_MIN, JUNK_LINK, PILE_MIN = 10, 5, 5
RUIN_MIN, RUIN_SPAN, RUIN_LINK, RUIN_MAX = 12, 3, 2, 600
REACH = 2


def art_base(name: str) -> str:
    return re.sub(r"\d.*$", "", name.lower().split(".")[0]).rstrip("_")


def clusters(points: dict, link: int) -> list[list]:
    """Group objects whose hexes lie within `link` of each other (single link)."""
    items = list(points.items())
    parent = list(range(len(items)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    by_cell = {}
    for i, (_, (hx, hy)) in enumerate(items):
        by_cell.setdefault((hx // link, hy // link), []).append(i)
    for i, (_, (hx, hy)) in enumerate(items):
        cx, cy = hx // link, hy // link
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for j in by_cell.get((cx + dx, cy + dy), ()):
                    if j > i:
                        jx, jy = items[j][1]
                        if abs(jx - hx) <= link and abs(jy - hy) <= link:
                            parent[find(i)] = find(j)
    groups = {}
    for i, (o, _) in enumerate(items):
        groups.setdefault(find(i), []).append(o)
    return list(groups.values())


def enclosed(hexes: set) -> bool:
    """True when walls (and the doors in them) close round some ground: an
    intact building that has no roof, however small. Worked on the hex grid,
    walls thickened by one hex so a one-hex crack doesn't count as open."""
    wall = {(x + dx, y + dy) for x, y in hexes for dx in (-1, 0, 1) for dy in (-1, 0, 1)}
    xs = [x for x, _ in wall]
    ys = [y for _, y in wall]
    x0, x1, y0, y1 = min(xs) - 1, max(xs) + 1, min(ys) - 1, max(ys) + 1
    outside, stack = {(x0, y0)}, [(x0, y0)]
    while stack:
        x, y = stack.pop()
        for q in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if x0 <= q[0] <= x1 and y0 <= q[1] <= y1 and q not in wall and q not in outside:
                outside.add(q)
                stack.append(q)
    inner = (x1 - x0 + 1) * (y1 - y0 + 1) - len(outside) - len(wall)
    return inner >= 4


def find_areas(gi, m: MapFile, ei: int, raw: bytes, wl, sc, it):
    """Yield (kind, core objects, all objects) for one elevation."""
    elev = m.elevations[ei]
    objs = [o for o in m.objects if o.elevation == elev and 0 <= o.tile < 40000]

    # whatever belongs to a complete building stays out of every area
    taken, built_sq = set(), set()
    for roof, keep, *_ in extract(gi, m, ei, raw, wl, sc, it):
        taken |= {id(o) for o in keep}
        built_sq |= roof

    def name_of(o):
        t, i = pid_type(o.pid), o.fid & 0xFFF
        if t == 2 and i < len(sc):
            return sc[i]
        if t == 0 and i < len(it):
            return it[i]
        if t == 3 and i < len(wl):
            return wl[i]
        return ""

    def hidden(o):
        return int.from_bytes(raw[o.off + OFF_FLAGS:o.off + OFF_FLAGS + 4], "big") & OBJECT_HIDDEN

    free = [o for o in objs if id(o) not in taken and pid_type(o.pid) in (0, 2, 3) and not hidden(o)
            and (hex_xy(o.tile)[0] // 2, hex_xy(o.tile)[1] // 2) not in built_sq]
    base = {id(o): art_base(name_of(o)) for o in free}

    def surroundings(core, walls_too):
        near = {(hx + dx, hy + dy) for o in core for hx, hy in [hex_xy(o.tile)]
                for dx in range(-REACH, REACH + 1) for dy in range(-REACH, REACH + 1)}
        out = list(core)
        core_ids = {id(o) for o in core}
        for o in free:
            if id(o) in core_ids or hex_xy(o.tile) not in near:
                continue
            t = pid_type(o.pid)
            if t == 2:
                out.append(o)
            elif t == 0:
                pr = gi.proto(o.pid)
                if pr is not None and pr.subtype == ITEM_CONTAINER:
                    out.append(o)
            elif t == 3 and (walls_too or fam(name_of(o)) in FENCE_FAMILIES):
                out.append(o)
        return out

    graves = {o: hex_xy(o.tile) for o in free if pid_type(o.pid) in (0, 2) and base[id(o)] in GRAVE}
    fences = {o: hex_xy(o.tile) for o in free
              if (pid_type(o.pid) == 3 and fam(name_of(o)) in FENCE_WALLS)
              or (pid_type(o.pid) == 2 and base[id(o)] in FENCE_SCENERY)}
    runs = clusters(fences, 2)

    def with_fence(core):
        """The area with the fence round it and what stands inside the fence."""
        gx = [hex_xy(o.tile)[0] for o in core]
        gy = [hex_xy(o.tile)[1] for o in core]
        fence = []
        for run in runs:
            pts = [hex_xy(o.tile) for o in run]
            near = any(abs(px - qx) <= FENCE_REACH and abs(py - qy) <= FENCE_REACH
                       for px, py in pts for qx, qy in zip(gx, gy))
            inside = (min(p[0] for p in pts) >= min(gx) - FENCE_SPREAD and max(p[0] for p in pts) <= max(gx) + FENCE_SPREAD
                      and min(p[1] for p in pts) >= min(gy) - FENCE_SPREAD and max(p[1] for p in pts) <= max(gy) + FENCE_SPREAD)
            if near and inside:
                fence += run
        keep = surroundings(core, False)
        if fence:
            have = {id(o) for o in keep}
            keep += [o for o in fence if id(o) not in have]
            have |= {id(o) for o in fence}
            area = [hex_xy(o.tile) for o in core + fence]
            x0, x1 = min(a[0] for a in area), max(a[0] for a in area)
            y0, y1 = min(a[1] for a in area), max(a[1] for a in area)
            for o in free:
                hx, hy = hex_xy(o.tile)
                if id(o) in have or not (x0 <= hx <= x1 and y0 <= hy <= y1):
                    continue
                tt = pid_type(o.pid)
                if tt == 2:
                    keep.append(o)
                elif tt == 0:
                    pr = gi.proto(o.pid)
                    if pr is not None and pr.subtype == ITEM_CONTAINER:
                        keep.append(o)
        return keep

    for core in clusters(graves, GRAVE_LINK):
        if len(core) >= GRAVE_MIN:
            yield "Graveyards", core, with_fence(core)

    junk = {o: hex_xy(o.tile) for o in free if pid_type(o.pid) in (0, 2) and base[id(o)] in JUNK}
    for core in clusters(junk, JUNK_LINK):
        if len(core) >= JUNK_MIN and sum(base[id(o)] in PILES for o in core) >= PILE_MIN:
            yield "Junkyards", core, with_fence(core)

    # ruins: outdoor ground level only
    if elev != 0 or sum(1 for v in m.roofs[ei] if (v & 0xFFF) > 1) < RUIN_ROOFS:
        return
    walls = {o: hex_xy(o.tile) for o in free if pid_type(o.pid) == 3
             and fam(name_of(o)) not in NOT_RUIN and not name_of(o).lower().startswith("block")}
    for core in clusters(walls, RUIN_LINK):
        if not RUIN_MIN <= len(core) <= RUIN_MAX:
            continue
        xs = [hex_xy(o.tile)[0] // 2 for o in core]
        ys = [hex_xy(o.tile)[1] // 2 for o in core]
        if max(xs) - min(xs) + 1 < RUIN_SPAN or max(ys) - min(ys) + 1 < RUIN_SPAN:
            continue
        shut = {hex_xy(o.tile) for o in core}
        near = {(hx + dx, hy + dy) for hx, hy in shut for dx in (-2, -1, 0, 1, 2) for dy in (-2, -1, 0, 1, 2)}
        for o in objs:
            if pid_type(o.pid) == 2 and hex_xy(o.tile) in near:
                pr = gi.proto(o.pid)
                if pr is not None and pr.subtype == SCENERY_DOOR:
                    shut.add(hex_xy(o.tile))
        if enclosed(shut):
            continue
        box = [(x, y) for x in range(min(xs), max(xs) + 1) for y in range(min(ys), max(ys) + 1)]
        if sum(1 for x, y in box if (m.roofs[ei][y * 100 + x] & 0xFFF) > 1) >= RUIN_ROOFED * len(box):
            continue
        yield "Ruins", core, surroundings(core, True)


def placeholder_art(foc: GameIndex, sc: list[str]) -> set[int]:
    """FOC scenery indices that draw an "UNUSED ART" box: mstr69 and every
    FRM byte-identical to it, and every small image that is mostly the
    bright green of the box Fallout 2's scenery copies of furniture
    (bookcases, desks, shelves, stairs...) draw; their real art is under items."""
    import hashlib
    import render
    pal = render.load_palette(foc)
    out = set()
    unused = hashlib.sha1(foc.vfs.read("art/scenery/mstr69.frm"), usedforsecurity=False).hexdigest()
    for i, n in enumerate(sc):
        try:
            raw = foc.vfs.read(f"art/scenery/{n}")
        except Exception:
            continue
        if hashlib.sha1(raw, usedforsecurity=False).hexdigest() == unused:
            out.add(i)
            continue
        try:
            im = render.Frm.parse(raw).to_image(pal).convert("RGBA")
        except Exception:
            continue
        px = [q for q in im.getdata() if q[3] > 0]
        if not px or len(px) > 1200:
            continue
        green = sum(1 for r, g, b, _ in px if g > 120 and g > 1.6 * r and g > 1.6 * b)
        if green >= 0.5 * len(px):
            out.add(i)
    return out


def write(sources, out: Path, native: bool = False, portable: bool = False) -> None:
    foc = GameIndex(pristine=False)
    foc_sc = [n.lower() for n in parse_lst(foc.vfs.read("art\\scenery\\scenery.lst"))]
    marker_art = placeholder_art(foc, foc_sc)
    seen: set[str] = set()
    counts = Counter()
    manifest = []
    for src in sources:
        gi = GameIndex() if src == "fo2" else port.open_source(src)
        ids = Ids(src, gi, foc, native)
        # markers are judged on FOC's art names: TCs name the marker art their own way
        ids_foc = Ids(src, gi, foc, False) if native else ids
        wl = parse_lst(gi.vfs.read("art\\walls\\walls.lst"))
        sc = parse_lst(gi.vfs.read("art\\scenery\\scenery.lst"))
        it = parse_lst(gi.vfs.read("art\\items\\items.lst"))
        per = Counter()
        game = GAME.get(src, src)
        for mp in list_maps(gi):
            try:
                raw = gi.vfs.read(mp)
                m = MapFile.parse(raw, gi.proto)
            except Exception:
                continue
            mname = mp.split("\\")[-1][:-4].upper()
            for ei in range(len(m.elevations)):
                for kind, core, keep in find_areas(gi, m, ei, raw, wl, sc, it):
                    core_ids = {id(o) for o in core}
                    hxs = [hex_xy(o.tile)[0] for o in keep]
                    hys = [hex_xy(o.tile)[1] for o in keep]
                    ax, ay = min(hxs) // 2 * 2, min(hys) // 2 * 2
                    objs, lost_core = [], 0
                    for o in keep:
                        rf = ids_foc.obj(o) if ids_foc is not ids else None
                        r = ids.obj(o)
                        if r is None:
                            lost_core += id(o) in core_ids
                            continue
                        chk = rf or r
                        if (chk[1] >> 24) & 0xF == 2 and (chk[1] & 0xFFF) in marker_art:
                            continue
                        hx, hy = hex_xy(o.tile)
                        flags = int.from_bytes(raw[o.off + OFF_FLAGS:o.off + OFF_FLAGS + 4], "big")
                        objs.append({"dxHex": hx - ax, "dyHex": hy - ay, "proPid": r[0], "frmPid": r[1],
                                     "direction": o.rotation % 6, "flags": flags})
                    if lost_core:
                        counts["missing art"] += 1
                        continue
                    if portable and not native and any(foc_only(o["proPid"], o["frmPid"]) for o in objs):
                        counts["foc_only"] += 1
                        continue
                    key = hashlib.sha1(json.dumps(sorted((o["dxHex"], o["dyHex"], o["proPid"], o["frmPid"])
                                                         for o in objs)).encode(), usedforsecurity=False).hexdigest()
                    if key in seen:
                        counts["duplicate"] += 1
                        continue
                    seen.add(key)
                    per[(kind, mname)] += 1
                    if (game, mname, kind, per[(kind, mname)]) in SKIP:
                        continue
                    floor_name = FLOORS[m.elevations[ei]]
                    name = f"{game} {mname} {per[(kind, mname)]} {floor_name}"
                    d = out / kind
                    d.mkdir(parents=True, exist_ok=True)
                    stamp = {"name": name, "version": 1,
                             "variants": [{"label": "default", "anchorHex": ay * 200 + ax,
                                           "objects": objs, "floor": [], "roof": []}],
                             "credit": CREDITS.get(src, game),
                             "source": {"game": game, "map": mname, "elevation": m.elevations[ei],
                                        "kind": kind, "coreObjects": len(core),
                                        "ids": ("the game's own" if native or src == "fo2"
                                                else "Fallout 2's")}}
                    (d / f"{name}.json").write_text(json.dumps(stamp, separators=(",", ":")), encoding="utf-8")
                    counts[kind] += 1
                    manifest.append((kind, name, len(core), len(objs)))
        print(f"{src}: " + ", ".join(f"{k} {sum(v for (kk, _), v in per.items() if kk == k)}"
                                     for k in ("Graveyards", "Junkyards", "Ruins")))
    print({k: v for k, v in counts.items()})
    lines = ["# Area stamps", "", "Cut from the original Fallouts' maps by tools/area_stamps.py: graveyards,",
             "junkyards and ruins, objects only (no ground tiles).", "",
             "| Kind | Stamp | Core objects | Objects |", "|---|---|---|---|"]
    lines += [f"| {a} | {b} | {c} | {d} |" for a, b, c, d in sorted(manifest)]
    (out / "AREAS.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--sources", default="fo1,fo2")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--native", action="store_true", help="keep each game's own ids")
    ap.add_argument("--portable", action="store_true", help="skip stamps that use ids only FOC has")
    args = ap.parse_args()
    write(args.sources.split(","), args.out, args.native, args.portable)
    return 0


if __name__ == "__main__":
    sys.exit(main())
