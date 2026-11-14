#!/usr/bin/env python3
"""
building_stamps.py - cut every building out of the original Fallouts' maps as
a gecko stamp, sorted into folders by construction and size.

    python tools/building_stamps.py --survey                 # wall families per building
    python tools/building_stamps.py --out "C:/Users/.../Desktop/Gecko Stamps"
    # the owner's library: every game in one tree, a folder per construction
    python tools/building_stamps.py --sources fo1,fo2,sonora,nevada,resurrection,olympus --no-size --no-floor --out "C:/Users/.../Desktop/Gecko Stamps"

A building is a roof component (tools/buildings.py) of MIN_ROOF squares or
more. Only the building comes across:
  * floor tiles under its roof, and its roof tiles; no ground around it
  * its walls: the largest connected chain of wall hexes on or next to the
    roof (a neighbouring building's walls or an alley wall are separated
    from it by an empty hex, so they stay behind)
  * scenery inside it or set in its walls (doors, windows, furniture,
    fixtures, blockers that go with the walls)
  * container items inside it (bookcases, desks, lockers); no loose loot
  * no critters, no misc (exit grids, scroll blockers, spatial markers)

Stamps store raw ids and are read in FOC's editing profile, whose base is
Fallout 2 + RPU, so Fallout 2 ids are kept as they are. Fallout 1 ids go
through port_stamp.Converter (by pid where the art matches, by art bytes
otherwise); anything that would need a port is dropped, not ported, and
counted. Identical buildings that recur across maps are written once.

Folders: <construction>/<size>/<source> <map> <n>.json, construction named
after the building's dominant wall family (FAMILIES below), size by roofed
squares (SIZES).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import port  # noqa: E402
from buildings import roof_components  # noqa: E402
from foclib import GameIndex, parse_lst  # noqa: E402
from foclib.formats import MapFile, hex_xy, pid_type  # noqa: E402
from map_survey import list_maps  # noqa: E402

MIN_ROOF = 24
REJECTS = Counter()      # why roofs were not written, for the report
SCENERY_DOOR = 0
FENCE_FAMILIES = {"fen", "fence", "tfenc"}
MIN_COVER = 0.6          # roof squares / area of the walls' bounding box
MAX_GAP = 2             # longest run of unwalled roof edge, in squares: a doorway
MAX_OPEN = 0.08         # share of the roof edge that may be open (doorways) in all
# scenery that belongs to the ground around a building, not to the building
TERRAIN = re.compile(r"(weed|bush|tree|tre\d|grass|cact|shrub|brush|flower|plant|rock|roks|boulder|"
                     r"rubbl|pebbl|strlit|lamp|lite\d|light|wreck|car\d|truck|tire|tirs|junk|trash|"
                     r"skel|bones|skull|dirt|mud|puddle|crater|hole|sign|ss1\d\d)", re.I)
OFF_FLAGS = 0x24
ITEM_CONTAINER = 1


def fam(n: str) -> str:
    return re.sub(r"\d.*$", "", n.lower().split(".")[0])


def complete(roof: set, chain: set) -> bool:
    """Walls all the way round. Each square on the roof's edge needs a wall
    on it or on the square just outside it (on the open side); unwalled
    edge squares that touch form a gap, and a gap longer than a doorway
    (MAX_GAP squares), or more than MAX_OPEN of the edge in all, means a
    side is down: a ruin, a lean-to or a roofed walkway, not a building."""
    chain_sq = {(hx // 2, hy // 2) for hx, hy in chain}
    open_sq = set()
    edge = 0
    for x, y in roof:
        # open directions; the wall may stand on the edge square, just outside
        # it, or (under a roof overhang, typically north and west) just inside
        dirs = [(dx, dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)) if (x + dx, y + dy) not in roof]
        if not dirs:
            continue
        edge += 1
        near = {(x, y)} | {(x + dx, y + dy) for dx, dy in dirs} | {(x - dx, y - dy) for dx, dy in dirs}
        if not near & chain_sq:
            open_sq.add((x, y))
    if not edge or len(open_sq) > MAX_OPEN * edge:
        return False
    seen = set()
    for s0 in open_sq:
        if s0 in seen:
            continue
        stack, run = [s0], 0
        seen.add(s0)
        while stack:
            x, y = stack.pop()
            run += 1
            for q in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if q in open_sq and q not in seen:
                    seen.add(q)
                    stack.append(q)
        if run > MAX_GAP:
            return False
    return True


def extract(gi: GameIndex, m: MapFile, ei: int, raw: bytes, wl, sc, it):
    """Yield (roof squares, [objects], floor {sq: tile}, roof {sq: tile}) per building."""
    elev = m.elevations[ei]
    objs = [o for o in m.objects if o.elevation == elev and 0 <= o.tile < 40000]
    walls = [o for o in objs if pid_type(o.pid) == 3]
    for comp in roof_components(m, ei):
        if len(comp) < MIN_ROOF:
            continue
        roof = {(q % 100, q // 100) for q in comp}
        ring = {(x + dx, y + dy) for x, y in roof for dx in (-1, 0, 1) for dy in (-1, 0, 1)}
        wall_hexes = {hex_xy(o.tile) for o in walls if (hex_xy(o.tile)[0] // 2, hex_xy(o.tile)[1] // 2) in ring}
        chains, seen = [], set()
        for s in wall_hexes:
            if s in seen:
                continue
            stack, ch = [s], set()
            seen.add(s)
            while stack:
                hx, hy = stack.pop()
                ch.add((hx, hy))
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        q = (hx + dx, hy + dy)
                        if q in wall_hexes and q not in seen:
                            seen.add(q)
                            stack.append(q)
            chains.append(ch)
        # every chain standing on the building's own roof (a doorway splits a
        # wall line into several chains); a chain only beside the roof - an
        # alley wall, the next building's wall line - stays behind
        # A building's south and east walls usually stand one square outside
        # its roof, and a doorway can cut them off from the rest; those count
        # too. Walls beside the other sides do not.
        se_edge = {(x + dx, y + dy) for x, y in roof for dx, dy in ((1, 0), (0, 1), (1, 1))}
        chain = set()
        for ch in chains:
            if any((hx // 2, hy // 2) in roof or (hx // 2, hy // 2) in se_edge for hx, hy in ch):
                chain |= ch
        if len(chain) < 8:
            REJECTS["no walls"] += 1
            continue
        # a fence run attached to a building is yard, not building
        wall_fam = {hex_xy(o.tile): fam(wl[o.fid & 0xFFF]) for o in walls
                    if hex_xy(o.tile) in chain and (o.fid & 0xFFF) < len(wl)}
        fence = {h for h, f in wall_fam.items() if f in FENCE_FAMILIES}
        if len(fence) < 0.5 * len(chain):
            chain -= fence
        if not complete(roof, chain):
            REJECTS["open side"] += 1
            continue
        # the roof must cover the ground the walls stand round: a ruin keeps
        # its walls but not its roof
        xs = [hx // 2 for hx, hy in chain]
        ys = [hy // 2 for hx, hy in chain]
        if len(roof) < MIN_COVER * (max(xs) - min(xs) + 1) * (max(ys) - min(ys) + 1):
            REJECTS["roof smaller than walls"] += 1
            continue
        near_wall = {(hx + dx, hy + dy) for hx, hy in chain for dx in (-1, 0, 1) for dy in (-1, 0, 1)}
        keep = []
        for o in objs:
            t = pid_type(o.pid)
            hx, hy = hex_xy(o.tile)
            sq = (hx // 2, hy // 2)
            if t == 3:
                if (hx, hy) in chain:
                    keep.append(o)
            elif t == 2:
                name = sc[o.fid & 0xFFF].lower() if (o.fid & 0xFFF) < len(sc) else ""
                pr = gi.proto(o.pid)
                if TERRAIN.search(name):
                    continue                        # weeds, rocks, trees, lamps, wrecks: terrain
                if sq in roof or (pr is not None and pr.subtype == SCENERY_DOOR and (hx, hy) in near_wall):
                    keep.append(o)
            elif t == 0:
                pr = gi.proto(o.pid)
                if pr is not None and pr.subtype == ITEM_CONTAINER and sq in roof:
                    keep.append(o)
        floor = {x + 100 * y: m.floors[ei][y * 100 + x] & 0xFFF for x, y in roof if (m.floors[ei][y * 100 + x] & 0xFFF) > 1}
        rtiles = {x + 100 * y: m.roofs[ei][y * 100 + x] & 0xFFF for x, y in roof}
        fams = Counter(fam(wl[o.fid & 0xFFF]) for o in keep if pid_type(o.pid) == 3 and (o.fid & 0xFFF) < len(wl)
                       and not wl[o.fid & 0xFFF].lower().startswith("block"))
        yield roof, keep, floor, rtiles, fams


# wall family -> construction folder. A building goes in the folder most of
# its walls belong to, counted per folder (classify), from the art the stamp
# actually uses: FOC's own names, with the one-letter tag ported art carries
# (ids.SOURCE_TAG: s Sonora, n Nevada, o Olympus, r Resurrection) stripped.
FAMILIES = {
    # FO2's nec set is dark concrete panels and brick bases, often both in one building
    "Brick & Concrete": ["nec", "flg"],
    "Tents": ["ybk", "yar", "yrt", "ylf"],
    "Adobe": ["adb", "adw"],
    "Wood & Scrap": ["jas", "jbs", "jcs", "jds", "jdd", "jaw", "jad", "jcd", "jcw", "cnx", "chi", "her", "cni",
                     "templ", "gizw"],
    "Corrugated Metal": ["ware", "gar", "tsna", "tsnb", "tsne", "cargt", "carw", "corn"],
    "Military & Metal": ["mmb", "mbw", "enclav", "st", "stab", "stay", "stat", "bos", "ship", "btnk", "shutl",
                         "dv"],
    "Stone & Plaster": ["stb", "cabr", "nuev", "vill", "anal", "temple", "vgat", "cath", "hotel", "rngr"],
    "Vault": ["va", "vb", "vaw", "vd", "vt", "ve", "vcn", "vtx", "vwindw", "vltex", "vltwd"],
    "Caves & Rock": ["ca", "cave"],
    "Fences & Stockades": ["fen", "fence", "tfenc"],
    "Ruins & Overgrown": ["dayg"],
}
# fittings, not construction: bars and counters, pipes, railings, door
# frames and doors, columns, statues, blank filler, interior trim
FITTINGS = {"bar", "black", "pap", "pac", "pbp", "pbc", "pbm", "rl", "rustb", "inside", "ncrdor",
            "dorfrm", "steldor", "steldo", "velvdr", "adc", "inf", "column", "necrn", "gprv",
            "poptr", "cwall"}
TAGS = "snor"
# ported art whose name is a vanilla family's but whose look isn't, checked
# by its full FOC name (tag included) before the tag is stripped. Checked by
# eye against contact sheets of every stamp, 2026-10-01.
TAGGED = {
    "oadb": "Brick & Concrete",      # Olympus "adb": dark concrete panels, not adobe
    "ocargt": "Wood & Scrap",        # Olympus cargo walls: weathered planks
    "ocarw": "Wood & Scrap",
    "sgar": "Brick & Concrete",      # Sonora "gar": grey brick
    "srngr": "Stone & Plaster",      # tan plaster with a brown band
    "sdayg": "Brick & Concrete",     # dark stone block, whole buildings
    "sanal": "Caves & Rock",         # blue cave rock
    "sher": "Corrugated Metal",      # rusty corrugated sheet
}
# single stamps the wall count gets wrong, by (game, map, elevation, anchor
# hex): the numbering differs between FOC and native ids, the anchor doesn't
STAMP_FOLDER = {
    ("Olympus", "RBFABRIC", 0, 19288): "Corrugated Metal",   # dark metal panels; its ocarw is trim
    # Sonora's "nuev" set: these are braced and riveted metal panels, not plaster
    ("Sonora", "CNGIDR", 0, 12094): "Military & Metal",
    ("Sonora", "CNGIDR", 1, 14452): "Military & Metal",
    ("Sonora", "CNGIDR", 2, 18528): "Military & Metal",
    ("Sonora", "CNLAB", 0, 13280): "Military & Metal",
    ("Sonora", "CNLAB", 0, 14916): "Military & Metal",
    ("Sonora", "CNLAB", 0, 18068): "Military & Metal",
    # Sonora's "her" set is rusty sheet metal and yellow plaster under one name
    ("Sonora", "HERMO1", 0, 11690): "Stone & Plaster",
    ("Sonora", "HERMO2", 0, 10910): "Stone & Plaster",
    ("Sonora", "HERMO2", 0, 12482): "Stone & Plaster",
    ("Sonora", "HERMO2", 0, 15710): "Stone & Plaster",
    ("Sonora", "HERMO2", 0, 22110): "Stone & Plaster",
    ("Sonora", "HERMO2", 0, 23282): "Stone & Plaster",
    ("Sonora", "HERMO2", 0, 26510): "Stone & Plaster",
}


def wall_folder(name: str) -> str | None:
    """Construction folder for one wall art name, None for a fitting or an unknown."""
    f = fam(name)
    if f in FITTINGS:
        return None
    if f in TAGGED:
        return TAGGED[f]
    if f in FOLDER_OF:
        return FOLDER_OF[f]
    if f[:1] in TAGS and f[1:] in FOLDER_OF:
        return FOLDER_OF[f[1:]]
    return None


def classify(objs: list, walls: list[str]) -> str | None:
    """The folder most of a stamp's walls belong to (block.frm and fittings
    don't count)."""
    votes = Counter()
    for o in objs:
        if (o["frmPid"] >> 24) & 0xF != 3:
            continue
        i = o["frmPid"] & 0xFFF
        if i >= len(walls) or walls[i].lower().startswith("block"):
            continue
        folder = wall_folder(walls[i])
        if folder:
            votes[folder] += 1
    return votes.most_common(1)[0][0] if votes else None
