#!/usr/bin/env python3
"""
ids.py - the project's ID space.

    python tools/ids.py                 show the map of base vs FOC ranges
    python tools/ids.py --check         fail if the base has grown into a FOC range

Fallout 2 identifies almost everything positionally:

  * a script index is the 1-BASED line number in scripts.lst
    (proto.cc / scripts.cc; SCRIPT_* defines in scripts.h are those line numbers,
    and create_object_sid stores `sid - 1` as the 0-based array index)
  * a proto PID's index is the 1-BASED line number in its type's .lst
    (proto.cc::_proto_list_str)
  * a GVAR number is the POSITION of its line in vault13.gam - the engine never
    reads the names at all (game.cc::globalVarsRead)

So "allocating an ID" means "appending a line in the right place", and anything
that shifts earlier lines silently repoints every ID after it.

THE PADDING SCHEME
------------------
Our content starts at a FIXED ABSOLUTE index, with filler entries between the
end of the base's list and that index:

    [ base entries ][ filler ][ FOC entries ]
                              ^ always the same absolute index

If RPU later adds entries, the filler shrinks and OUR indices do not move. That
is the whole point - it makes base updates survivable instead of catastrophic.
If the base ever grows past a FOC start, --check fails loudly rather than
letting IDs silently shift.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib.paths import inside  # noqa: E402
from foclib import GameIndex, TYPE_NAME  # noqa: E402

# --------------------------------------------------------------------------- ranges

# Where FOC content begins in each positional space. Round numbers, chosen with
# comfortable headroom over the RPU 2.4.33 baseline measured on 2026-08-26.
FOC_START = {
    "script": 1600,        # base scripts.lst ends at 1558
    "gvar": 800,           # base vault13.gam has 791 slots (0..790)
    "proto_items": 700,    # base items.lst    596
    "proto_critters": 700,  # base critters.lst 549
    "proto_scenery": 2500,  # base scenery.lst  2334
    "proto_walls": 2100,   # base walls.lst    1991
    "proto_tiles": 4000,   # base tiles.lst    3892
    "proto_misc": 100,     # base misc.lst     50
    "map": 200,            # base maps.txt max map id 172

    # Art has its own positional .lst per directory. A proto's FID indexes into
    # these, so porting a critter or a piece of scenery means allocating here
    # too, not just in proto_*. Baselines measured 2026-08-26 against RPU 2.4.33.
    "art_items": 300,      # base art/items.lst      213
    "art_critters": 250,   # base art/critters.lst   152
    "art_scenery": 2500,   # base art/scenery.lst   2383
    "art_walls": 2200,     # base art/walls.lst     2049
    "art_tiles": 4200,     # base art/tiles.lst     4077
    "art_misc": 100,       # base art/misc.lst        56
    "art_heads": 50,       # base art/heads.lst       15
}

ART_KEY = {
    "items": "art_items", "critters": "art_critters", "scenery": "art_scenery",
    "walls": "art_walls", "tiles": "art_tiles", "misc": "art_misc",
    "heads": "art_heads",
}

# Filler names written into the gap. They must be harmless if never referenced.
FILLER = {
    "script": "focrsvd.int",
    "proto": "00000000.pro",
    "gvar": "GVAR_FOC_RESERVED",
    "art": "reserved.frm",
}

PROTO_KEY = {
    0: "proto_items", 1: "proto_critters", 2: "proto_scenery",
    3: "proto_walls", 4: "proto_tiles", 5: "proto_misc",
}


# --------------------------------------------------------------------------- art names

# The engine reads every art .lst line with strncpy(dst, line, 12) into a 13-byte
# slot (art.cc::artReadList, 0x419664 in fallout2.exe, unpatched by sfall 4.5).
# A longer name is silently cut to 12 chars, art_ptr_lock() returns NULL for
# it, and _obj_insert (0x48d9c4) dereferences that NULL the moment the object
# shares a tile with another one - the "referenced memory at 00000014" crash.
# Gecko does NOT truncate, so a bad name looks fine in the editor. Every ported
# art file therefore gets an 8.3 name: <1-char source tag><7 chars of stem>.frm
ENGINE_ART_NAME_MAX = 12
# A FID packs its art index into 12 bits (art.cc::artBuildFilePath: fid & 0xFFF),
# so lines 4097+ of any art .lst are unreachable; port.py refuses to allocate
# there and validate.py warns about anything already sitting there.
ART_INDEX_CAP = 4096
SOURCE_TAG = {"sonora": "s", "nevada": "n", "olympus": "o", "resurrection": "r"}


def _base36(n: int) -> str:
    digits = "0123456789abcdefghijklmnopqrstuvwxyz"
    out = ""
    while True:
        n, r = divmod(n, 36)
        out = digits[r] + out
        if n == 0:
            return out


def engine_art_name(source: str, original: str, taken: set[str]) -> str:
    """A <=12-char lowercase filename for `original` ported from `source` that is
    not in `taken` (a set of lowercase names already used in that art dir)."""
    stem, _, ext = original.lower().rpartition(".")
    if not stem:
        stem, ext = ext, "frm"
    tag = SOURCE_TAG.get(source.lower(), (source[:1].lower() if source else "x"))
    room = ENGINE_ART_NAME_MAX - len(ext) - 1 - len(tag)
    cand = f"{tag}{stem[:room]}.{ext}"
    if cand not in taken:
        return cand
    # stems one char too long usually differ in their LAST char (marker01/02/03):
    # drop the char before it instead of the distinguishing one
    if len(stem) > room:
        cand = f"{tag}{stem[:room - 1]}{stem[-1]}.{ext}"
        if cand not in taken:
            return cand
    for width in (1, 2, 3):
        for k in range(36 ** width):
            cand = f"{tag}{stem[:room - width]}{_base36(k).rjust(width, '0')}.{ext}"
            if cand not in taken:
                return cand
    raise RuntimeError(f"could not find a free name for {source}/{original}")


def check_art_name(art_dir: str, name: str) -> None:
    """Refuse an art filename the engine would truncate into a missing file."""
    if len(name) > ENGINE_ART_NAME_MAX:
        sys.exit(
            f"\nREFUSING art name art\\{art_dir}\\{name} ({len(name)} chars).\n"
            f"The engine keeps only the first {ENGINE_ART_NAME_MAX} characters of a "
            f".lst entry (art.cc::artReadList), so this file could never be found: "
            f"invisible in game, and a crash at 0x48d9c4 when stacked on a tile.\n"
            f"Use ids.engine_art_name() to pick a name."
        )


def base_counts(gi: GameIndex) -> dict[str, int]:
    """How many entries the BASE already occupies in each space."""
    out = {"script": len(gi.scripts), "gvar": len(gi.gvar_slots)}
    for t, key in PROTO_KEY.items():
        out[key] = len(gi.proto_lst(t))
    out["map"] = _max_map_id(gi) + 1
    for art_dir, key in ART_KEY.items():
        out[key] = len(_art_lst(gi, art_dir))
    return out


def _art_lst(gi: GameIndex, art_dir: str) -> list[str]:
    from foclib import parse_lst
    try:
        return parse_lst(gi.vfs.read(f"art\\{art_dir}\\{art_dir}.lst"))
    except FileNotFoundError:
        return []


def _max_map_id(gi: GameIndex) -> int:
    import re
    try:
        txt = gi.vfs.read_text("data\\maps.txt")
    except FileNotFoundError:
        return -1
    ids = [int(m) for m in re.findall(r"\[Map\s+(\d+)\]", txt, re.I)]
    return max(ids) if ids else -1


def check(gi: GameIndex) -> list[str]:
    """Return a list of problems; empty means the ID space is intact."""
    problems = []
    counts = base_counts(gi)
    for key, start in FOC_START.items():
        used = counts.get(key, 0)
        if used > start:
            problems.append(
                f"{key}: base now occupies {used} entries, which has grown past "
                f"the FOC start at {start}. Every FOC {key} ID after that point "
                f"has shifted. Raise FOC_START['{key}'] and re-point existing "
                f"content, or pin the base back."
            )
    return problems


def report(gi: GameIndex) -> None:
    counts = base_counts(gi)
    print(f"base: {gi.base}")
    print()
    print(f"{'space':16s} {'base uses':>10s} {'filler':>8s} {'FOC starts':>11s}")
    print("-" * 50)
    for key in FOC_START:
        used = counts.get(key, 0)
        start = FOC_START[key]
        pad = max(0, start - used)
        flag = "" if used <= start else "   <-- OVERFLOW"
        print(f"{key:16s} {used:10d} {pad:8d} {start:11d}{flag}")
    print()
    print("Indices are 1-based line numbers for scripts and protos; GVARs are")
    print("0-based positions. A FOC script at index 1600 is line 1600 of the")
    print("generated scripts.lst, and SCRIPT_FOC_x should be #defined as 1600.")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="exit non-zero if the base has grown into a FOC range")
    ap.add_argument("--base", type=Path, default=None)
    args = ap.parse_args()

    gi = GameIndex(inside(args.base) if args.base else None)
    problems = check(gi)

    if args.check:
        if problems:
            for p in problems:
                print("ERROR: " + p)
            return 1
        print("ID space OK: base fits below every FOC start.")
        return 0

    report(gi)
    if problems:
        print()
        for p in problems:
            print("ERROR: " + p)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
