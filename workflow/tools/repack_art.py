#!/usr/bin/env python3
"""
repack_art.py - move every piece of FOC art that something actually uses below
the engine's 4096 art-index cap, swapping unused art up past it.

    python tools/repack_art.py --dry-run
    python tools/repack_art.py

WHY THIS EXISTS
---------------
A FID carries a 12-bit art index for the ENGINE (art.cc::artBuildFilePath masks
with 0xFFF), so an art .lst entry at index 4096 or above is unreachable in-game:
a proto or map object pointing there draws `index & 0xFFF`, an unrelated sprite.
gecko does not mask, so such an object looks right in the editor and wrong in
the game. The bulk ports (port_bulk.py) put thousands of TC scenery and wall
pieces into mod/ids/art_*.txt - far more than fit under the cap - while FOC's
maps and stamps use only a small fraction of them.

So the lists are REPACKED rather than pruned: art something uses is guaranteed
a slot below 4096, and art nothing uses yet sits above it. Nothing is deleted,
so the whole ported palette stays browsable in gecko; placing a piece from
above the cap and re-running this tool swaps it down.

WHAT COUNTS AS USED
-------------------
  * every object (and inventory object) in every FOC map file, in both
    base/mods/foc/maps and mod/data/maps, backups included (a restored backup
    must still draw the right art): its own FID and its proto's FID
  * every gecko stamp in the patterns folder (not _tc_originals, which are in
    their source game's ID space), and every stamp in the Desktop 'Gecko
    Stamps*' folders tools/building_stamps.py writes (gecko reads those
    through junctions in the patterns folder): frmPid and the proto's FID
  * every FOC proto a script names by its PID_* define in scripts_src

WHAT GETS REWRITTEN
-------------------
Only swapped entries move; everything else keeps its index. Proto PIDs never
change - only art indices do - so scripts, the proto registries and map PIDs
are untouched. For each swap the tool rewrites:
  * mod/ids/art_<dir>.txt              (the two lines trade places)
  * every .pro under mod/data/proto and base/mods/foc/proto   (FID at offset 8)
  * every map object FID in the map files above
  * every stamp frmPid
  * mod/assets.csv art rows' dest_id   (then regenerates ASSETS.md)
  * base/mods/foc/art/<dir>/<dir>.lst  (so gecko and the game see it without a
                                        full deploy.py run)
and writes the full old->new table to build/repack_art/<timestamp>.json.

Close gecko first: a gecko session opened before the repack still holds the old
indices and would write them back on save.
"""

from __future__ import annotations

import argparse
import csv
import datetime as _dt
import json
import os
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import lists  # noqa: E402
import port  # noqa: E402
from foclib import FOC_LAYER, GameIndex, MapFile, TYPE_NAME, parse_pro, pid_index, pid_type  # noqa: E402
from ids import ART_INDEX_CAP, ART_KEY, FOC_START  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
IDS = ROOT / "mod" / "ids"
AUTHORED = ROOT / "mod" / "data"
DEPLOYED = ROOT / "base" / "mods" / FOC_LAYER
from foclib import config as _config  # noqa: E402
PATTERNS = _config.patterns_dir()
FOC_IDS_H = ROOT / "scripts_src" / "foc" / "foc_ids.h"
KEEP_PROTOS = ROOT / "mod" / "ids" / "keep_protos.txt"   # PIDs to keep below the cap though unplaced


def stamp_files() -> list[Path]:
    """The patterns folder's own stamps, plus the Desktop stamp libraries
    (read directly: the junctions that show them to gecko are not followed,
    so nothing is counted twice)."""
    out = sorted(PATTERNS.glob("*.json")) + sorted((PATTERNS / "Custom").glob("*.json"))
    for d in _config.stamp_libraries():
        if d.is_dir():
            out += sorted(d.rglob("*.json"))
    return out
REPORT = ROOT / "build" / "repack_art"

DIR_OF_TYPE = {t: d for t, d in TYPE_NAME.items()}