GAME = {"fo1": "Fallout 1", "fo2": "Fallout 2", "sonora": "Sonora", "nevada": "Nevada",
        "resurrection": "Resurrection", "olympus": "Olympus"}
FOLDER_OF = {f: k for k, fs in FAMILIES.items() for f in fs}
FLOORS = {0: "1st Floor", 1: "2nd Floor", 2: "3rd Floor"}
SIZES = [(60, "Small"), (150, "Medium"), (400, "Large"), (10 ** 9, "Huge")]


def foc_only(pro: int, frm: int) -> bool:
    """An id past the base game's lists: only FOC has it."""
    from ids import FOC_START
    t = (pro >> 24) & 0xF
    pk = {0: "proto_items", 2: "proto_scenery", 3: "proto_walls", 5: "proto_misc"}.get(t)
    ak = {0: "art_items", 2: "art_scenery", 3: "art_walls", 5: "art_misc"}.get((frm >> 24) & 0xF)
    return (pk is not None and (pro & 0xFFFFFF) >= FOC_START[pk]) or \
           (ak is not None and ak in FOC_START and (frm & 0xFFFFFF) >= FOC_START[ak])


def size_name(n: int) -> str:
    return next(name for top, name in SIZES if n < top)


class Ids:
    """Source ids -> FOC ids. Fallout 2 maps are read from FOC's own base, so
    their ids are already FOC's. Fallout 1 goes through port_stamp.Converter
    in dry-run mode: a piece that would need porting is dropped."""

    def __init__(self, src: str, gi: GameIndex, foc: GameIndex, native: bool = False):
        self.src, self.gi, self.foc = src, gi, foc
        self.conv = None
        if src != "fo2" and not native:
            from port_stamp import Converter, Foc, TileMatcher
            self.focv = Foc()
            self.conv = Converter(self.focv, src, gi, True, "building_stamps")
            self._matcher = []
            self.TileMatcher = TileMatcher
            self.src_art = {t: [n.lower() for n in parse_lst(gi.vfs.read(f"art\\{d}\\{d}.lst"))]
                            for t, d in ((0, "items"), (2, "scenery"), (3, "walls"))}
            self.foc_art = {t: [n.lower() for n in parse_lst(foc.vfs.read(f"art\\{d}\\{d}.lst"))]
                            for t, d in ((0, "items"), (2, "scenery"), (3, "walls"))}
        self.dropped = 0

    def obj(self, o):
        if self.conv is None:
            return o.pid, o.fid
        # Same pid in FOC drawing byte-identical art: keep it (Fallout 1 and 2
        # share names for art that differs - Shady Sands' adobe is not
        # Arroyo's - so the name alone is not enough).
        t = pid_type(o.pid)
        same = self.foc.proto(o.pid)
        d = {0: "items", 2: "scenery", 3: "walls"}.get(t)
        if same is not None and d is not None:
            _, a = self.conv.src_art_bytes(d, o.fid & 0xFFF)
            if a is not None and a == self.focv.art_bytes(d, same.fid & 0xFFF):
                return o.pid, (o.fid & ~0xFFF) | (same.fid & 0xFFF)
        pid = self.conv.map_pid(o.pid)
        fid = self.conv.map_frm(o.pid, o.fid, pid) if pid else None
        if pid is None or fid is None:
            self.dropped += 1
            return None
        return pid, fid

    def tile(self, v: int) -> int:
        if self.conv is None:
            return v

        def matcher():
            if not self._matcher:
                self._matcher.append(self.TileMatcher(self.focv))
            return self._matcher[0]
        return self.conv.map_tile(v, matcher)


