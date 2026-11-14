#!/usr/bin/env python3
"""
map_vehicle_blockers.py - make cars, trucks, wagons and carts solid on a map.

    python tools/map_vehicle_blockers.py RWRUINS.MAP --dry-run
    python tools/map_vehicle_blockers.py RWRUINS.MAP
    python tools/map_vehicle_blockers.py --all

A vehicle is one scenery object that blocks only its anchor hex, while its
sprite covers a patch several hexes wide, so the player walks through most of
it. Fallout's own fix is scenery proto 67, "Secret Blocking Hex", on every hex
of the footprint. The footprint comes from car_blockers.py (the sprite's
ground slab, from the FRM and object.cc's draw maths); this tool only differs
in which objects count as vehicles and in how it writes:

  * vehicles are found by their proto NAME (car, truck, wagon, cart, bike,
    "rusting vehicle", "patrol automobile"...), not by base-game art names,
    so the cars ported from the TCs are caught too;
  * the blockers are APPENDED to the map's object lists byte for byte, the
    rest of the map untouched (car_blockers.py rebuilds the whole map through
    MapBuilder, which once collapsed Redwell's four brahmin into one);
  * hexes that already hold any object are left alone, so running it twice
    adds nothing, and SKIP lists objects that must stay passable once gone
    (the car in the ruins stash wall, which the sledgehammers remove).

Both copies of the map (base/mods/foc/maps and mod/data/maps) are written
when they match; the old file goes to _safety_backups/vehicle_blockers/.
"""

from __future__ import annotations

import argparse
import filecmp
import shutil
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib import GameIndex, MapFile  # noqa: E402
from foclib.formats import parse_lst  # noqa: E402
import car_blockers as cb  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DEPLOYED = ROOT / "base" / "mods" / "foc" / "maps"
AUTHORED = ROOT / "mod" / "data" / "maps"
BACKUP = ROOT / "_safety_backups" / "vehicle_blockers"

BLOCKER_PID = cb.BLOCKING_HEX_PID          # scenery 67
OBJ_ID_OFF, OBJ_TILE_OFF, OBJ_ELEV_OFF = 0x00, 0x04, 0x28

NAME_WORDS = ("car", "truck", "wagon", "cart", "bike", "vehicle", "automobile",
              "jeep", "bus", "motorcycle", "highwayman", "wreck")
NAME_NOT = ("cartography", "cargo crate", "carpet", "card", "scarecrow", "bush",
            "carton", "carcass")

# (map name, pid, tile) of vehicles that must not get blockers
SKIP = {
    ("RWRUINS.MAP", 0x02000A77, 25849),     # the car in the stash wall (rw_stash.h)
    ("RWRUINS.MAP", 0x02000A76, 25651),
}


def pro_name(gi: GameIndex, pid: int, cache: dict, msg: dict) -> str:
    if pid not in cache:
        name = ""
        try:
            b = gi.vfs.read(gi.proto_path(pid))
            name = msg.get(struct.unpack_from(">i", b, 4)[0], "")
        except Exception:  # noqa: BLE001
            pass
        cache[pid] = name.lower()
    return cache[pid]


def load_msg(gi: GameIndex) -> dict:
    out = {}
    txt = gi.vfs.read(r"text\english\game\pro_scen.msg").decode("cp1252", "replace")
    for line in txt.splitlines():
        if line.startswith("{"):
            parts = line.split("}")
            try:
                out[int(parts[0][1:])] = parts[2].lstrip("{")
            except (ValueError, IndexError):
                pass
    return out


def is_vehicle(name: str) -> bool:
    if not name or any(n in name for n in NAME_NOT):
        return False
    words = name.replace(",", " ").split()
    return any(w in NAME_WORDS or any(w.startswith(k) for k in ("automobile", "truck", "wagon"))
               for w in words) or "rusting vehicle" in name or "patrol automobile" in name


def object_lists(raw: bytes, m: MapFile, gi: GameIndex):
    """(total_off, [(count_off, [(obj, start, end)...]) for elevations 0..2], end_of_objects)."""
    first = m.objects[0]
    total_off = first.off - 4 * (first.elevation + 2)
    p = total_off + 4
    lists = []
    for _e in range(3):
        n = struct.unpack_from(">i", raw, p)[0]
        count_off = p
        p += 4
        spans = []
        for _ in range(n):
            obj, end = MapFile._read_object(raw, p, gi.proto)
            spans.append((obj, p, end))
            p = end
        lists.append((count_off, spans))
    return total_off, lists, p


def template_record(gi: GameIndex) -> bytes:
    """A Secret Blocking Hex record from a map that has one."""
    for p in sorted(DEPLOYED.glob("*")):
        if p.suffix.lower() != ".map":
            continue
        raw = p.read_bytes()
        m = gi.load_map(raw)
        for o in m.objects:
            if o.pid == BLOCKER_PID:
                _obj, end = MapFile._read_object(raw, o.off, gi.proto)
                return raw[o.off:end]
    sys.exit("no Secret Blocking Hex on any map to copy a record from")