def fid_dir(fid: int) -> str | None:
    return DIR_OF_TYPE.get((fid >> 24) & 0xF)


def fid_index(fid: int) -> int:
    d = fid_dir(fid)
    return fid & (0xFFF if d == "critters" else 0xFFFFFF)


def with_index(fid: int, idx: int) -> int:
    d = fid_dir(fid)
    mask = 0xFFF if d == "critters" else 0xFFFFFF
    return (fid & ~mask & 0xFFFFFFFF) | idx


def map_files() -> list[Path]:
    out = []
    for top in (DEPLOYED / "maps", AUTHORED / "maps"):
        if top.is_dir():
            out += [p for p in top.rglob("*") if p.is_file()]
    return sorted(out)


def load_map(gi: GameIndex, p: Path) -> MapFile | None:
    try:
        return gi.load_map(p.read_bytes())
    except Exception as e:  # noqa: BLE001 - a non-map file in a maps dir is reported, not fatal
        print(f"  (skipping {p.relative_to(ROOT)}: not a readable map: {e})")
        return None


def all_objects(m: MapFile):
    stack = list(m.objects)
    while stack:
        o = stack.pop()
        stack.extend(o.inventory)
        yield o


class Protos:
    """FOC proto FIDs, read from the authored tree (base protos as fallback)."""

    def __init__(self):
        self.gi = GameIndex()
        self.lsts = {t: lists.proto_lst(self.gi, t) for t in TYPE_NAME}
        self.cache: dict[int, int | None] = {}

    def path(self, pid: int, root: Path) -> Path | None:
        t = pid_type(pid)
        lst = self.lsts.get(t, [])
        i = pid_index(pid) - 1
        if not (0 <= i < len(lst)) or not lst[i]:
            return None
        return root / "proto" / TYPE_NAME[t] / lst[i]

    def fid(self, pid: int) -> int | None:
        if pid in self.cache:
            return self.cache[pid]
        out = None
        p = self.path(pid, AUTHORED)
        if p is not None and p.is_file():
            out = parse_pro(p.read_bytes()).fid
        elif pid_type(pid) in TYPE_NAME:
            pr = self.gi.proto(pid)
            out = pr.fid if pr is not None else None
        self.cache[pid] = out
        return out


def script_pids() -> set[int]:
    """PIDs of FOC protos that a script names via a foc_ids.h define."""
    if not FOC_IDS_H.is_file():
        return set()
    defs = {}
    for m in re.finditer(r"#define\s+(PID_\w+)\s+\((\d+)\)", FOC_IDS_H.read_text(encoding="utf-8")):
        defs[m.group(1)] = int(m.group(2))
    used = set()
    for ssl in (ROOT / "scripts_src").rglob("*.ssl"):
        text = ssl.read_text(encoding="utf-8", errors="replace")
        for name in set(re.findall(r"\bPID_\w+", text)):
            if name in defs:
                used.add(defs[name])
    # protos made for the owner to place (tools/mirror_scenery.py adds them):
    # kept below the cap before any map uses them, so they draw right in game
    if KEEP_PROTOS.is_file():
        for line in KEEP_PROTOS.read_text(encoding="utf-8").splitlines():
            line = line.split("#")[0].strip()
            if line:
                used.add(int(line, 0))
    return used


def collect_used(protos: Protos, gi: GameIndex) -> tuple[dict[str, set[int]], dict[str, int]]:
    used: dict[str, set[int]] = {d: set() for d in ART_KEY}
    counts = {"maps": 0, "stamps": 0, "script_pids": 0}

    def add_fid(fid):
        d = fid_dir(fid) if fid is not None else None
        if d in used:
            used[d].add(fid_index(fid))

    for p in map_files():
        m = load_map(gi, p)
        if m is None:
            continue
        counts["maps"] += 1
        for o in all_objects(m):
            add_fid(o.fid)
            if o.pid != -1:
                add_fid(protos.fid(o.pid))
    for p in stamp_files():
        d = json.loads(p.read_text(encoding="utf-8"))
        counts["stamps"] += 1
        for v in d.get("variants", []):
            for o in v.get("objects", []):
                add_fid(o["frmPid"])
                add_fid(protos.fid(o["proPid"]))
    for pid in script_pids():
        counts["script_pids"] += 1
        add_fid(protos.fid(pid))
    return used, counts


