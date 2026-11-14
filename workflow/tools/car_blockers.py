#!/usr/bin/env python3
"""
car_blockers.py - fill the footprint of car / caravan-cart scenery with
invisible blocking hexes, so the sprite reads as solid instead of something
you walk through.

    python tools/car_blockers.py --dry-run
    python tools/car_blockers.py --render build/redwell_blockers.png
    python tools/car_blockers.py --map mod/data/maps/REDWELL.MAP

A car or cart is a single scenery object that blocks exactly one hex - its
anchor - while the sprite covers a 3x5-ish patch. Fallout's fix, used by the
shipped maps, is scenery proto 67 "Secret Blocking Hex": an invisible,
one-hex, movement-blocking object with no other behaviour. This tool works
out which hexes a vehicle sprite covers and drops a 67 on each.

HOW THE FOOTPRINT IS DERIVED
    Not from the shipped maps - 155 of them were measured and vanilla mappers
    blockered cars wildly inconsistently (many not at all, many placed flush
    against a building that already blocks). Instead it is geometric: the
    vehicle's FRM gives a sprite rectangle in screen space
    (object.cc::_obj_render_object), that rectangle is trimmed to the chassis
    (drop the roof overhang you can see over but not walk through, inset the
    sides), and every hex whose screen anchor lands inside the trimmed
    rectangle gets a blocker.

    The trim factors are tunable (--top-skip, --inset-x, --bottom-trim) and
    the whole thing is meant to be eyeballed on --render output and corrected
    per-proto in FOOTPRINT_OVERRIDE. Spatial judgement is the owner's.

Round-trips through MapBuilder, exactly like strip_blockers.py: object table,
script table and tile layers are rebuilt from the parse, so SIDs stay
consistent. A .bak is kept beside the map.
"""

from __future__ import annotations

import argparse
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
    TYPE_NAME,
    hex_xy,
    pid_type,
)
from foclib.formats import parse_lst  # noqa: E402
from mapbuild import MapBuilder, MapObjectSpec, extras_for  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent

BLOCKING_HEX_PID = (2 << 24) | 67          # scenery 67, "Secret Blocking Hex"
BLOCKING_HEX_FID = (2 << 24) | 0x15        # art/scenery/block.frm (list line 22)

# Art-name prefixes that count as a drive-through vehicle. carvsign is a
# signpost, not a vehicle; the *b variants (car1b..car5b) are burnt husks and
# share the base footprint.
VEHICLE_PREFIXES = ("car", "truck", "cart", "shorcar", "ccart", "browreck",
                    "carspec")
NOT_VEHICLES = {"carvsign", "cargo", "carpet"}

# Per-proto footprint tweaks, applied on top of the geometric result.
#   "skip"   - never blocker this art (e.g. a decorative fragment)
#   "nudge"  - (dx, dy) hex offsets to slide the whole computed footprint by
#   "add"    - extra (dx, dy) hex offsets from the anchor, always blockered
#   "drop"   - (dx, dy) offsets to remove from the computed result
# dx is +east, dy is +south in hex-tile coordinates. Verify on
#   python tools/car_blockers.py --only <art> --render build/x.png
FOOTPRINT_OVERRIDE: dict[str, dict] = {
    # The caravan carts are drawn with a large left/up art offset and the
    # geometric slab lands low and to the right of the body. Slide it back.
    "ccart01": {"nudge": (-1, -1)},
    "ccart02": {"nudge": (-1, -1)},
    "ccart03": {"nudge": (0, -1)},
    "ccart05": {"nudge": (-3, 0)},
    "ccart06": {"nudge": (-1, -1)},
}


# --------------------------------------------------------------- screen geometry
# object.cc::_obj_render_object, mirrored from tools/render.py.

