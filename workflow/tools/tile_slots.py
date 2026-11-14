#!/usr/bin/env python3
"""
tile_slots.py - new floor and roof tiles for FOC, in slots of tiles.lst that
hold nothing FOC can lose.

    python tools/tile_slots.py --list                     # how many slots are left
    python tools/tile_slots.py --port nevada bport001.frm bport002.frm ...

WHY
---
A map square stores a 12-bit tile id, so tiles.lst can never pass 4096 lines,
and the base list already has 4077 (CLAUDE.md 4d). But not every line is a
picture of its own: some repeat a tile that an earlier line already draws (one
picture sits on 114 lines), and two have no art at all. Those lines are spare
as long as nothing of FOC's points at them. A slot is taken when it is
  - the first line of its picture (that one stays), or ids 0 and 1;
  - used by a map in mod/data/maps or base/mods/foc/maps;
  - used by any stamp in gecko's patterns folder (a stamp made in the FOC
    profile could hold it);
  - already given a new tile (mod/ids/art_tiles_slots.txt).
Every other spare line can take a new tile without changing anything that is
drawn today.

mod/ids/art_tiles_slots.txt holds "<tile id>,<file name>" per new tile;
tools/lists.py puts those names on those lines of the tiles.lst deploy.py
ships, so the game and gecko's FOC profile both see them. The art itself
goes to mod/data/art/tiles/ and into the ledger (mod/assets.csv).

port_stamp.py ports a stamp's missing tiles through here instead of swapping
them for the nearest-looking FOC tile, while slots last.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import port  # noqa: E402
from foclib import GameIndex, parse_lst  # noqa: E402
from foclib.formats import MapFile  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
SLOTS = ROOT / "mod" / "ids" / "art_tiles_slots.txt"
TILE_DIR = ROOT / "mod" / "data" / "art" / "tiles"
MAP_DIRS = (ROOT / "mod" / "data" / "maps", ROOT / "base" / "mods" / "foc" / "maps")
from foclib import config as _config  # noqa: E402
PATTERNS = _config.patterns_dir()
MAX_NAME = 12   # the engine keeps 12 characters of an art .lst name

HEADER = ("# New floor and roof tiles in spare lines of the base tiles.lst (tools/tile_slots.py).\n"
          "# <tile id>,<file in art/tiles>. Never reorder or reuse a line: maps and stamps hold these ids.\n")


TILE_FILE = re.compile(r"[A-Za-z0-9_-]+\.frm", re.IGNORECASE)   # a file in art/tiles


def read_slots() -> dict[int, str]:
    out: dict[int, str] = {}
    if SLOTS.is_file():
        for line in SLOTS.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            tid, name = line.split(",", 1)
            out[int(tid)] = name.strip()
    return out


def add_slot(tid: int, name: str) -> None:
    text = SLOTS.read_text(encoding="utf-8") if SLOTS.is_file() else HEADER
    if not text.endswith("\n"):
        text += "\n"
    SLOTS.write_text(text + f"{tid},{name}\n", encoding="utf-8")


def _ids_in_maps(gi: GameIndex) -> set[int]:
    used: set[int] = set()
    for folder in MAP_DIRS:
        for p in folder.glob("*.[mM][aA][pP]"):
            try:
                m = MapFile.parse(p.read_bytes(), gi.proto)
            except Exception:
                continue
            for layer in m.floors + m.roofs:
                used.update(v & 0xFFF for v in layer)
    return used


def _ids_in_stamps() -> set[int]:
    used: set[int] = set()
    if not PATTERNS.is_dir():
        return used
    for p in PATTERNS.rglob("*.json"):
        try:
            d = json.loads(p.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            continue
        for v in d.get("variants", []) if isinstance(d, dict) else []:
            for layer in ("floor", "roof"):
                for t in v.get(layer, []):
                    used.add(int(t.get("tileId", 0)))
    return used


def spare_slots(gi: GameIndex | None = None) -> list[int]:
    """The lines of the base tiles.lst that can take a new tile, lowest first."""
    gi = gi or GameIndex()                      # pristine base
    base = parse_lst(gi.vfs.read("art\\tiles\\tiles.lst"))
    seen: set[str] = set()
    spare: list[int] = []
    for i, name in enumerate(base):
        if i <= 1:
            continue
        try:
            h = hashlib.sha1(gi.vfs.read("art\\tiles\\" + name), usedforsecurity=False).hexdigest()
        except FileNotFoundError:
            spare.append(i)                     # a line with no art
            continue
        if h in seen:
            spare.append(i)                     # a repeat of an earlier picture
        seen.add(h)
    taken = set(read_slots()) | _ids_in_maps(GameIndex(pristine=False)) | _ids_in_stamps()
    return [i for i in spare if i not in taken]


def port_tile(source: str, src: GameIndex, name: str, slots: list[int], notes: str = "") -> int:
    """Copy one tile from a source game into the next spare slot; its id.
    `slots` is spare_slots() and is consumed."""
    name = name.lower()
    if len(name) > MAX_NAME:
        raise ValueError(f"{name}: tile names must be {MAX_NAME} characters or fewer")
    for tid, have in read_slots().items():
        if have.lower() == name:
            return tid
    if not slots:
        raise RuntimeError("no spare tile slots left")
    data = src.vfs.read("art\\tiles\\" + name)
    dst = TILE_DIR / name
    if dst.exists() and dst.read_bytes() != data:
        raise RuntimeError(f"{dst} already exists with other art")
    TILE_DIR.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(data)
    tid = slots.pop(0)
    add_slot(tid, name)
    row = port.ledger_row(source, "art", f"art\\tiles\\{name}", name, f"mod/data/art/tiles/{name}", tid,
                          notes or "tile in a spare tiles.lst line (tile_slots.py)")
    have = {(r["source"], r["source_path"], r["dest_path"]) for r in port.read_ledger()}
    if (row["source"], row["source_path"], row["dest_path"]) not in have:
        port.append_ledger([row])
    return tid


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--list", action="store_true", help="count the spare slots")
    ap.add_argument("--port", nargs="+", metavar=("SOURCE", "NAME"), help="a source and tile file names")
    args = ap.parse_args()
    if args.list:
        spare = spare_slots()
        print(f"{len(read_slots())} tiles in slots, {len(spare)} spare: {spare[:12]}{' ...' if len(spare) > 12 else ''}")
        return 0
    if args.port:
        source, names = args.port[0], args.port[1:]
        src = port.open_source(source)
        spare = spare_slots()
        for n in names:
            if not TILE_FILE.fullmatch(n):
                raise SystemExit(f"{n!r}: give a tile file name, like ADB2000.FRM")
            print(f"{n} -> {port_tile(source, src, n, spare)}")
        return 0
    ap.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
