#!/usr/bin/env python3
"""
sync_maps.py - copy a map you just edited in gecko back into the authored
source tree, so it survives the next deploy.py run.

    python tools/sync_maps.py REDWELL.MAP
    python tools/sync_maps.py --all
    python tools/sync_maps.py --all --dry-run

WHY THIS EXISTS
    gecko edits base\\mods\\foc\\maps\\<NAME> (the deployed copy - that's what's
    mounted). deploy.py copies mod\\data\\maps\\<NAME> -> base\\mods\\foc\\maps\\<NAME>
    VERBATIM every time it runs (see deploy.py's own docstring: "mod\\data\\** -
    maps, .msg, .pro, art, sound - copied verbatim"). So a real gecko edit that
    only exists in the deployed copy is silently overwritten and lost the next
    time deploy.py runs for any reason (porting more assets, a script rebuild).

    mod\\data\\maps\\ is the authored source of truth this project is built
    around. Run this after any gecko session whose changes you want to keep,
    before running deploy.py again.

--all syncs every map that differs (by content, not just by name) between the
two trees; a map that's only in one tree is skipped and reported, not guessed
at, since a name only on the deployed side could be a leftover from a manual
test rather than something you want promoted into authored content.
"""
from __future__ import annotations

import argparse
import filecmp
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEPLOYED = ROOT / "base" / "mods" / "foc" / "maps"
AUTHORED = ROOT / "mod" / "data" / "maps"


def sync_one(name: str, dry_run: bool) -> str:
    src = DEPLOYED / name
    dst = AUTHORED / name
    if not src.is_file():
        return f"  SKIP {name}: not in deployed maps ({src})"
    if dst.is_file() and filecmp.cmp(src, dst, shallow=False):
        return f"  ok   {name}: already in sync"
    action = "would copy" if dry_run else "copied"
    if not dry_run:
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
    return f"  {action:11s} {name}  ({src.stat().st_size} bytes)"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="*", help="map filename(s), e.g. REDWELL.MAP")
    ap.add_argument("--all", action="store_true", help="sync every map present in both trees")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if not args.all and not args.names:
        sys.exit("give map name(s), or --all")

    if args.all:
        deployed_names = {p.name for p in DEPLOYED.glob("*") if p.is_file()}
        authored_names = {p.name for p in AUTHORED.glob("*") if p.is_file()}
        only_deployed = deployed_names - authored_names
        names = sorted(deployed_names & authored_names)
        for n in sorted(only_deployed):
            print(f"  SKIP {n}: only in deployed maps, not in authored - sync by name if intentional")
    else:
        names = args.names

    for name in names:
        print(sync_one(name, args.dry_run))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
