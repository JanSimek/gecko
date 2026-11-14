#!/usr/bin/env python3
"""
fix_art_names.py - bring every FOC art filename within the engine's 12-char limit.

    python tools/fix_art_names.py [--apply]

WHY THIS EXISTS
  art.cc::artReadList (0x419664 in fallout2.exe, unpatched by sfall 4.5) reads
  each art .lst line with strncpy(dst, line, 12) into a 13-byte slot. Any name
  longer than 12 characters is silently cut to 12, so "nevada_fcapcan.frm"
  becomes "nevada_fcapc", which does not exist, art_ptr_lock() returns NULL and
  the object is invisible - or, when it shares a tile with another object,
  _obj_insert (0x48d9c4) dereferences the NULL Art* and the game dies with
  "referenced memory at 00000014". Gecko does not truncate, which is why the
  same map looks right in the editor and crashes in the game.

  port.py used to prefix every ported file with its full source name
  ("sonora_", "olympus_", ...) which blew straight past the limit for all
  8,840 imported items/scenery/walls entries.

WHAT IT DOES
  A FID is an INDEX into the .lst, so no proto, map or script needs to change -
  only the names. For every registry entry that is over 12 characters (or that
  shadows a base-game file of the same name) it:
    1. picks a <=12-char name:  <1-char source tag><first 7 chars of the base>.frm
       (sonora->s, nevada->n, olympus->o, resurrection->r), disambiguating
       collisions with a base-36 counter, checked against the base game's own
       list, every other FOC entry and the files already on disk;
    2. renames the file in mod/data/art/<dir>/ AND base/mods/foc/art/<dir>/;
    3. rewrites mod/ids/art_<dir>.txt line-for-line (indices never move);
    4. regenerates base/mods/foc/art/<dir>/<dir>.lst with deploy.py's renderer;
    5. appends old->new rows to mod/ids/art_renames.csv.
  Without --apply it only prints what it would do.
"""
from __future__ import annotations

import argparse
import csv
import shutil
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import lists  # noqa: E402
from foclib import GameIndex  # noqa: E402
from ids import ART_KEY, ENGINE_ART_NAME_MAX, SOURCE_TAG, engine_art_name  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
MOD = ROOT / "mod"
IDS = MOD / "ids"
SRC_ART = MOD / "data" / "art"
DEPLOYED_ART = ROOT / "base" / "mods" / "foc" / "art"
RENAMES = IDS / "art_renames.csv"
BACKUPS = ROOT / "_safety_backups"

DIRS = ["items", "scenery", "walls"]   # critters already fit (6-char bases)


def split_source(name: str) -> tuple[str, str]:
    """'olympus_tower3.frm' -> ('olympus', 'tower3.frm'); 'char06.frm' -> ('', 'char06.frm')."""
    head, sep, rest = name.partition("_")
    if sep and head.lower() in SOURCE_TAG:
        return head.lower(), rest
    return "", name


def registry_lines(path: Path) -> list[str]:
    return path.read_text(encoding="utf-8", errors="replace").splitlines()


def plan_dir(art_dir: str, base_gi: GameIndex) -> list[tuple[int, str, str]]:
    """[(registry line number, old name, new name)] for one art dir."""
    reg = IDS / f"{ART_KEY[art_dir]}.txt"
    lines = registry_lines(reg)
    base_names = {n.lower() for n in lists.base_art_lst(base_gi, art_dir) if n}
    on_disk = {p.name.lower() for p in (SRC_ART / art_dir).iterdir()} if (SRC_ART / art_dir).is_dir() else set()
    taken = set(base_names) | on_disk
    entries = [(i, l.strip()) for i, l in enumerate(lines) if l.strip() and not l.strip().startswith("#")]
    # names that already fit and shadow nothing keep their slot untouched
    keep = {n.lower() for _, n in entries
            if len(n) <= ENGINE_ART_NAME_MAX and n.lower() not in base_names}
    taken |= keep
    plan = []
    for i, name in entries:
        if name.lower() in keep:
            continue
        source, original = split_source(name)
        new = engine_art_name(source, original, taken)
        taken.add(new)
        plan.append((i, name, new))
    return plan


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--apply", action="store_true", help="actually rename/rewrite (default: dry run)")
    args = ap.parse_args()

    base_gi = GameIndex()                       # pristine: the base list our indices sit on
    plans = {d: plan_dir(d, base_gi) for d in DIRS}

    problems = 0
    for d, plan in plans.items():
        print(f"{d}: {len(plan)} entries to rename")
        for _, old, new in plan[:4]:
            print(f"    {old:28s} -> {new}")
        if len(plan) > 4:
            print("    ...")
        for _, old, new in plan:
            if len(new) > ENGINE_ART_NAME_MAX:
                print(f"  !! {new} still too long"); problems += 1
            for tree in (SRC_ART, DEPLOYED_ART):
                if not (tree / d / old).is_file():
                    print(f"  !! missing file {tree / d / old}"); problems += 1
                if (tree / d / new).exists():
                    print(f"  !! target already exists {tree / d / new}"); problems += 1
        news = [n for _, _, n in plan]
        if len(set(news)) != len(news):
            print("  !! duplicate targets"); problems += 1
    if problems:
        print(f"\n{problems} problem(s) - nothing changed")
        return 1
    if not args.apply:
        print("\ndry run only - rerun with --apply")
        return 0

    stamp = time.strftime("%Y%m%d_%H%M%S")
    bdir = BACKUPS / f"artnames_{stamp}"
    bdir.mkdir(parents=True, exist_ok=True)
    for d in DIRS:
        shutil.copy2(IDS / f"{ART_KEY[d]}.txt", bdir)
        lst = DEPLOYED_ART / d / f"{d}.lst"
        if lst.is_file():
            shutil.copy2(lst, bdir / f"{d}.lst")
    print(f"\nbackups of registries + .lst -> {bdir}")

    new_rows = []
    for d, plan in plans.items():
        # 1. files, both trees
        for _, old, new in plan:
            for tree in (SRC_ART, DEPLOYED_ART):
                (tree / d / old).rename(tree / d / new)
        # 2. registry, line for line
        reg = IDS / f"{ART_KEY[d]}.txt"
        lines = registry_lines(reg)
        for i, old, new in plan:
            assert lines[i].strip() == old
            lines[i] = new
        reg.write_text("\n".join(lines) + "\n", encoding="utf-8")
        # 3. deployed .lst, exactly as deploy.py renders it
        before = (DEPLOYED_ART / d / f"{d}.lst").read_bytes().count(b"\n")
        payload = lists.render_lst(lists.art_lst(base_gi, d))
        assert payload.count(b"\n") == before, (d, before, payload.count(b"\n"))
        (DEPLOYED_ART / d / f"{d}.lst").write_bytes(payload)
        new_rows += [(stamp, d, old, new) for _, old, new in plan]
        print(f"{d}: renamed {len(plan)} files in both trees, registry + {d}.lst rewritten")

    # 4. traceability
    write_header = not RENAMES.exists()
    with RENAMES.open("a", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        if write_header:
            w.writerow(["when", "dir", "old", "new"])
        w.writerows(new_rows)
    print(f"{len(new_rows)} renames recorded in {RENAMES.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