# who made each source's maps, stamped into every stamp as "credit"
CREDITS = {
    "fo1": "Fallout (1997), Interplay Productions / Black Isle Studios.",
    "fo2": "Fallout 2 (1998), Black Isle Studios / Interplay; maps as in the Restoration Project "
           "(killap), maintained as RPU by BGforge.",
    "sonora": "Fallout: Sonora, Alexander \"BLACK DESIGNER\" Poshelyuzhin and Alexander \"Red888guns\" "
              "Berezin, with Osman \"Wolfram\" Adzhiusmanov, Grigory Sokolov, Denis \"MASTER\" Voloshin, "
              "Dmitry Kuznetsov, Foxx and Lexx.",
    "nevada": "Fallout: Nevada, Alexander \"BLACK DESIGNER\" Poshelyuzhin and the Nevada team "
              "(Denis \"Master\" Voloshin, Anton \"SAMIRAKUS\" Smirnov, Osman \"Wolfram\" Ajiusmanov, "
              "Alexey Osadchiy, X'IL, Pixote, Mr.Stalin, Dmitriy Yermak, Pyran, Foxx).",
    "resurrection": "Fallout 1.5: Resurrection, the Resurrection team (resurrection.cz).",
    "olympus": "Olympus 2207, Nebesa Games: Artem \"Rainman\" Samoilov, Alexander \"Saur\" Berezin, "
               "Sergey \"Zoomer\" Bokarev, Elena Samoilova; English by keyboard gecko.",
}


