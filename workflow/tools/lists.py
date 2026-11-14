#!/usr/bin/env python3
"""
lists.py - assemble the .lst / .gam files the game actually loads.

    full list = [ base entries ] [ filler up to FOC_START ] [ FOC entries ]

The filler is what makes base updates survivable: if RPU grows, the filler
shrinks and FOC indices stay put. See tools/ids.py for the reasoning.

Used by both deploy.py (writes them) and validate.py (checks against them),
so the two can never disagree about what an ID means.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib import GameIndex, TYPE_NAME  # noqa: E402
from gen_ids import read_registry  # noqa: E402
from ids import ART_KEY, FILLER, FOC_START, PROTO_KEY  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
IDS = ROOT / "mod" / "ids"


class Overflow(Exception):
    pass


def _assemble(base_entries: list[str], foc_entries: list[str],
              start: int, filler: str, space: str) -> list[str]:
    if len(base_entries) > start:
        raise Overflow(
            f"{space}: base occupies {len(base_entries)} entries, past the FOC "
            f"start at {start}"
        )
    pad = [filler] * (start - len(base_entries))
    return list(base_entries) + pad + list(foc_entries)


def scripts_lst(gi: GameIndex) -> list[str]:
    """Full scripts.lst content, 1-based: result[0] is line 1."""
    # A retired slot (blank line in scripts.txt) has no .int of its own; render
    # it as the reserved filler so scripts.lst never carries an empty line.
    foc = [e or FILLER["script"] for e in read_registry(IDS / "scripts.txt")]
    # FOC_START is a 1-based LINE number, so it is index start-1 in the array
    return _assemble(gi.scripts, foc, FOC_START["script"] - 1,
                     FILLER["script"], "script")


# Local variables each FOC script gets. The engine sizes a script's local
# vars from the "# local_vars=N" note on its scripts.lst line (scripts.cc
# scriptsLoadScriptsList) and has none without it: set_local_var is dropped
# and local_var reads garbage. Local var 0 is also the engine's reaction
# slot: start_gdialog opens a talking head good above 10, bad at -10 or
# below (reaction.cc), and a script without local vars hands it an
# uninitialised value - that is what had the colonists smiling. FOC scripts
# leave var 0 at 0 so every head opens neutral.
FOC_LOCAL_VARS = 8


def scripts_lst_text(gi: GameIndex) -> list[str]:
    """scripts.lst as deployed: scripts_lst() plus each line's local_vars note
    (the base's own counts, FOC_LOCAL_VARS for ours)."""
    names = scripts_lst(gi)
    raw = gi.vfs.read("scripts\\scripts.lst").decode("cp1252", "replace").splitlines()
    foc_first = FOC_START["script"] - 1
    out = []
    for i, name in enumerate(names):
        if not name:
            out.append(name)
            continue
        if i >= foc_first:
            n = FOC_LOCAL_VARS
        else:
            m = re.search(r"#.*local_vars=(\d+)", raw[i]) if i < len(raw) else None
            n = int(m.group(1)) if m else 0
        out.append(f"{name:<13} # local_vars={n}")
    return out


def proto_lst(gi: GameIndex, obj_type: int) -> list[str]:
    key = PROTO_KEY[obj_type]
    foc = read_registry(IDS / f"{key}.txt")
    if not foc:
        return list(gi.proto_lst(obj_type))
    return _assemble(gi.proto_lst(obj_type), foc, FOC_START[key] - 1,
                     FILLER["proto"], key)


def base_art_lst(gi: GameIndex, art_dir: str) -> list[str]:
    from foclib import parse_lst
    try:
        return parse_lst(gi.vfs.read(f"art\\{art_dir}\\{art_dir}.lst"))
    except FileNotFoundError:
        return []


def art_lst(gi: GameIndex, art_dir: str) -> list[str]:
    """
    Full art .lst, 1-based. A proto's FID indexes into this, so if we allocate
    an art index we MUST ship the matching .lst or the engine draws whatever
    happens to sit at that line in the base's list.
    """
    key = ART_KEY[art_dir]
    foc = read_registry(IDS / f"{key}.txt")
    base = base_art_lst(gi, art_dir)
    out = list(base) if not foc else _assemble(base, foc, FOC_START[key] - 1, FILLER["art"], key)
    if art_dir == "tiles":
        # new tiles in spare lines of the base list (tools/tile_slots.py)
        for tid, name in tile_slots().items():
            if 0 <= tid < len(out):
                out[tid] = name
    return out


def tile_slots() -> dict[int, str]:
    """mod/ids/art_tiles_slots.txt: tile id -> the new tile's file name."""
    out: dict[int, str] = {}
    p = IDS / "art_tiles_slots.txt"
    if p.is_file():
        for line in p.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                tid, name = line.split(",", 1)
                out[int(tid)] = name.strip()
    return out


def gvar_names(gi: GameIndex) -> list[str]:
    """Full vault13.gam slot list, 0-based: result[0] is GVAR 0."""
    foc = read_registry(IDS / "gvars.txt")
    if not foc:
        return [n or "" for n in gi.gvar_slots]
    base = [n or "" for n in gi.gvar_slots]
    return _assemble(base, foc, FOC_START["gvar"], FILLER["gvar"], "gvar")


def render_gam(names: list[str]) -> bytes:
    """
    Rebuild vault13.gam from the slot list.

    Numbering is positional and the engine never reads the names
    (game.cc::globalVarsRead), so the format only has to be: the section
    header, then exactly one counted line per slot. Comments carry the index
    for humans and are ignored by the engine.
    """
    out = ["//  GENERATED by tools/deploy.py - do not edit.",
           "//  Slot numbers are positions in this file. Never reorder.",
           "",
           "GAME_GLOBAL_VARS:",
           ""]
    # NOTE: the blank line above starts with '\n' so the engine skips it.
    for i, name in enumerate(names):
        label = name if name else f"GVAR_UNUSED_{i}"
        out.append(f"{label:<44}:=0;    //      ({i})")
    return ("\r\n".join(out) + "\r\n").encode("cp1252", "replace")


def render_lst(entries: list[str]) -> bytes:
    return ("\r\n".join(entries) + "\r\n").encode("cp1252", "replace")


def foc_script_index(name: str) -> int | None:
    """1-based scripts.lst line for one of OUR scripts, or None."""
    foc = read_registry(IDS / "scripts.txt")
    want = name.lower()
    if not want.endswith(".int"):
        want += ".int"
    for i, n in enumerate(foc):
        if n.lower() == want:
            return FOC_START["script"] + i
    return None


def summary(gi: GameIndex) -> str:
    lines = []
    s = scripts_lst(gi)
    lines.append(f"scripts.lst    {len(s):6d} lines "
                 f"(base {len(gi.scripts)}, FOC from {FOC_START['script']})")
    g = gvar_names(gi)
    lines.append(f"vault13.gam    {len(g):6d} slots "
                 f"(base {len(gi.gvar_slots)}, FOC from {FOC_START['gvar']})")
    for t, key in PROTO_KEY.items():
        foc = read_registry(IDS / f"{key}.txt")
        if foc:
            p = proto_lst(gi, t)
            lines.append(f"{TYPE_NAME[t]+'.lst':14s} {len(p):6d} lines "
                         f"(base {len(gi.proto_lst(t))}, FOC from {FOC_START[key]})")
    return "\n".join(lines)


if __name__ == "__main__":
    print(summary(GameIndex()))
