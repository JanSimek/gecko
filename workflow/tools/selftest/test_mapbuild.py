#!/usr/bin/env python3
"""
Writer/reader round-trip.

The reader is verified against all 270 base maps. So if a written map reads
back through that same reader with every field intact, the writer agrees with
the format the engine actually consumes.

Also asserts the elevation guard fires, because that is the bug this whole
exercise exists to avoid.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from foclib import GameIndex, MapFile  # noqa: E402
from mapbuild import MapBuilder, MapObjectSpec, extras_for, hex_of  # noqa: E402


def main() -> int:
    gi = GameIndex()
    fails = []

    # ---- 1. the elevation guard --------------------------------------------
    try:
        MapBuilder("GUARD.MAP", elevations=(0,), start_elevation=1)
        fails.append("elevation guard did NOT fire for start=0 on a level-1 map")
    except ValueError as e:
        if "black-floor" not in str(e):
            fails.append(f"guard fired with an unhelpful message: {e}")
        print("  ok: start elevation is checked against declared elevations")

    # ---- 2. flags are derived, not guessed ---------------------------------
    cases = {(0,): 0x0C, (0, 1): 0x08, (0, 1, 2): 0x00, (1, 2): 0x02, (0, 2): 0x04}
    for elevs, want in cases.items():
        got = MapBuilder("F.MAP", elevations=elevs).flags
        if got != want:
            fails.append(f"flags for {elevs}: got 0x{got:02X}, want 0x{want:02X}")
    print(f"  ok: flags derived correctly for {len(cases)} elevation combinations")

    # ---- 3. round-trip a populated map -------------------------------------
    mb = MapBuilder("RT.MAP", elevations=(0,), start_tile=20100, map_id=200)

    # a floor with real variety, the way real maps do it
    tiles = gi.proto_lst(4)  # not used for ids; ids come from art\tiles
    from foclib import parse_lst
    tile_names = parse_lst(gi.vfs.read("art/tiles/tiles.lst"))
    idx = {n.lower(): i + 1 for i, n in enumerate(tile_names) if n}
    ground = [idx[n] for n in ("cav4008.frm", "cav4009.frm", "bld2043.frm")
              if n in idx]
    if not ground:
        fails.append("could not resolve any test tile names in the base")
        ground = [2]
    mb.fill_floor(0, 30, 30, 39, 39,
                  lambda x, y: ground[(x * 7 + y * 3) % len(ground)])

    # one object of each type we can size
    made = 0
    for obj_type, count in ((2, 3), (3, 3), (0, 2)):
        lst = gi.proto_lst(obj_type)
        placed = 0
        for i, fn in enumerate(lst):
            if not fn or placed >= count:
                continue
            pid = (obj_type << 24) | (i + 1)
            pr = gi.proto(pid)
            if pr is None:
                continue
            mb.add(MapObjectSpec(
                pid=pid, fid=pr.fid,
                tile=hex_of(32 + placed, 32 + obj_type),
                elevation=0,
                extras=extras_for(obj_type, pid, pr.subtype),
            ))
            placed += 1
            made += 1

    data = mb.to_bytes()
    m = MapFile.parse(data, gi.proto)

    checks = [
        ("version", m.version, 20),
        ("name", m.name, "RT.MAP"),
        ("elevations", m.elevations, [0]),
        ("flags", m.flags, 0x0C),
        ("start tile", m.default_tile, 20100),
        ("start elevation", m.default_elevation, 0),
        ("map id", m.map_id, 200),
        ("object count", len(m.objects), made),
        ("floor squares", sum(1 for f in m.floors[0] if f > 1), 100),
    ]
    for label, got, want in checks:
        if got != want:
            fails.append(f"round-trip {label}: got {got!r}, want {want!r}")

    for a, b in zip(mb.objects, sorted(m.objects, key=lambda o: o.tile)):
        pass  # order is by elevation group; compare as sets instead
    wrote = {(o.pid, o.tile) for o in mb.objects}
    read = {(o.pid, o.tile) for o in m.objects}
    if wrote != read:
        fails.append(f"object identity mismatch: {wrote ^ read}")

    print(f"  ok: round-tripped a {len(data)} byte map with {made} objects, "
          f"100 floored squares")

    # ---- 3b. empty squares must be 1, never 0 ------------------------------
    zeros = sum(1 for v in m.floors[0] if v == 0) + sum(1 for v in m.roofs[0] if v == 0)
    if zeros:
        fails.append(f"{zeros} squares written as tile id 0. Empty is 1; 0 is a "
                     f"real index pointing at art that is not there, and the "
                     f"floor will not draw.")
    ones = sum(1 for v in m.floors[0] if v == 1)
    print(f"  ok: {ones} empty floor squares carry the sentinel 1, none carry 0")

    # ---- 4. the file is the size the format implies ------------------------
    expect = 0xEC + 1 * 10000 * 4 + 5 * 4 + 4 + 3 * 4
    if len(data) < expect:
        fails.append(f"map is {len(data)} bytes, shorter than the {expect} "
                     f"a header + one tile block + empty tables requires")

    print()
    if fails:
        for f in fails:
            print(f"FAIL: {f}")
        return 1
    print("map writer agrees with the reader that reads all 270 base maps.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