NO_SIZE = False          # --no-size: construction / floor only
NO_FLOOR = False         # --no-floor: no floor level (the name still ends in the floor)


def write(sources, out: Path, native: bool = False, portable: bool = False) -> None:
    foc = GameIndex(pristine=False)
    foc_walls = parse_lst(foc.vfs.read("art\\walls\\walls.lst"))
    seen: set[str] = set()
    counts = Counter()
    manifest = []
    for src in sources:
        gi = GameIndex() if src == "fo2" else port.open_source(src)
        ids = Ids(src, gi, foc, native)
        # construction is judged on FOC's copy of the art, in every id mode,
        # so the shared native sets sort exactly like the checked library
        ids_cls = Ids(src, gi, foc, False) if native else ids
        wl = parse_lst(gi.vfs.read("art\\walls\\walls.lst"))
        sc = parse_lst(gi.vfs.read("art\\scenery\\scenery.lst"))
        it = parse_lst(gi.vfs.read("art\\items\\items.lst"))
        per_map = Counter()
        incomplete: list[str] = []

        for mp in list_maps(gi):
            try:
                raw = gi.vfs.read(mp)
                m = MapFile.parse(raw, gi.proto)
            except Exception:
                continue
            mname = mp.split("\\")[-1][:-4].upper()
            for ei in range(len(m.elevations)):
                for roof, keep, floor, rtiles, fams in extract(gi, m, ei, raw, wl, sc, it):
                    if not fams:
                        continue
                    folder = FOLDER_OF.get(fams.most_common(1)[0][0])
                    if folder is None:
                        continue
                    ax = min(x for x, _ in roof) * 2
                    ay = min(y for _, y in roof) * 2
                    objs = []
                    missing = 0          # any piece FOC has no art for: not a complete building
                    for o in keep:
                        r = ids.obj(o)
                        if r is None:
                            missing += 1
                            continue
                        hx, hy = hex_xy(o.tile)
                        flags = int.from_bytes(raw[o.off + OFF_FLAGS:o.off + OFF_FLAGS + 4], "big")
                        objs.append({"dxHex": hx - ax, "dyHex": hy - ay, "proPid": r[0], "frmPid": r[1],
                                     "direction": o.rotation % 6, "flags": flags})
                    if missing:
                        # a building with holes is worse than none
                        incomplete.append(f"{mname} ({missing} pieces)")
                        continue
                    # sort by the art the stamp really uses (FOC ids), not the
                    # source game's names: TCs name byte-identical walls differently
                    if native:
                        walls_foc = []
                        for o in keep:
                            if pid_type(o.pid) == 3:
                                r = ids_cls.obj(o)
                                if r is not None:
                                    walls_foc.append({"frmPid": r[1]})
                        folder = classify(walls_foc, foc_walls) or folder
                    else:
                        folder = classify(objs, foc_walls) or folder
                    folder = STAMP_FOLDER.get((GAME.get(src, src), mname, m.elevations[ei], ay * 200 + ax), folder)
                    if portable and not native and any(foc_only(o["proPid"], o["frmPid"]) for o in objs):
                        counts["foc_only"] += 1
                        continue
                    fl = [{"dxTile": q % 100 - ax // 2, "dyTile": q // 100 - ay // 2, "tileId": ids.tile(v)}
                          for q, v in sorted(floor.items())]
                    rf = [{"dxTile": q % 100 - ax // 2, "dyTile": q // 100 - ay // 2, "tileId": ids.tile(v)}
                          for q, v in sorted(rtiles.items()) if v > 1]
                    key = hashlib.sha1(json.dumps([sorted((o["dxHex"], o["dyHex"], o["proPid"], o["frmPid"]) for o in objs),
                                                   sorted((t["dxTile"], t["dyTile"], t["tileId"]) for t in fl)]).encode(), usedforsecurity=False).hexdigest()
                    if key in seen:
                        counts["duplicate"] += 1
                        continue
                    seen.add(key)
                    per_map[mname] += 1
                    game = GAME.get(src, src)
                    name = f"{game} {mname} {per_map[mname]}"
                    size = size_name(len(roof))
                    floor_name = FLOORS[m.elevations[ei]]
                    name = f"{name} {floor_name}"
                    d = out / folder
                    if not NO_SIZE:
                        d = d / size
                    if not NO_FLOOR:
                        d = d / floor_name
                    d.mkdir(parents=True, exist_ok=True)
                    anchor = ay * 200 + ax
                    stamp = {"name": name, "version": 1,
                             "variants": [{"label": "default", "anchorHex": anchor,
                                           "objects": objs, "floor": fl, "roof": rf}],
                             "credit": CREDITS.get(src, game),
                             "source": {"game": game, "map": mname, "elevation": m.elevations[ei],
                                        "roofSquares": len(roof),
                                        "ids": ("the game's own" if native or src == "fo2"
                                                else "Fallout 2's")}}
                    (d / f"{name}.json").write_text(json.dumps(stamp, separators=(",", ":")), encoding="utf-8")
                    counts[(folder, size, floor_name)] += 1
                    manifest.append((folder, size, floor_name, name, len(roof), len(objs)))
        print(f"{src}: {sum(per_map.values())} stamps; {len(incomplete)} buildings skipped as "
              f"incomplete (art FOC does not have)")
    print(f"duplicates skipped: {counts['duplicate']}; FOC-only ids skipped: {counts['foc_only']}; "
          f"not complete: {dict(REJECTS)}")
    REJECTS.clear()
    for (folder, size, fl_), n in sorted((k, v) for k, v in counts.items() if isinstance(k, tuple)):
        print(f"  {folder:20} {size:7} {fl_:10} {n}")
    lines = ["# Building stamps", "", "Cut from the original Fallouts' maps by tools/building_stamps.py.",
             ("Folders: construction" + ("" if NO_SIZE else " / size") + ("" if NO_FLOOR else " / floor") + ".")
             + " A 2nd or 3rd floor stamp goes on that elevation in gecko,",
             "over the building's 1st floor from the same map (same name, same number).", "",
             "| Construction | Size | Floor | Stamp | Roofed squares | Objects |", "|---|---|---|---|---|---|"]
    lines += [f"| {a} | {b} | {c} | {d} | {e} | {f} |" for a, b, c, d, e, f in sorted(manifest)]
    (out / "INDEX.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def survey(sources):
    tot = Counter()
    examples = defaultdict(list)
    for src in sources:
        gi = port.open_source(src)
        wl = parse_lst(gi.vfs.read("art\\walls\\walls.lst"))
        sc = parse_lst(gi.vfs.read("art\\scenery\\scenery.lst"))
        it = parse_lst(gi.vfs.read("art\\items\\items.lst"))
        n = 0
        for mp in list_maps(gi):
            try:
                raw = gi.vfs.read(mp)
                m = MapFile.parse(raw, gi.proto)
            except Exception:
                continue
            for ei in range(len(m.elevations)):
                for roof, keep, floor, rtiles, fams in extract(gi, m, ei, raw, wl, sc, it):
                    if not fams:
                        continue
                    top = fams.most_common(1)[0][0]
                    tot[top] += 1
                    n += 1
                    if len(examples[top]) < 3:
                        examples[top].append(f"{src}:{mp.split(chr(92))[-1]}")
        print(f"{src}: {n} buildings")
    for f, c in tot.most_common():
        print(f"{f:10} {c:4}  {examples[f]}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--survey", action="store_true")
    ap.add_argument("--sources", default="fo1,fo2")
    ap.add_argument("--out", type=Path)
    ap.add_argument("--separate", action="store_true", help="a folder per source: '<out> - <game>'")
    ap.add_argument("--native", action="store_true",
                    help="keep each game's own ids (for someone who opens that game in gecko)")
    ap.add_argument("--portable", action="store_true",
                    help="skip stamps that use ids only FOC has (for sharing the Fallout 1 & 2 set)")
    ap.add_argument("--no-size", action="store_true",
                    help="folders are construction / floor, without the size level")
    ap.add_argument("--no-floor", action="store_true",
                    help="no floor folders; the stamp name still ends in its floor")
    ap.add_argument("--layout", default="separate-desktop", choices=["separate-desktop", "library"],
                    help="library: <out>/<Game> Buildings/... (one tree, as gecko's patterns folder)")
    args = ap.parse_args()
    global NO_SIZE, NO_FLOOR
    NO_SIZE = args.no_size
    NO_FLOOR = args.no_floor
    if args.survey:
        survey(args.sources.split(","))
        return 0
    if args.out and args.layout == "library":
        for src in args.sources.split(","):
            write([src], args.out / f"{GAME.get(src, src)} Buildings", args.native, args.portable)
        return 0
    if args.out and args.separate:
        # one folder per source: <out> - <game>
        for src in args.sources.split(","):
            write([src], args.out.with_name(f"{args.out.name} - {GAME.get(src, src)}"), args.native, args.portable)
        return 0
    if args.out:
        write(args.sources.split(","), args.out, args.native, args.portable)
        return 0
    ap.error("--survey or --out")


if __name__ == "__main__":
    sys.exit(main())