def plan(raw: bytes, m: MapFile, gi: GameIndex, name: str, names: dict, msg: dict, art: list):
    occupied = {}
    for o in m.objects:
        if 0 <= o.tile < 40000:
            occupied.setdefault(o.elevation, set()).add(o.tile)
    want: dict[int, set[int]] = {}
    found = []
    frm_cache: dict[str, bytes] = {}
    for o in m.objects:
        if (o.pid >> 24) != 2 or not (0 <= o.tile < 40000):
            continue
        if (name.upper(), o.pid, o.tile) in SKIP:
            continue
        pname = pro_name(gi, o.pid, names, msg)
        if not is_vehicle(pname):
            continue
        idx = o.fid & 0xFFF
        if not (0 <= idx < len(art)) or not art[idx]:
            continue
        aname = art[idx].strip().lower().replace(".frm", "")
        try:
            data = frm_cache.get(aname) or gi.vfs.read(f"art/scenery/{aname}.frm")
            frm_cache[aname] = data
            w, h, fx, fy = cb.frm_geometry(data, o.rotation)
        except Exception:  # noqa: BLE001
            continue
        hexes = set(cb.footprint_hexes(o.tile, w, h, fx, fy, 56, 0.26, 0))
        ov = cb.FOOTPRINT_OVERRIDE.get(aname, {})
        if ov.get("skip"):
            continue
        ndx, ndy = ov.get("nudge", (0, 0))
        if (ndx, ndy) != (0, 0):
            hexes = {(t // 200 + ndy) * 200 + (t % 200 + ndx) for t in hexes}
        occ = occupied.setdefault(o.elevation, set())
        hexes = {t for t in hexes if 0 <= t < 40000 and t not in occ}
        found.append((pname, (o.tile % 200, o.tile // 200), len(hexes)))
        want.setdefault(o.elevation, set()).update(hexes)
        occ.update(hexes)
    return want, found


def apply(raw: bytes, m: MapFile, gi: GameIndex, want: dict, rec: bytes) -> bytes:
    total_off, lists, end = object_lists(raw, m, gi)
    next_id = max(struct.unpack_from(">i", raw, o.off)[0] for o in m.objects) + 1
    out = bytearray(raw[:total_off])
    added = sum(len(v) for v in want.values())
    out += struct.pack(">i", struct.unpack_from(">i", raw, total_off)[0] + added)
    for e, (count_off, spans) in enumerate(lists):
        new = []
        for tile in sorted(want.get(e, ())):
            r = bytearray(rec)
            struct.pack_into(">i", r, OBJ_ID_OFF, next_id)
            struct.pack_into(">i", r, OBJ_TILE_OFF, tile)
            struct.pack_into(">i", r, OBJ_ELEV_OFF, e)
            next_id += 1
            new.append(bytes(r))
        out += struct.pack(">i", len(spans) + len(new))
        out += b"".join(raw[s:t] for _o, s, t in spans) + b"".join(new)
    out += raw[end:]
    return bytes(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("map", nargs="?")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    gi = GameIndex(pristine=False)
    msg = load_msg(gi)
    art = parse_lst(gi.vfs.read(r"art\scenery\scenery.lst"))
    names: dict = {}
    rec = template_record(gi)
    maps = ([p.name for p in sorted(DEPLOYED.glob("*")) if p.suffix.lower() == ".map"]
            if args.all else [args.map])
    for name in maps:
        dep, aut = DEPLOYED / name, AUTHORED / name
        if aut.exists() and not filecmp.cmp(dep, aut, shallow=False):
            print(f"{name}: the two copies differ - run map_fixup.py on it first; skipped")
            continue
        raw = dep.read_bytes()
        m = gi.load_map(raw)
        want, found = plan(raw, m, gi, name, names, msg, art)
        n = sum(len(v) for v in want.values())
        print(f"{name}: {len(found)} vehicles, {n} new blocking hexes")
        for pname, xy, k in found:
            print(f"   {pname:28} at {xy}: {k}")
        if args.dry_run or n == 0:
            continue
        out = apply(raw, m, gi, want, rec)
        check = gi.load_map(out)
        assert len(check.objects) == len(m.objects) + n
        BACKUP.mkdir(parents=True, exist_ok=True)
        stamp = time.strftime("%Y%m%d_%H%M%S")
        shutil.copy2(dep, BACKUP / f"{name}.{stamp}")
        dep.write_bytes(out)
        if aut.exists():
            aut.write_bytes(out)
        print(f"   wrote {name} (old copy in {BACKUP.relative_to(ROOT)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
