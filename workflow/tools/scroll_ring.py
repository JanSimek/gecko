#!/usr/bin/env python3
"""
scroll_ring.py - replace a map's scroll-blocker ring with one sized to the
map's actual content, so the camera can't be scrolled out past it into
undecorated wasteland or the void beyond.

    python tools/scroll_ring.py --dry-run
    python tools/scroll_ring.py --render build/x.png
    python tools/scroll_ring.py            # writes mod/data/maps/REDWELL.MAP

THE BUG THIS FIXES
    The engine's only camera-scroll limit besides a fixed ~7-hex margin from
    the true map edge (tile.cc::tileSetBorder - constant, not mapper-set) is
    an invisible "scroll blocker": misc proto 12 (PID 0x0500000C). tileSetCenter
    refuses to centre the camera on any hex holding one (object.cc::
    _obj_scroll_blocking_at). There is no other limit - a mapper places a
    ring of these around the playable area, or the camera scrolls freely
    across whatever is beyond it.

    REDWELL.MAP's ring (433 objects, a clean diamond outline - structurally
    identical to several vanilla DESERT*.MAP borders, most likely inherited
    from a stamp/fragment) is sized for a small vanilla encounter map. It
    sits well INSIDE Redwell's own town walls: floor tiles are painted out to
    hex (6,8)-(189,189), the town's buildings/walls span roughly
    (31,29)-(169,164), but the ring's bounding box is only (48,43)-(155,148).
    The camera scrolls right past it, out through the town wall, across the
    painted desert, and can be dragged almost to the map edge - "way past"
    the ring, exactly as reported. It isn't a hole in the ring; the ring is
    just drawn around the wrong, too-small area.

THE FIX
    Delete the old ring, draw a new one around the map's actual content
    (auto-measured from painted floor + placed objects, with a margin so nothing
    sits flush against it) as TWO concentric diamonds ~4 hexes apart. Each
    layer alone matches the vanilla shape/density (one hex-adjacency-connected
    loop; verified with the same connectivity check as strip_blockers.py's
    audit), and the second layer is redundancy: mapScroll steps the camera by
    32x24 screen px per ~33ms tick (map.cc::mapScroll), noticeably more than a
    single hex, and a single-file diagonal line is the known-fragile shape for
    that to slip past unnoticed. Two independent layers make that require
    missing twice in a row.

Round-trips through MapBuilder like strip_blockers.py / car_blockers.py:
object table, script table and tile layers rebuilt from the parse, so SIDs
stay consistent. A .bak is kept beside the map.
"""

from __future__ import annotations

import argparse
import collections
import shutil
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib.paths import inside  # noqa: E402
from foclib import (  # noqa: E402
    OBJ_TYPE_CRITTER,
    GameIndex,
    MapFile,
    hex_xy,
    pid_type,
)
from mapbuild import MapBuilder, MapObjectSpec, extras_for  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent

SCROLL_BLOCKER_PID = 0x0500000C   # misc 12 - the one PID the engine hard-codes
                                  # for scroll blocking (object.cc:2572)
SCROLL_BLOCKER_FID = 0x05000001  # proto 12's own .fid (read out of the base;
                                  # misc art indices don't mirror proto indices)


def neighbors(x: int, y: int) -> list[tuple[int, int]]:
    """The engine's 6 hex neighbours (tile.cc::_dir_tile), by column parity."""
    if x % 2 == 0:
        offs = [(-1, 0), (-1, 1), (0, 1), (1, 1), (1, 0), (0, -1)]
    else:
        offs = [(-1, -1), (-1, 0), (0, 1), (1, -1), (1, 0), (0, -1)]
    return [(x + dx, y + dy) for dx, dy in offs]


def walk(a: tuple[int, int], b: tuple[int, int]) -> list[tuple[int, int]]:
    """Greedy hex walk from a to b - every step a real hex-neighbour move, so
    the result is connected by construction."""
    cur = a
    path = [cur]
    guard = 0
    while cur != b and guard < 4000:
        guard += 1
        best, bestd = None, None
        for n in neighbors(*cur):
            d = (n[0] - b[0]) ** 2 + (n[1] - b[1]) ** 2
            if bestd is None or d < bestd:
                best, bestd = n, d
        if best == cur:
            break
        cur = best
        path.append(cur)
    return path


def diamond(n, e, s, w) -> set[tuple[int, int]]:
    pts: set[tuple[int, int]] = set()
    for a, b in [(n, e), (e, s), (s, w), (w, n)]:
        pts.update(walk(a, b))
    return pts


def check_connectivity(pts: set[tuple[int, int]]) -> list[int]:
    """Component sizes under hex adjacency. A correct single ring is 1 value."""
    adj = collections.defaultdict(list)
    for p in pts:
        for n in neighbors(*p):
            if n in pts:
                adj[p].append(n)
    seen, comps = set(), []
    for p in pts:
        if p in seen:
            continue
        stack, comp = [p], [p]
        seen.add(p)
        while stack:
            c = stack.pop()
            for n in adj[c]:
                if n not in seen:
                    seen.add(n)
                    stack.append(n)
                    comp.append(n)
        comps.append(len(comp))
    return sorted(comps, reverse=True)