def hex_screen(tile: int) -> tuple[int, int]:
    v3 = 199 - (tile % 200)
    v4 = tile // 200
    x = 48 * (v3 // 2) + 16 * v4
    y = 12 * -(v3 // 2) + 12 * v4
    if v3 & 1:
        x += 32
    return x, y


def frm_geometry(data: bytes, rotation: int) -> tuple[int, int, int, int]:
    """(width, height, xOffset, yOffset) for one rotation of an FRM."""
    xo = struct.unpack_from(">6h", data, 0x0A)
    yo = struct.unpack_from(">6h", data, 0x16)
    frame_offsets = struct.unpack_from(">6I", data, 0x22)
    base = 0x3E + frame_offsets[rotation if rotation < 6 else 0]
    if base + 12 > len(data):
        base = 0x3E
    w, h, _size, _fx, _fy = struct.unpack_from(">HHIhh", data, base)
    r = rotation % 6
    return w, h, xo[r], yo[r]


def vehicle_art_names(gi: GameIndex) -> dict[int, str]:
    """scenery PID -> art basename, for every vehicle proto in the base."""
    art = parse_lst(gi.vfs.read(r"art\scenery\scenery.lst"))
    out: dict[int, str] = {}
    lst = gi.proto_lst(2)
    for line in range(1, len(lst) + 1):
        pid = (2 << 24) | line
        pr = gi.proto(pid)
        if pr is None:
            continue
        i = pr.fid & 0xFFF
        if not (0 <= i < len(art)) or not art[i]:
            continue
        name = art[i].lower().strip().replace(".frm", "")
        if name in NOT_VEHICLES:
            continue
        if any(name.startswith(p) for p in VEHICLE_PREFIXES):
            out[pid] = name
    return out


def footprint_hexes(anchor_tile: int, w: int, h: int, ox: int, oy: int,
                    ground_band: int, side_frac: float, bottom_trim: int) -> list[int]:
    """Hexes whose screen anchor lands inside the vehicle's ground patch.

    A Fallout vehicle sprite is drawn tall for perspective, but only its lower
    slab sits on the ground. `sy` (the sprite's bottom edge, from
    object.cc::_obj_render_object) minus `ground_band` pixels is that slab.
    `side_frac` trims each side, since the sprite width includes overhang the
    player walks past.
    """
    hx, hy = hex_screen(anchor_tile)
    sx = hx + 16 + ox
    # A Fallout vehicle sprite is drawn tall for perspective (yOffset is often
    # 40-60px), but on the ground it sits roughly on its anchor row, one hex
    # south at most. Scaling the slab position by yOffset - which only reflects
    # how tall the artist drew the sprite - drags it a whole car-length past
    # the body. Use a small fixed nudge instead.
    mid = hy + 8 + 14 - bottom_trim
    inset_x = int(w * side_frac)
    left = sx - w // 2 + inset_x
    right = sx + w // 2 - inset_x
    bottom = mid + ground_band // 2
    top = mid - ground_band // 2

    ax, ay = hex_xy(anchor_tile)
    out: list[int] = []
    # a sprite is at most ~6 hexes wide and ~14 tall; search a generous window
    for dy in range(-4, 16):
        ny = ay + dy
        if not (0 <= ny < 200):
            continue
        for dx in range(-7, 8):
            nx = ax + dx
            if not (0 <= nx < 200):
                continue
            tile = ny * 200 + nx
            if tile == anchor_tile:
                continue
            px, py = hex_screen(tile)
            px += 16
            py += 8
            if left <= px <= right and top <= py <= bottom:
                out.append(tile)
    return out


# --------------------------------------------------------------- object rebuild

def read_critter_extras(data: bytes, off: int):
    base = off + 72 + 12
    return struct.unpack_from(">10i", data, base + 4)


def compute_blockers(m: MapFile, gi: GameIndex, *, ground_band: int,
                     side_frac: float, bottom_trim: int,
                     only: str | None = None) -> dict[int, set[int]]:
    """elevation -> set of hexes that should carry a new blocking hex."""
    vehicles = vehicle_art_names(gi)
    frm_cache: dict[int, bytes] = {}

    # hexes that already block movement (wall / scenery / existing 67) - do not
    # stack a second blocker on them.
    occupied: dict[int, set[int]] = {}
    for o in m.objects:
        if not (0 <= o.tile < 40000):
            continue
        t = pid_type(o.pid)
        if t in (2, 3) or o.pid == BLOCKING_HEX_PID:
            occupied.setdefault(o.elevation, set()).add(o.tile)

    want: dict[int, set[int]] = {}
    for o in m.objects:
        if o.pid not in vehicles or not (0 <= o.tile < 40000):
            continue
        name = vehicles[o.pid]
        ov = FOOTPRINT_OVERRIDE.get(name, {})
        if ov.get("skip"):
            continue
        if only and name != only:
            continue

        fpath = f"art/scenery/{name}.frm"
        try:
            data = frm_cache.get(o.pid) or gi.vfs.read(fpath)
            frm_cache[o.pid] = data
            w, h, fx, fy = frm_geometry(data, o.rotation)
        except Exception as exc:  # noqa: BLE001
            print(f"  ! {name}: cannot read FRM ({exc}) - skipped")
            continue

        hexes = set(footprint_hexes(o.tile, w, h, fx, fy,
                                    ground_band, side_frac, bottom_trim))
        ax, ay = hex_xy(o.tile)
        ndx, ndy = ov.get("nudge", (0, 0))
        if (ndx, ndy) != (0, 0):
            hexes = {(t // 200 + ndy) * 200 + (t % 200 + ndx) for t in hexes}
        for dx, dy in ov.get("add", []):
            hexes.add((ay + dy) * 200 + (ax + dx))
        for dx, dy in ov.get("drop", []):
            hexes.discard((ay + dy) * 200 + (ax + dx))

        occ = occupied.setdefault(o.elevation, set())
        hexes = {t for t in hexes if 0 <= t < 40000 and t not in occ}
        want.setdefault(o.elevation, set()).update(hexes)
        occ.update(hexes)   # keep two vehicles from double-placing on a shared hex

    return want


def blocker_specs(m: MapFile, gi: GameIndex, *, ground_band: int = 56,
                  side_frac: float = 0.26, bottom_trim: int = 0,
                  only: str | None = None) -> list[MapObjectSpec]:
    """MapObjectSpecs for every blocking hex a vehicle on this map needs.

    For callers that already hold a MapBuilder (map_finalize.py): append these
    to it before to_bytes(), no second round-trip. Defaults match the CLI.
    """
    want = compute_blockers(m, gi, ground_band=ground_band, side_frac=side_frac,
                            bottom_trim=bottom_trim, only=only)
    specs: list[MapObjectSpec] = []
    for elev_value, tiles in want.items():
        for tile in sorted(tiles):
            specs.append(MapObjectSpec(
                pid=BLOCKING_HEX_PID, fid=BLOCKING_HEX_FID, tile=tile,
                elevation=elev_value, rotation=0, frame=0,
                sid=-1, script_index=-1, extras=()))
    return specs


def rebuild(path: Path, m: MapFile, raw: bytes, gi: GameIndex,
            new_blockers: dict[int, set[int]]) -> bytes:
    # Preserve the map's own header script (map_enter_p_proc etc.) - it is
    # stored raw at header 0x24 and MapBuilder writes map_script_index there
    # verbatim. strip_blockers.py drops it; a map that gets both passes needs
    # bind_scripts.py to run afterwards regardless.
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

    for elev_index, tiles in new_blockers.items():
        for tile in sorted(tiles):
            mb.add(MapObjectSpec(
                pid=BLOCKING_HEX_PID, fid=BLOCKING_HEX_FID, tile=tile,
                elevation=elev_index, rotation=0, frame=0,
                sid=-1, script_index=-1, extras=()))

    return mb.to_bytes()


# --------------------------------------------------------------- render overlay

def render_overlay(path: Path, new_blockers: dict[int, set[int]], out: Path,
                   gi: GameIndex) -> None:
    try:
        from PIL import ImageDraw
    except ImportError:
        print("  (Pillow not installed - skipping --render)")
        return
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "focrender", str(Path(__file__).resolve().parent / "render.py"))
    rmod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(rmod)

    m = MapFile.parse(path.read_bytes(), gi.proto)
    img = rmod.render_map(gi, m, 0)
    # recompute the same bounds render_map used
    floors = m.floors[0]
    pts = []
    for sq, tid in enumerate(floors):
        if tid > 1:
            x, y = rmod.square_xy(sq)
            pts.append((x, y))
    for o in m.objects:
        if o.elevation == m.elevations[0] and 0 <= o.tile < 40000:
            x, y = rmod.hex_xy_screen(o.tile)
            pts.append((x - 100, y - 200))
            pts.append((x + 100, y + 60))
    minx = min(p[0] for p in pts)
    miny = min(p[1] for p in pts)

    dr = ImageDraw.Draw(img)
    n = 0
    marked: list[tuple[int, int]] = []
    for tiles in new_blockers.values():
        for tile in tiles:
            x, y = rmod.hex_xy_screen(tile)
            cx, cy = x + 16 - minx, y + 8 - miny
            dr.ellipse([cx - 4, cy - 4, cx + 4, cy + 4],
                       fill=(255, 40, 40, 255), outline=(255, 255, 255, 255))
            marked.append((cx, cy))
            n += 1
    # mark vehicle anchors in cyan so the offset is visible
    vehicles = vehicle_art_names(gi)
    for o in m.objects:
        if o.pid in vehicles and 0 <= o.tile < 40000:
            x, y = rmod.hex_xy_screen(o.tile)
            cx, cy = x + 16 - minx, y + 8 - miny
            dr.ellipse([cx - 5, cy - 5, cx + 5, cy + 5],
                       outline=(0, 230, 230, 255), width=2)
            marked.append((cx, cy))

    img = img.convert("RGB")
    if marked:
        pad = 160
        x0 = max(0, min(p[0] for p in marked) - pad)
        y0 = max(0, min(p[1] for p in marked) - pad)
        x1 = min(img.width, max(p[0] for p in marked) + pad)
        y1 = min(img.height, max(p[1] for p in marked) + pad)
        img = img.crop((x0, y0, x1, y1))
    img.save(out)
    print(f"  wrote {out} ({n} blockers red, vehicle anchors cyan) {img.size}")


# --------------------------------------------------------------- cli

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--map", default="mod/data/maps/REDWELL.MAP")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--render", metavar="PNG",
                    help="write a render with the new blockers marked, and stop")
    ap.add_argument("--only", metavar="ARTNAME",
                    help="only process this one vehicle art (e.g. ccart02)")
    ap.add_argument("--ground-band", type=int, default=56,
                    help="pixel height of the ground slab centred on the "
                         "vehicle's ground line")
    ap.add_argument("--side-frac", type=float, default=0.26,
                    help="fraction of sprite width to trim off each side")
    ap.add_argument("--bottom-trim", type=int, default=0,
                    help="pixels to trim off the bottom of the sprite "
                         "(negative extends it down)")
    args = ap.parse_args()

    gi = GameIndex()
    path = inside(ROOT / args.map)
    if not path.is_file():
        sys.exit(f"map not found: {path}")
    raw = path.read_bytes()
    m = MapFile.parse(raw, gi.proto)

    vehicles = vehicle_art_names(gi)
    present = [(vehicles[o.pid], o.tile) for o in m.objects
               if o.pid in vehicles and 0 <= o.tile < 40000]
    print(f"{path.name}: {len(m.objects)} objects, {len(present)} vehicles")
    for name, tile in sorted(present):
        hx, hy = hex_xy(tile)
        print(f"    {name:10s} at hex ({hx},{hy})")

    new_blockers = compute_blockers(
        m, gi, ground_band=args.ground_band, side_frac=args.side_frac,
        bottom_trim=args.bottom_trim, only=args.only)
    total = sum(len(v) for v in new_blockers.values())
    print(f"  blocking hexes to add: {total}")

    if args.render:
        out = inside(ROOT / args.render)
        out.parent.mkdir(parents=True, exist_ok=True)
        render_overlay(path, new_blockers, out, gi)
        return 0

    if args.dry_run:
        print("  dry run - nothing written")
        return 0
    if total == 0:
        print("  nothing to do")
        return 0

    out_bytes = rebuild(path, m, raw, gi, new_blockers)
    check = MapFile.parse(out_bytes, gi.proto)
    added = sum(1 for o in check.objects if o.pid == BLOCKING_HEX_PID) - \
        sum(1 for o in m.objects if o.pid == BLOCKING_HEX_PID)
    scripted = sum(1 for o in check.objects
                   if o.script_index not in (-1, 0xFFFFFFFF))

    backup = path.with_suffix(path.suffix + ".bak")
    shutil.copy2(path, backup)
    path.write_bytes(out_bytes)
    print(f"  wrote {path.name}: {len(check.objects)} objects "
          f"(+{added} blocking hexes)")
    print(f"  objects still carrying a script: {scripted}")
    print(f"  backup: {backup.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
