#!/usr/bin/env python3
"""
deploy.py - assemble the mod into the working base install so it can be played.

    python tools/deploy.py [--clean] [--dry-run] [--verbose]

Writes into base\\mods\\foc\\ as a FOLDER MOD. sfall loads folders listed in
mods_order.txt exactly like .dat files (LoadOrder.cpp), and later lines win, so
listing "foc" after "rpu.dat" puts our content on top of RPU.

Deliberately NOT base\\data\\. That is the highest-priority layer in the chain,
so anything written there is read back as part of the BASE next time the ID
baseline is computed - our own additions would be counted as base entries and
every later allocation would shift by however much we had deployed. Keeping our
output in its own named layer lets GameIndex(pristine=True) exclude it cleanly.

The base is OUR copy, built by tools/make-base.ps1. The original install under
The install you copied it from is never touched by anything in this project.

What gets written:
  * generated scripts.lst   base entries + filler + FOC entries
  * generated vault13.gam   same scheme, slot-for-slot
  * generated proto .lst    per type, only when the mod allocates that type
  * generated data\\maps.txt     base maps + filler + FOC map sections
  * generated data\\city.txt     base areas + FOC area sections
  * generated map.msg            base + FOC map/elevation/area names
  * generated data\\quests.txt   FOC's Pip-Boy quest log (tools/questlog.py)
  * build\\scripts\\*.int     the compiled scripts
  * mod\\data\\**             maps, .msg, .pro, art, sound - copied verbatim

Every file written is recorded in the layer's _foc_manifest.txt, so --clean
removes exactly what we put there and nothing else.
"""

from __future__ import annotations

import argparse
import filecmp
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import lists  # noqa: E402
import questlog  # noqa: E402
import worldmap  # noqa: E402
from foclib.paths import inside  # noqa: E402
from foclib import FOC_LAYER, GameIndex, TYPE_NAME  # noqa: E402
from gen_ids import read_registry  # noqa: E402
from ids import ART_KEY, PROTO_KEY  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
MOD = ROOT / "mod"
BUILD = ROOT / "build" / "scripts"
MANIFEST_NAME = "_foc_manifest.txt"


class Deployer:
    def __init__(self, base: Path, dry: bool, verbose: bool):
        self.data = base / "mods" / FOC_LAYER
        self.dry = dry
        self.verbose = verbose
        self.written: list[str] = []

    def _record(self, rel: str, note: str):
        self.written.append(rel)
        if self.verbose:
            print(f"  {note:<10s} mods\\{FOC_LAYER}\\{rel}")

    def write_bytes(self, rel: str, payload: bytes, note="generated"):
        dst = self.data / rel
        if not self.dry:
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(payload)
        self._record(rel, note)

    def copy(self, src: Path, rel: str, note="copied"):
        dst = self.data / rel
        if not self.dry:
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
        self._record(rel, note)


def read_manifest(data: Path) -> list[str]:
    p = data / MANIFEST_NAME
    if not p.is_file():
        return []
    return [l.strip() for l in p.read_text(encoding="utf-8").splitlines()
            if l.strip() and not l.startswith("#")]


def clean(data: Path, dry: bool, verbose: bool) -> int:
    entries = read_manifest(data)
    if not entries:
        print("nothing to clean (no manifest)")
        return 0
    removed = 0
    for rel in entries:
        p = data / rel
        if p.is_file():
            if not dry:
                p.unlink()
            removed += 1
            if verbose:
                print(f"  removed   mods\\{FOC_LAYER}\\{rel}")
    # prune directories that are now empty, deepest first
    if not dry:
        for d in sorted({(data / r).parent for r in entries},
                        key=lambda x: len(x.parts), reverse=True):
            try:
                if d.is_dir() and not any(d.iterdir()):
                    d.rmdir()
            except OSError:
                pass
        mp = data / MANIFEST_NAME
        if mp.is_file():
            mp.unlink()
    print(f"cleaned {removed} previously deployed file(s)")
    return 0