def measure_object_extent(m: MapFile) -> tuple[int, int, int, int]:
    """(minx, maxx, miny, maxy) of every placed object - the actual town
    footprint (buildings, walls, scenery), not counting the scroll ring
    itself. This is what the new ring should hug plus a margin."""
    xs, ys = [], []
    for o in m.objects:
        if o.pid == SCROLL_BLOCKER_PID or not (0 <= o.tile < 40000):
            continue
        hx, hy = hex_xy(o.tile)
        xs.append(hx)
        ys.append(hy)
    return min(xs), max(xs), min(ys), max(ys)


def measure_floor_extent(m: MapFile) -> tuple[int, int, int, int]:
    """(minx, maxx, miny, maxy) of painted floor - the ring must stay inside
    this or the camera can scroll to a hex with no floor (the black void the
    painted desert exists to avoid)."""
    xs, ys = [], []
    for sq, tid in enumerate(m.floors[0]):
        if tid > 1:
            sx, sy = sq % 100, sq // 100
            xs += [sx * 2, sx * 2 + 1]
            ys += [sy * 2, sy * 2 + 1]
    return min(xs), max(xs), min(ys), max(ys)


def build_ring(cx: int, cy: int, half_w: int, half_h: int,
              gap: int) -> set[tuple[int, int]]:
    inner = diamond((cx, cy - half_h), (cx + half_w, cy),
                    (cx, cy + half_h), (cx - half_w, cy))
    outer = diamond((cx, cy - half_h - gap), (cx + half_w + gap, cy),
                    (cx, cy + half_h + gap), (cx - half_w - gap, cy))
    return inner | outer


def read_critter_extras(data: bytes, off: int):
    base = off + 72 + 12
    return struct.unpack_from(">10i", data, base + 4)