# ------------------------------------------------------------------ registry

def registry_split(path: Path) -> tuple[list[str], list[int]]:
    """(all lines, positions of entry lines) - comment lines keep their place."""
    lines = path.read_text(encoding="utf-8").splitlines()
    entries = [i for i, l in enumerate(lines) if not l.strip().startswith("#")]
    while entries and not lines[entries[-1]].strip():
        entries.pop()                       # trailing blank lines are not entries
    return lines, entries


def plan(used: dict[str, set[int]]) -> dict[str, list[tuple[int, int]]]:
    """Per art dir, the (high, low) index pairs to swap."""
    swaps: dict[str, list[tuple[int, int]]] = {}
    for d, key in ART_KEY.items():
        reg = IDS / f"{key}.txt"
        if not reg.is_file():
            continue
        lines, entries = registry_split(reg)
        start = FOC_START[key] - 1          # art is 0-based: see port.py cmd_art
        idx = [start + k for k in range(len(entries))]
        hi = [i for i in idx if i >= ART_INDEX_CAP and i in used[d]]
        lo = [i for i in idx if i < ART_INDEX_CAP and i not in used[d]]
        if len(hi) > len(lo):
            sys.exit(f"art/{d}: {len(hi)} used entries above {ART_INDEX_CAP} but only "
                     f"{len(lo)} unused slots below it - the used set itself no longer fits")
        # lowest free slots first, so the region just under the cap stays compact
        swaps[d] = list(zip(hi, lo))
    return swaps


def apply_registry(d: str, pairs: list[tuple[int, int]]) -> None:
    key = ART_KEY[d]
    reg = IDS / f"{key}.txt"
    lines, entries = registry_split(reg)
    start = FOC_START[key] - 1
    for a, b in pairs:
        la, lb = entries[a - start], entries[b - start]
        lines[la], lines[lb] = lines[lb], lines[la]
    reg.write_text("\n".join(lines) + "\n", encoding="utf-8")


# ------------------------------------------------------------------ rewriting

def remap_fid(fid: int, table: dict[str, dict[int, int]]) -> int | None:
    d = fid_dir(fid)
    if d not in table:
        return None
    new = table[d].get(fid_index(fid))
    return None if new is None else with_index(fid, new)


def rewrite_protos(table, dry) -> int:
    n = 0
    for root in (AUTHORED / "proto", DEPLOYED / "proto"):
        if not root.is_dir():
            continue
        for p in root.rglob("*.pro"):
            b = bytearray(p.read_bytes())
            if len(b) < 12:
                continue
            fid = struct.unpack_from(">I", b, 8)[0]
            new = remap_fid(fid, table)
            if new is None:
                continue
            struct.pack_into(">I", b, 8, new)
            n += 1
            if not dry:
                p.write_bytes(bytes(b))
    return n


def rewrite_maps(gi, table, dry) -> tuple[int, int]:
    files = objs = 0
    for p in map_files():
        m = load_map(gi, p)
        if m is None:
            continue
        b = bytearray(p.read_bytes())
        changed = 0
        for o in all_objects(m):
            new = remap_fid(o.fid & 0xFFFFFFFF, table)
            if new is None:
                continue
            struct.pack_into(">I", b, o.fid_offset, new)
            changed += 1
        if changed:
            files += 1
            objs += changed
            if not dry:
                # Both trees get byte-identical rewrites, so deploy.py's sync-back
                # (content first, then mtime) still sees them as in sync. Do NOT
                # restore the old mtime: same size + same mtime hides the change
                # from git's stat cache, and the rewrite never gets committed.
                p.write_bytes(bytes(b))
    return files, objs