def sync_edited_maps_back(base: Path, dry_run: bool) -> int:
    """
    Copy any deployed map gecko has edited back into mod\\data\\maps BEFORE this
    run's clean+copy would overwrite it with the (possibly stale) authored copy.

    gecko edits base\\mods\\foc\\maps\\<NAME> directly - that is the mounted,
    live copy, not mod\\data\\maps\\<NAME>. Every deploy run below unconditionally
    deletes the deployed copy (clean()) and then overwrites it from mod\\data
    (authored content) - if a gecko edit was never synced back first, this is
    exactly how it gets silently destroyed. Confirmed to have actually happened
    (lost real edits from an active gecko session) before this check existed.
    Runs unconditionally, every deploy, so remembering a separate sync step is
    never required for this specific loss to be possible again.
    """
    deployed_dir = base / "mods" / FOC_LAYER / "maps"
    authored_dir = MOD / "data" / "maps"
    if not deployed_dir.is_dir() or not authored_dir.is_dir():
        return 0

    deployed_names = {p.name for p in deployed_dir.glob("*") if p.is_file()}
    authored_names = {p.name for p in authored_dir.glob("*") if p.is_file()}

    synced = 0
    for name in sorted(deployed_names & authored_names):
        src = deployed_dir / name
        dst = authored_dir / name
        if filecmp.cmp(src, dst, shallow=False):
            continue
        # Only pull the DEPLOYED side forward when it's actually the newer one. Content
        # differing isn't enough on its own - mod\data is also a legitimate place to fix a
        # map directly (by hand, or via port.py/gecko-cli), and that source edit needs to
        # flow OUTWARD to the deployed copy on the next deploy, not get clobbered by an
        # older deployed file just because the bytes don't match.
        if src.stat().st_mtime <= dst.stat().st_mtime:
            print(f"  NOTE: {name} differs from mod\\data but the deployed copy isn't newer - "
                  f"leaving it alone (mod\\data will overwrite it, as normal)")
            continue
        synced += 1
        action = "would sync back" if dry_run else "synced back"
        print(f"  {action}: {name} has unsaved gecko edits not yet in mod\\data - "
              f"copying deployed -> authored before deploying ({src.stat().st_size} bytes)")
        if not dry_run:
            shutil.copy2(src, dst)

    only_deployed = deployed_names - authored_names
    for name in sorted(only_deployed):
        print(f"  WARNING: {deployed_dir / name} has no counterpart in mod\\data\\maps - "
              f"not auto-added (could be a stray test file); run "
              f"`python tools/sync_maps.py {name}` if this should become authored content")

    if synced:
        print(f"  ({synced} map(s) synced back - see mod/data/maps)\n")
    return synced


