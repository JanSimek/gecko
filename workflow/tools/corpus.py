#!/usr/bin/env python3
"""
corpus.py - build a searchable library of human-composed map fragments.

    python tools/corpus.py index [--source NAME] [--limit N]
    python tools/corpus.py stats
    python tools/corpus.py search [--kind interior] [--tileset cav] [--has door]
                                  [--min-objects 8] [--size 8x8..24x24] [--limit N]
    python tools/corpus.py show <fragment-id>
    python tools/corpus.py resolve <fragment-id>     what it costs to place here

WHY FRAGMENTS AND NOT RULES
---------------------------
Generating maps from rules produces geometry that is correct and dead - see
MDFG's authored_walls_render.png next to its donor-based lower_deck.png. The
composition is the part a person did, and it is the part that does not come
back from statistics. So instead of learning to compose, lift the composition:
cut real rooms out of real maps, tiles and objects together, and keep them as
placeable units.

Base evidence for the approach: human maps use a median of 91 distinct floor
tiles each (min 2, max 362), layered as a dominant ground tile plus accents
plus a whole grammar of `edg*` transition tiles at the seams. MDFG's generated
floor used exactly one. That gap does not close by picking a better rule.

EVERYTHING IS STORED BY NAME
----------------------------
Tile ids, proto PIDs and art indices all mean different things in different
installs - that is the entire reason port.py exists. A fragment lifted from
Sonora and stored as "scenery index 1417" is worthless here. So fragments
record ART AND PROTO FILENAMES, resolved through the source install's own
.lst files at extraction time. Placing one later means resolving those names
against OUR lists and allocating what is missing.

Source installs are READ-ONLY.
"""

from __future__ import annotations

import argparse
import collections
import json
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib import (  # noqa: E402
    OBJ_TYPE_CRITTER,
    OBJ_TYPE_ITEM,
    OBJ_TYPE_MISC,
    OBJ_TYPE_SCENERY,
    OBJ_TYPE_WALL,
    GameIndex,
    TYPE_NAME,
    hex_xy,
    is_exit_grid,
    parse_lst,
    pid_type,
)
from port import load_sources  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
CORPUS = ROOT / "corpus"
FRAGMENTS = CORPUS / "fragments.jsonl"
MAPSTATS = CORPUS / "maps.jsonl"

SQ_W = 100
HEX_W = 200

# Fragment size limits, in squares. Below the minimum it is a cupboard, not a
# room; above the maximum it is a whole district and not placeable as a unit.
MIN_SQUARES = 12
MAX_SQUARES = 900
MIN_SIDE = 3
MAX_SIDE = 40

# Components bigger than MAX_SQUARES are not rooms - they are whole exteriors,
# city blocks, or (in Olympus, where floor runs continuously under the walls)
# an entire map as one component. Discarding them threw away every exterior in
# the corpus, so instead they get tiled into fixed windows: still real human
# composition, just cut to a placeable size rather than a semantic one.
WINDOW = 14
WINDOW_MIN_FLOOR = 40
# A window of bare ground is not composition, it is texture. 83% of windows came
# back with fewer than 5 objects on the first pass and drowned the real material,
# so a window has to carry actual content to earn a place in the library. Rooms
# are exempt: a deliberately empty room is still an authored room.
WINDOW_MIN_OBJECTS = 8


# --------------------------------------------------------------------- helpers

class SourceView:
    """A source install plus the name lookups needed to make fragments portable."""

    def __init__(self, name: str, gi: GameIndex):
        self.name = name
        self.gi = gi
        self._tiles = None
        self._art: dict[str, list[str]] = {}

    @property
    def tiles(self) -> list[str]:
        if self._tiles is None:
            try:
                self._tiles = parse_lst(self.gi.vfs.read("art\\tiles\\tiles.lst"))
            except FileNotFoundError:
                self._tiles = []
        return self._tiles

    def tile_name(self, tid: int) -> str | None:
        """
        Art lists are 0-BASED. art.cc::artReadList builds a flat array with
        no header line, indexed directly by `fid & 0xFFF`, and index 0 holds
        reserved.frm. Proto lists are 1-BASED (proto.cc::_proto_list_str
        counts from i=1). Two conventions, one apart - see CLAUDE.md 4f.

        Tile id 1 is the engine's "no tile" sentinel.
        """
        if tid <= 1:
            return None
        return self.tiles[tid] if tid < len(self.tiles) and self.tiles[tid] else None

    def art_name(self, obj_type: int, fid: int) -> str | None:
        d = TYPE_NAME.get(obj_type)
        if d is None:
            return None
        if d not in self._art:
            try:
                self._art[d] = parse_lst(self.gi.vfs.read(f"art\\{d}\\{d}.lst"))
            except FileNotFoundError:
                self._art[d] = []
        lst = self._art[d]
        i = fid & 0xFFF
        return lst[i] if 0 <= i < len(lst) and lst[i] else None

    def proto_file(self, pid: int) -> str | None:
        return self.gi.proto_filename(pid)