def rebuild(path: Path, m: MapFile, raw: bytes, gi: GameIndex,
            new_ring: set[tuple[int, int]]) -> bytes:
    mb = MapBuilder(path.name, elevations=tuple(m.elevations),
                    map_id=m.map_id if m.map_id != -1 else 200,
                    map_script_index=m.map_script_id)
    for e in m.elevations:
        i = m.elevations.index(e)
        mb.floors[e] = list(m.floors[i])
        mb.roofs[e] = list(m.roofs[i])
    mb.start_tile = m.default_tile
    mb.start_elevation = m.default_elevation
    mb.start_orientation = getattr(m, "default_orientation", 0)

    sid_map: dict[tuple[int, int], int] = {}
    for o in m.objects:
        if o.pid == SCROLL_BLOCKER_PID:
            continue          # dropping the old ring entirely
        if not (0 <= o.tile < 40000):
            continue
        pr = gi.proto(o.pid)
        if pr is None:
            continue
        t = pid_type(o.pid)
        extras = (read_critter_extras(raw, o.off) if t == OBJ_TYPE_CRITTER
                  else extras_for(t, o.pid, pr.subtype))

        sid = -1
        idx = o.script_index
        if idx not in (-1, 0xFFFFFFFF):
            stype = 4 if t == OBJ_TYPE_CRITTER else 3
            key = (stype, idx)
            if key not in sid_map:
                sid_map[key] = mb.add_script(stype, idx)
            sid = sid_map[key]

        mb.add(MapObjectSpec(
            pid=o.pid, fid=o.fid, tile=o.tile, elevation=o.elevation,
            rotation=o.rotation, frame=o.frame,
            sid=sid, script_index=idx if sid != -1 else -1, extras=extras))

    ground_elev = m.elevations[0]
    for (hx, hy) in sorted(new_ring):
        mb.add(MapObjectSpec(
            pid=SCROLL_BLOCKER_PID, fid=SCROLL_BLOCKER_FID, tile=hy * 200 + hx,
            elevation=ground_elev, rotation=0, frame=0,
            sid=-1, script_index=-1, extras=()))

    return mb.to_bytes()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--map", default="mod/data/maps/REDWELL.MAP")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--render", metavar="PNG",
                    help="write a full-map render with old (red) and new "
                         "(green) rings marked, and stop")
    ap.add_argument("--margin", type=int, default=10,
                    help="hexes of buffer between the map's measured content "
                         "and the inner ring layer")
    ap.add_argument("--gap", type=int, default=4,
                    help="hexes between the inner and outer ring layers")
    args = ap.parse_args()

    gi = GameIndex()
    path = inside(ROOT / args.map)
    if not path.is_file():
        sys.exit(f"map not found: {path}")
    raw = path.read_bytes()
    m = MapFile.parse(raw, gi.proto)

    old_ring = {hex_xy(o.tile) for o in m.objects
               if o.pid == SCROLL_BLOCKER_PID and 0 <= o.tile < 40000}
    ox0, ox1, oy0, oy1 = measure_object_extent(m)
    fx0, fx1, fy0, fy1 = measure_floor_extent(m)
    print(f"{path.name}: object footprint x[{ox0},{ox1}] y[{oy0},{oy1}]; "
          f"painted floor x[{fx0},{fx1}] y[{fy0},{fy1}]")
    if old_ring:
        print(f"  old ring: {len(old_ring)} tiles, bbox "
              f"x[{min(p[0] for p in old_ring)},{max(p[0] for p in old_ring)}] "
              f"y[{min(p[1] for p in old_ring)},{max(p[1] for p in old_ring)}]")
    else:
        print("  no existing scroll-blocker ring")

    cx, cy = (ox0 + ox1) // 2, (oy0 + oy1) // 2
    half_w = (ox1 - ox0) // 2 + args.margin
    half_h = (oy1 - oy0) // 2 + args.margin
    # never let the OUTER layer (half+gap) reach past the painted floor, or
    # past a safe distance from the true 0/199 grid edge (tile.cc's own hard
    # border sits ~6-13 hexes in, but that varies with window size - stay
    # well clear of it too).
    max_half_w = min(cx - fx0, fx1 - cx, cx - 12, 187 - cx) - args.gap
    max_half_h = min(cy - fy0, fy1 - cy, cy - 12, 187 - cy) - args.gap
    if half_w > max_half_w:
        print(f"  ! half_w {half_w} clamped to {max_half_w} (floor/border limit)")
        half_w = max_half_w
    if half_h > max_half_h:
        print(f"  ! half_h {half_h} clamped to {max_half_h} (floor/border limit)")
        half_h = max_half_h
    new_ring = build_ring(cx, cy, half_w, half_h, args.gap)

    comps = check_connectivity(new_ring)
    nx0 = min(p[0] for p in new_ring); nx1 = max(p[0] for p in new_ring)
    ny0 = min(p[1] for p in new_ring); ny1 = max(p[1] for p in new_ring)
    print(f"  new ring: {len(new_ring)} tiles, bbox x[{nx0},{nx1}] y[{ny0},{ny1}], "
          f"components={comps[:4]}{'...' if len(comps) > 4 else ''}")
    if len(comps) != 2:
        print(f"  ! expected exactly 2 connected loops (inner+outer), got "
              f"{len(comps)} - check --margin/--gap before trusting this")

    if args.render:
        try:
            import importlib.util
            from PIL import ImageDraw
        except ImportError:
            sys.exit("Pillow not installed - cannot --render")
        spec = importlib.util.spec_from_file_location(
            "focrender", str(Path(__file__).resolve().parent / "render.py"))
        rmod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(rmod)
        img = rmod.render_map(gi, m, 0).convert("RGB")
        floors = m.floors[0]
        pts = []
        for sq, tid in enumerate(floors):
            if tid > 1:
                x, y = rmod.square_xy(sq)
                pts.append((x, y))
        for o in m.objects:
            if o.elevation == m.elevations[0] and 0 <= o.tile < 40000:
                x, y = rmod.hex_xy_screen(o.tile)
                pts += [(x - 100, y - 200), (x + 100, y + 60)]
        minx = min(p[0] for p in pts)
        miny = min(p[1] for p in pts)
        dr = ImageDraw.Draw(img)
        for p in old_ring:
            x, y = rmod.hex_xy_screen(p[1] * 200 + p[0])
            cx2, cy2 = x + 16 - minx, y + 8 - miny
            dr.ellipse([cx2 - 3, cy2 - 3, cx2 + 3, cy2 + 3], fill=(255, 60, 60))
        for p in new_ring:
            x, y = rmod.hex_xy_screen(p[1] * 200 + p[0])
            cx2, cy2 = x + 16 - minx, y + 8 - miny
            dr.ellipse([cx2 - 3, cy2 - 3, cx2 + 3, cy2 + 3], fill=(60, 255, 90))
        out = inside(ROOT / args.render)
        out.parent.mkdir(parents=True, exist_ok=True)
        scale = 4 if max(img.size) > 3000 else 1
        if scale > 1:
            img = img.resize((img.width // scale, img.height // scale))
        img.save(out)
        print(f"  wrote {out} (old ring red, new ring green)")
        return 0

    if args.dry_run:
        print("  dry run - nothing written")
        return 0

    out_bytes = rebuild(path, m, raw, gi, new_ring)
    check = MapFile.parse(out_bytes, gi.proto)
    new_count = sum(1 for o in check.objects if o.pid == SCROLL_BLOCKER_PID)
    scripted = sum(1 for o in check.objects
                   if o.script_index not in (-1, 0xFFFFFFFF))

    backup = path.with_suffix(path.suffix + ".bak")
    shutil.copy2(path, backup)
    path.write_bytes(out_bytes)
    print(f"  wrote {path.name}: {len(check.objects)} objects "
          f"({len(old_ring)} old ring tiles removed, {new_count} new ones added)")
    print(f"  objects still carrying a script: {scripted}")
    print(f"  backup: {backup.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