def ensure_load_order(base: Path) -> None:
    """
    Guarantee `foc` is the LAST enabled line of mods_order.txt.

    Order is priority: LoadOrder.cpp pushes each entry onto a vector and notes
    that "Patches placed at the back of the vector will have priority in the
    chain over the front(previous) patches". Last line therefore wins, and our
    layer must override RPU rather than the other way round.
    """
    path = base / "mods" / "mods_order.txt"
    lines = []
    if path.is_file():
        lines = path.read_text(encoding="cp1252", errors="replace").splitlines()

    kept = [l for l in lines if l.strip().lower() != FOC_LAYER]
    while kept and not kept[-1].strip():
        kept.pop()
    kept.append(FOC_LAYER)

    new = "\n".join(kept) + "\n"
    if not path.is_file() or path.read_text(encoding="cp1252", errors="replace") != new:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(new, encoding="cp1252")
        print(f"mods_order.txt: '{FOC_LAYER}' is now the last (highest priority) entry")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--clean", action="store_true",
                    help="remove previously deployed files and stop")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--base", type=Path, default=None)
    args = ap.parse_args()

    gi = GameIndex(inside(args.base) if args.base else None)          # pristine: the baseline our IDs sit on
    data = gi.base / "mods" / FOC_LAYER

    # Before anything else touches the deployed tree: pull forward any map gecko
    # edited there that mod\data doesn't have yet. Must run even for --clean,
    # since clean() is exactly the step that deletes the deployed copy outright.
    print("checking for unsynced gecko map edits...")
    sync_edited_maps_back(gi.base, args.dry_run)

    if args.clean:
        return clean(data, args.dry_run, args.verbose)

    # A stale deploy must not survive a rename: clear the old set first.
    old = read_manifest(data)
    if old and not args.dry_run:
        clean(data, False, args.verbose)

    d = Deployer(gi.base, args.dry_run, args.verbose)

    # --- generated lists ---------------------------------------------------
    try:
        d.write_bytes("scripts\\scripts.lst",
                      lists.render_lst(lists.scripts_lst_text(gi)))
        d.write_bytes("data\\vault13.gam",
                      lists.render_gam(lists.gvar_names(gi)))
        for t, key in PROTO_KEY.items():
            if read_registry(MOD / "ids" / f"{key}.txt"):
                name = TYPE_NAME[t]
                d.write_bytes(f"proto\\{name}\\{name}.lst",
                              lists.render_lst(lists.proto_lst(gi, t)))
        # Art indices are a SEPARATE positional space from proto indices, and a
        # proto's FID points into it. Allocate art without shipping the matching
        # .lst and the engine renders whatever the base happens to have at that
        # line - which usually looks almost right, the worst kind of wrong.
        for art_dir, art_key in ART_KEY.items():
            if read_registry(MOD / "ids" / f"{art_key}.txt") or (art_dir == "tiles" and lists.tile_slots()):
                d.write_bytes(f"art\\{art_dir}\\{art_dir}.lst",
                              lists.render_lst(lists.art_lst(gi, art_dir)))
    except lists.Overflow as e:
        print(f"ERROR: {e}")
        print("       the base has grown past a FOC start; run tools/ids.py")
        return 1

    # --- generated worldmap registration -----------------------------------
    # maps.txt / city.txt / map.msg REPLACE whatever the base supplies (the foc
    # layer wins the override chain), so they carry the full base content plus
    # our appends. A map with no maps.txt section is unreachable from the
    # worldmap; a map with no city.txt area has no marker or entrance.
    try:
        d.write_bytes("data\\maps.txt",
                      worldmap.merged_maps_txt(gi).encode("cp1252", "replace"))
        d.write_bytes("data\\city.txt",
                      worldmap.merged_city_txt(gi).encode("cp1252", "replace"))
        d.write_bytes("text\\english\\game\\map.msg",
                      worldmap.merged_map_msg(gi).encode("cp1252", "replace"))
        if worldmap._config.own_worldmap():
            d.write_bytes("data\\worldmap.txt",
                          worldmap.merged_worldmap_txt(gi).encode("cp1252", "replace"))
            d.write_bytes("text\\english\\game\\worldmap.msg",
                          worldmap.merged_worldmap_msg(gi).encode("cp1252", "replace"))
    except worldmap.WorldmapError as e:
        print(f"ERROR: {e}")
        return 1

    # --- generated Pip-Boy quest log -----------------------------------------
    # FOC's entries only (the base's are vanilla quests); the text is
    # mod\data\text\english\game\quests.msg, copied below with the rest.
    try:
        quests_txt, problems = questlog.build(gi)
    except (questlog.QuestLogError, worldmap.WorldmapError) as e:
        problems = [str(e)]
    if problems:
        for p in problems:
            print(f"ERROR: {p}")
        return 1
    d.write_bytes(questlog.QUESTS_VFS, quests_txt.encode("cp1252", "replace"))

    # --- compiled scripts --------------------------------------------------
    if BUILD.is_dir():
        for p in sorted(BUILD.glob("*.int")):
            d.copy(p, f"scripts\\{p.name}")

    # --- authored content --------------------------------------------------
    mod_data = MOD / "data"
    if mod_data.is_dir():
        for p in sorted(mod_data.rglob("*")):
            if not p.is_file():
                continue
            rel = p.relative_to(mod_data)
            # art file names go out in lower case: the game doesn't care, but
            # gecko indexes a loose data folder by exact name and asks for
            # lower case (art/critters/mahogxaa.frm), so MAHOGXAA.FRM is
            # invisible to it
            if rel.parts[0].lower() == "art":
                rel = rel.with_name(rel.name.lower())
            d.copy(p, str(rel).replace("/", "\\"))

    # --- alternative-format music --------------------------------------------
    # sfall's mp3/wma/wav substitution for an .acm looks on the real disk in
    # fallout2.cfg's music_path1/2 (data\sound\music\), NOT through the mod
    # layers, so a track in mods\foc\sound\music is never found. Mirror them
    # there (idempotent; these are our files by name, so nothing is cleaned).
    music_src = mod_data / "sound" / "music"
    if music_src.is_dir() and not args.dry_run:
        music_dst = gi.base / "data" / "sound" / "music"
        music_dst.mkdir(parents=True, exist_ok=True)
        for p in sorted(music_src.iterdir()):
            if p.suffix.lower() in (".mp3", ".wma", ".wav"):
                shutil.copy2(p, music_dst / p.name)
                print(f"music: {p.name} -> data/sound/music")

    # --- make sure sfall actually loads the layer --------------------------
    if not args.dry_run:
        ensure_load_order(gi.base)

    # --- manifest ----------------------------------------------------------
    if not args.dry_run:
        body = ["# Files deployed by tools/deploy.py. Used by --clean.",
                "# Do not edit by hand.", *sorted(d.written)]
        (data / MANIFEST_NAME).write_text("\n".join(body) + "\n", encoding="utf-8")

    print(f"\ndeployed {len(d.written)} file(s) into {data}")
    if args.dry_run:
        print("--dry-run: nothing was written.")
    else:
        print(f"manifest: mods\\{FOC_LAYER}\\{MANIFEST_NAME}")
        print(f"\nRun it:  cd /d \"{gi.base}\" && fallout2.exe")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())