def tileset_of(names: collections.Counter) -> str:
    """
    Crude but effective family key: Fallout's tile art is named by kit -
    cav*, bld*, edg*, dirt*, mtn*, flr*. The dominant non-edge prefix names
    the kit, because edg* tiles are transitions and appear in nearly every map.
    """
    best, best_n = "?", 0
    for n, c in names.items():
        m = re.match(r"[A-Za-z]+", n)
        if not m:
            continue
        p = m.group(0).lower()
        if p.startswith("edg"):        # transition tiles, present in nearly every map
            continue
        if c > best_n:
            best, best_n = p, c
    return best


# ------------------------------------------------------------------ segmenting

def segment(m, elev_i: int, wall_squares: set[int]) -> list[list[int]]:
    """
    Connected components of floored squares, with walls acting as barriers.

    4-connected on the square grid. A square holding a wall object is not
    traversable, so a walled room comes out as its own component. Doorways do
    leak - an interior with open doors becomes one component covering the whole
    building - which is fine: a building interior is a perfectly good unit.
    """
    floors = m.floors[elev_i]
    seen = bytearray(10000)
    out = []
    for start in range(10000):
        if seen[start] or floors[start] <= 1 or start in wall_squares:
            continue
        stack = [start]
        seen[start] = 1
        comp = []
        while stack:
            sq = stack.pop()
            comp.append(sq)
            x, y = sq % SQ_W, sq // SQ_W
            for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if not (0 <= nx < SQ_W and 0 <= ny < SQ_W):
                    continue
                n = ny * SQ_W + nx
                if seen[n] or floors[n] <= 1 or n in wall_squares:
                    continue
                seen[n] = 1
                stack.append(n)
        out.append(comp)
    return out


# ------------------------------------------------------------------- extraction