def rewrite_stamps(table, dry) -> tuple[int, int]:
    files = objs = 0
    for p in stamp_files():
        d = json.loads(p.read_text(encoding="utf-8"))
        changed = 0
        for v in d.get("variants", []):
            for o in v.get("objects", []):
                new = remap_fid(o["frmPid"], table)
                if new is not None:
                    o["frmPid"] = new
                    changed += 1
        if changed:
            files += 1
            objs += changed
            if not dry:
                p.write_text(json.dumps(d, separators=(",", ":")) + "\n", encoding="utf-8")  # NOSONAR - the stamp goes back to the file it was read from
    return files, objs


def rewrite_ledger(table, dry) -> int:
    rows = port.read_ledger()
    n = 0
    for r in rows:
        if r["kind"] != "art":
            continue
        m = re.search(r"art[/\\]([a-z]+)[/\\]", r["dest_path"].lower())
        if not m or m.group(1) not in table:
            continue
        try:
            old = int(r["dest_id"])
        except ValueError:
            continue
        new = table[m.group(1)].get(old)
        if new is not None:
            r["dest_id"] = str(new)
            n += 1
    if n and not dry:
        with port.LEDGER.open("w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=port.LEDGER_FIELDS)
            w.writeheader()
            for r in rows:
                w.writerow({k: r.get(k, "") for k in port.LEDGER_FIELDS})
        port.regen_ledger_md()
    return n


def write_deployed_lists(dirs, dry) -> None:
    gi = GameIndex()
    for d in dirs:
        dst = DEPLOYED / "art" / d / f"{d}.lst"
        if not dry:
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(lists.render_lst(lists.art_lst(gi, d)))


# ----------------------------------------------------------------------- main

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    gi = GameIndex(pristine=False)
    protos = Protos()
    used, counts = collect_used(protos, gi)
    print(f"scanned {counts['maps']} map files, {counts['stamps']} stamps, "
          f"{counts['script_pids']} script-named protos")

    swaps = plan(used)
    table: dict[str, dict[int, int]] = {}
    for d, pairs in swaps.items():
        key = ART_KEY[d]
        n_reg = len(registry_split(IDS / f"{key}.txt")[1])
        start = FOC_START[key] - 1
        foc_used = sum(1 for i in used[d] if i >= start)
        print(f"  art/{d}: {n_reg} FOC entries, {foc_used} used, "
              f"{len(pairs)} used entr{'y' if len(pairs) == 1 else 'ies'} above {ART_INDEX_CAP} to move down")
        if pairs:
            t = {}
            for a, b in pairs:
                t[a], t[b] = b, a
            table[d] = t
    if not table:
        print("nothing to do: every used art entry is already below the cap")
        return 0

    n_pro = rewrite_protos(table, True)
    n_mapf, n_mapo = rewrite_maps(gi, table, True)
    n_stf, n_sto = rewrite_stamps(table, True)
    print(f"would rewrite: {n_pro} proto FIDs, {n_mapo} object FIDs in {n_mapf} map files, "
          f"{n_sto} stamp objects in {n_stf} stamps")
    if args.dry_run:
        print("--dry-run: nothing written")
        return 0

    for d, pairs in swaps.items():
        if pairs:
            apply_registry(d, pairs)
    n_pro = rewrite_protos(table, False)
    n_mapf, n_mapo = rewrite_maps(gi, table, False)
    n_stf, n_sto = rewrite_stamps(table, False)
    n_led = rewrite_ledger(table, False)
    write_deployed_lists(table.keys(), False)

    REPORT.mkdir(parents=True, exist_ok=True)
    out = REPORT / f"{_dt.datetime.now():%Y%m%d_%H%M%S}.json"
    out.write_text(json.dumps({d: {str(k): v for k, v in t.items()} for d, t in table.items()},
                              indent=1), encoding="utf-8")
    print(f"rewrote {n_pro} proto FIDs, {n_mapo} object FIDs in {n_mapf} map files, "
          f"{n_sto} stamp objects in {n_stf} stamps, {n_led} ledger rows")
    print(f"old->new table: {out.relative_to(ROOT)}")
    print("Next: python tools/validate.py")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