def extract_map(sv: SourceView, game_path: str, frag_out, map_out) -> int:
    gi = sv.gi
    try:
        data = gi.vfs.read(game_path)
        m = gi.load_map(data)
    except Exception as e:  # noqa: BLE001
        map_out.write(json.dumps({"source": sv.name, "map": game_path,
                                  "error": f"{type(e).__name__}: {e}"}) + "\n")
        return 0

    # objects by elevation, and the wall mask
    by_elev: dict[int, list] = collections.defaultdict(list)
    stack = list(m.objects)
    while stack:
        o = stack.pop()
        stack.extend(o.inventory)
        if 0 <= o.tile < 40000:
            by_elev[o.elevation].append(o)

    tile_names_all = collections.Counter()
    made = 0

    for elev_i, elev in enumerate(m.elevations):
        floors = m.floors[elev_i]
        objs = by_elev.get(elev, [])

        wall_sq = set()
        for o in objs:
            if pid_type(o.pid) == OBJ_TYPE_WALL:
                hx, hy = hex_xy(o.tile)
                wall_sq.add((hy // 2) * SQ_W + (hx // 2))

        names = collections.Counter()
        for f in floors:
            n = sv.tile_name(f)
            if n:
                names[n] += 1
        tile_names_all.update(names)

        obj_by_sq: dict[int, list] = collections.defaultdict(list)
        for o in objs:
            hx, hy = hex_xy(o.tile)
            obj_by_sq[(hy // 2) * SQ_W + (hx // 2)].append(o)

        regions = []          # (gx0, gy0, gx1, gy1, comp_squares, origin)
        for comp in segment(m, elev_i, wall_sq):
            xs = [c % SQ_W for c in comp]
            ys = [c // SQ_W for c in comp]
            x0, x1 = min(xs), max(xs)
            y0, y1 = min(ys), max(ys)

            if MIN_SQUARES <= len(comp) <= MAX_SQUARES:
                w, h = x1 - x0 + 1, y1 - y0 + 1
                if not (MIN_SIDE <= w <= MAX_SIDE and MIN_SIDE <= h <= MAX_SIDE):
                    continue
                # one square of margin, so wall lines on the boundary come too
                regions.append((max(0, x0 - 1), max(0, y0 - 1),
                                min(SQ_W - 1, x1 + 1), min(SQ_W - 1, y1 + 1),
                                len(comp), "room"))
            elif len(comp) > MAX_SQUARES:
                inside = set(comp)
                for wy in range(y0, y1 + 1, WINDOW):
                    for wx in range(x0, x1 + 1, WINDOW):
                        ex, ey = min(SQ_W - 1, wx + WINDOW - 1), min(SQ_W - 1, wy + WINDOW - 1)
                        n = sum(1 for yy in range(wy, ey + 1)
                                for xx in range(wx, ex + 1)
                                if yy * SQ_W + xx in inside)
                        if n >= WINDOW_MIN_FLOOR:
                            regions.append((wx, wy, ex, ey, n, "window"))

        for gx0, gy0, gx1, gy1, comp_n, origin_kind in regions:
            gw, gh = gx1 - gx0 + 1, gy1 - gy0 + 1

            palette: list[str] = []
            pal_index: dict[str, int] = {}

            def pack(grid_ids):
                cells = []
                for yy in range(gy0, gy1 + 1):
                    row = []
                    for xx in range(gx0, gx1 + 1):
                        n = sv.tile_name(grid_ids[yy * SQ_W + xx])
                        if n is None:
                            row.append(-1)
                            continue
                        if n not in pal_index:
                            pal_index[n] = len(palette)
                            palette.append(n)
                        row.append(pal_index[n])
                    cells.append(row)
                return cells

            floor_cells = pack(floors)
            roof_cells = pack(m.roofs[elev_i])

            frag_objs = []
            counts = collections.Counter()
            has = set()
            for yy in range(gy0, gy1 + 1):
                for xx in range(gx0, gx1 + 1):
                    for o in obj_by_sq.get(yy * SQ_W + xx, ()):
                        t = pid_type(o.pid)
                        hx, hy = hex_xy(o.tile)
                        rec = {
                            "t": t,
                            "proto": sv.proto_file(o.pid),
                            "art": sv.art_name(t, o.fid),
                            # hex offset from the fragment's top-left square
                            "dx": hx - gx0 * 2,
                            "dy": hy - gy0 * 2,
                            "rot": o.rotation,
                            "frm": o.frame,
                        }
                        if o.script_index >= 0:
                            rec["script"] = gi.script_name(o.script_index + 1)
                        if is_exit_grid(o.pid):
                            has.add("exit_grid")
                        frag_objs.append(rec)
                        counts[TYPE_NAME.get(t, str(t))] += 1

            local_tiles = collections.Counter()
            roofed = floored = 0
            for yy in range(gy0, gy1 + 1):
                for xx in range(gx0, gx1 + 1):
                    sq = yy * SQ_W + xx
                    n = sv.tile_name(floors[sq])
                    if n:
                        local_tiles[n] += 1
                        floored += 1
                        if sv.tile_name(m.roofs[elev_i][sq]):
                            roofed += 1
            kit = tileset_of(local_tiles)

            # Enclosure, measured against the perimeter rather than a flat
            # count: walls sit on the hex grid, so a fully walled rectangle of
            # w x h squares carries roughly 4*(w+h) wall objects. Half that is a
            # generous bar for "this space is enclosed".
            #
            # Roof coverage is stored too but deliberately NOT used for this. In
            # Fallout 2 a roof marks a building you walk UNDER on an exterior
            # map; a pure interior map usually has no roof layer at all, because
            # you are already inside. It answers a different question.
            perim = 2 * (gw + gh)
            wall_ratio = counts.get("walls", 0) / perim if perim else 0.0
            interior = wall_ratio >= 0.75
            if counts.get("critters"):
                has.add("critters")
            if counts.get("items"):
                has.add("items")
            for rec in frag_objs:
                a = (rec["art"] or "").lower()
                if "door" in a:
                    has.add("door")
                if "stair" in a:
                    has.add("stairs")
                if "ladder" in a:
                    has.add("ladder")
                if rec.get("script"):
                    has.add("scripted")

            if origin_kind == "window" and len(frag_objs) < WINDOW_MIN_OBJECTS:
                continue

            fid = f"{sv.name}:{Path(game_path).stem.lower()}:e{elev}:{gx0}_{gy0}"
            frag = {
                "id": fid,
                "source": sv.name,
                "map": game_path,
                "elevation": elev,
                "origin_sq": [gx0, gy0],
                "size_sq": [gw, gh],
                "floor_squares": comp_n,
                "cut": origin_kind,
                "roofed_fraction": round(roofed / floored, 2) if floored else 0.0,
                "wall_ratio": round(wall_ratio, 2),
                "kind": "interior" if interior else "exterior",
                "tileset": kit,
                "tags": sorted(has),
                "objects_by_type": dict(counts),
                "object_count": len(frag_objs),
                "palette": palette,
                "floor": floor_cells,
                "roof": roof_cells,
                "objects": frag_objs,
            }
            frag_out.write(json.dumps(frag, separators=(",", ":")) + "\n")
            made += 1

    map_out.write(json.dumps({
        "source": sv.name,
        "map": game_path,
        "version": m.version,
        "map_id": m.map_id,
        "elevations": m.elevations,
        "objects": len(m.objects),
        "map_scripts": len(m.script_pids),
        "distinct_floor_tiles": len(tile_names_all),
        "top_tiles": [n for n, _ in tile_names_all.most_common(8)],
        "fragments": made,
    }) + "\n")
    return made


# ------------------------------------------------------------------- commands

def cmd_index(args):
    CORPUS.mkdir(parents=True, exist_ok=True)
    srcs = load_sources()
    if args.source:
        if args.source not in srcs:
            sys.exit(f"unknown source {args.source}")
        srcs = {args.source: srcs[args.source]}

    t0 = time.time()
    total_frags = total_maps = 0
    with FRAGMENTS.open("w", encoding="utf-8") as fo, \
         MAPSTATS.open("w", encoding="utf-8") as mo:
        for name, meta in sorted(srcs.items()):
            path = Path(meta["path"])
            if not path.is_dir():
                print(f"  skip {name}: {path} missing")
                continue
            try:
                sv = SourceView(name, GameIndex(path, pristine=False))
            except FileNotFoundError as e:
                print(f"  skip {name}: {e}")
                continue

            maps = sorted({n for prov in sv.gi.vfs.providers if prov.kind == "dat"
                           for n in prov.dat.names() if n.lower().endswith(".map")},
                          key=str.lower)
            for prov in sv.gi.vfs.providers:
                if prov.kind == "dir":
                    for p in prov.root.rglob("*"):
                        if p.is_file() and p.suffix.lower() == ".map":
                            maps.append(str(p.relative_to(prov.root)))
            # Archives disagree about filename case (maps\BROKEN2.map and
            # maps\broken2.map are the same file), which would index and then
            # duplicate every fragment. Keep one spelling per lowercased name.
            uniq = {}
            for gp in maps:
                uniq.setdefault(gp.lower(), gp)
            maps = sorted(uniq.values(), key=str.lower)
            if args.limit:
                maps = maps[:args.limit]

            n = 0
            for gp in maps:
                n += extract_map(sv, gp, fo, mo)
                total_maps += 1
            total_frags += n
            print(f"  {name:16s} {len(maps):5d} maps -> {n:6d} fragments")

    print(f"\n{total_frags} fragments from {total_maps} maps in {time.time()-t0:.1f}s")
    print(f"  {FRAGMENTS.relative_to(ROOT)}  "
          f"({FRAGMENTS.stat().st_size/1048576:.1f} MB)")
    return 0


def load_fragments():
    if not FRAGMENTS.is_file():
        sys.exit("no corpus yet - run: python tools/corpus.py index")
    with FRAGMENTS.open(encoding="utf-8") as f:
        for line in f:
            yield json.loads(line)


def cmd_stats(args):
    kinds = collections.Counter()
    kits = collections.Counter()
    tags = collections.Counter()
    sources = collections.Counter()
    sizes = []
    objs = []
    for fr in load_fragments():
        kinds[fr["kind"]] += 1
        kits[fr["tileset"]] += 1
        sources[fr["source"]] += 1
        for t in fr["tags"]:
            tags[t] += 1
        sizes.append(fr["size_sq"][0] * fr["size_sq"][1])
        objs.append(fr["object_count"])
    n = len(sizes)
    if not n:
        print("corpus is empty")
        return 1
    sizes.sort(); objs.sort()
    print(f"fragments: {n}")
    print(f"  by kind    : {dict(kinds)}")
    print(f"  by source  : {dict(sources.most_common())}")
    print(f"  area (sq)  : min {sizes[0]}, median {sizes[n//2]}, max {sizes[-1]}")
    print(f"  objects    : min {objs[0]}, median {objs[n//2]}, max {objs[-1]}")
    print(f"  top kits   : {dict(kits.most_common(12))}")
    print(f"  tags       : {dict(tags.most_common())}")
    return 0


def cmd_search(args):
    lo = hi = None
    if args.size:
        a, _, b = args.size.partition("..")
        def area(s):
            w, _, h = s.partition("x")
            return int(w) * int(h)
        lo = area(a)
        hi = area(b) if b else None

    hits = []
    for fr in load_fragments():
        if args.kind and fr["kind"] != args.kind:
            continue
        if args.source and fr["source"] != args.source:
            continue
        if args.tileset and fr["tileset"] != args.tileset:
            continue
        if args.min_objects and fr["object_count"] < args.min_objects:
            continue
        if args.has and not set(args.has).issubset(set(fr["tags"])):
            continue
        a = fr["size_sq"][0] * fr["size_sq"][1]
        if lo is not None and a < lo:
            continue
        if hi is not None and a > hi:
            continue
        if args.art:
            arts = [(o["art"] or "").lower() for o in fr["objects"]]
            matched = {}
            for term in args.art:
                found = sorted({a for a in arts if term.lower() in a})
                if not found:
                    matched = None
                    break
                matched[term] = found
            if matched is None:
                continue
            fr["_matched_art"] = matched
        hits.append(fr)

    hits.sort(key=lambda f: -f["object_count"])
    for fr in hits[:args.limit]:
        w, h = fr["size_sq"]
        print(f"  {fr['id']:<46s} {w:2d}x{h:<2d} {fr['kind']:8s} "
              f"{fr['tileset']:6s} obj={fr['object_count']:<4d} "
              f"{','.join(fr['tags'])}")
        if args.show_art and fr.get("_matched_art"):
            for term, found in fr["_matched_art"].items():
                print(f"        {term}: {', '.join(found[:8])}"
                      f"{f' (+{len(found)-8})' if len(found) > 8 else ''}")
    print(f"({len(hits)} match, showing {min(len(hits), args.limit)})")
    return 0


def cmd_show(args):
    for fr in load_fragments():
        if fr["id"] != args.id:
            continue
        w, h = fr["size_sq"]
        print(f"{fr['id']}")
        print(f"  from      {fr['source']} {fr['map']} elevation {fr['elevation']}")
        print(f"  size      {w}x{h} squares ({fr['floor_squares']} floored)")
        print(f"  kind      {fr['kind']}, tileset {fr['tileset']}")
        print(f"  tags      {', '.join(fr['tags']) or '-'}")
        print(f"  objects   {fr['objects_by_type']}")
        print(f"  palette   {len(fr['palette'])} distinct tiles")
        print(f"            {', '.join(fr['palette'][:12])}"
              f"{' ...' if len(fr['palette']) > 12 else ''}")
        print("\n  floor map (palette index, '.' = no tile):")
        for row in fr["floor"]:
            print("    " + "".join("." if c < 0 else "0123456789abcdefghijklmnopqrstuvwxyz"[c % 36]
                                   for c in row))
        print("\n  objects:")
        for o in fr["objects"][:40]:
            print(f"    {TYPE_NAME.get(o['t'],'?'):9s} {str(o['art']):<16s} "
                  f"proto={o['proto']} at hex+({o['dx']},{o['dy']})"
                  + (f" script={o['script']}" if o.get("script") else ""))
        if len(fr["objects"]) > 40:
            print(f"    ... and {len(fr['objects']) - 40} more")
        return 0
    sys.exit(f"no fragment with id {args.id}")


# ------------------------------------------------------------------- resolving

def cmd_resolve(args):
    """
    What would placing this fragment cost?

    Names that already exist in our base are free. The rest have to be ported
    and allocated before the fragment can be laid down.

    NOTE ON PROTOS: a proto's FILENAME is "00001417.pro" - the index wearing an
    extension. It collides across installs while naming a completely different
    object, so it is never a valid identity test. Protos are matched here by
    (type, art name), which is the part that actually means something.
    """
    from foclib import parse_lst as _pl

    gi = GameIndex()
    tiles = {n.lower() for n in _pl(gi.vfs.read("art/tiles/tiles.lst")) if n}
    art_by_dir = {}
    for d in TYPE_NAME.values():
        try:
            art_by_dir[d] = _pl(gi.vfs.read(f"art\\{d}\\{d}.lst"))
        except FileNotFoundError:
            art_by_dir[d] = []
    art_sets = {d: {n.lower() for n in lst if n} for d, lst in art_by_dir.items()}

    proto_art = {}
    for ot, d in TYPE_NAME.items():
        names = set()
        al = art_by_dir.get(d, [])
        for i, fn in enumerate(gi.proto_lst(ot)):
            if not fn:
                continue
            pr = gi.proto((ot << 24) | (i + 1))
            if pr is None:
                continue
            idx = pr.fid & 0xFFF
            if 0 <= idx < len(al) and al[idx]:
                names.add(al[idx].lower())
        proto_art[ot] = names

    for fr in load_fragments():
        if fr["id"] != args.id:
            continue
        miss_tiles = sorted({n for n in fr["palette"] if n.lower() not in tiles})
        miss_art, miss_proto = set(), set()
        for o in fr["objects"]:
            d = TYPE_NAME.get(o["t"])
            a = (o["art"] or "").lower()
            if not a:
                continue
            if d and a not in art_sets.get(d, ()):
                miss_art.add(f"{d}/{o['art']}")
            if a not in proto_art.get(o["t"], ()):
                miss_proto.add(f"{d}/{o['art']}")

        print(f"{fr['id']}  ({fr['source']} {fr['map']})")
        print(f"  {len(fr['palette'])} tiles, {fr['object_count']} objects")
        print(f"  tiles to port  : {len(miss_tiles)}")
        for n in miss_tiles[:12]:
            print(f"      {n}")
        if len(miss_tiles) > 12:
            print(f"      ... and {len(miss_tiles)-12} more")
        print(f"  art to port    : {len(miss_art)}")
        for n in sorted(miss_art)[:12]:
            print(f"      {n}")
        if len(miss_art) > 12:
            print(f"      ... and {len(miss_art)-12} more")
        print(f"  protos to port : {len(miss_proto)}")
        if not (miss_tiles or miss_art or miss_proto):
            print("\n  Nothing to port - every name already exists in our base.")
        return 0
    sys.exit(f"no fragment with id {args.id}")


def main() -> int:
    ap = argparse.ArgumentParser(description="human-composed map fragment library")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("index"); p.set_defaults(fn=cmd_index)
    p.add_argument("--source"); p.add_argument("--limit", type=int)

    sub.add_parser("stats").set_defaults(fn=cmd_stats)

    p = sub.add_parser("search"); p.set_defaults(fn=cmd_search)
    p.add_argument("--kind", choices=["interior", "exterior"])
    p.add_argument("--source"); p.add_argument("--tileset")
    p.add_argument("--has", nargs="*", default=[])
    p.add_argument("--art", nargs="*", default=[],
                   help="only fragments containing objects whose art name "
                        "matches ALL of these substrings, e.g. --art brahmin")
    p.add_argument("--show-art", action="store_true",
                   help="list the matching art names in each hit")
    p.add_argument("--min-objects", type=int)
    p.add_argument("--size", help="e.g. 8x8..24x24")
    p.add_argument("--limit", type=int, default=30)

    p = sub.add_parser("show"); p.set_defaults(fn=cmd_show)
    p.add_argument("id")

    p = sub.add_parser("resolve"); p.set_defaults(fn=cmd_resolve)
    p.add_argument("id")

    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    raise SystemExit(main())
