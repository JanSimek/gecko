#!/usr/bin/env python3
"""
port.py - pull assets in from another Fallout 2 install and remap them into
this project's ID space.

    python tools/port.py sources
    python tools/port.py list  <source> [pattern] [--limit N]
    python tools/port.py file  <source> <game\\path> [...]        verbatim copy
    python tools/port.py art   <source> <art\\dir\\file.frm> [...]  copy + allocate index
    python tools/port.py proto <source> <PID|hex> [...]           copy + remap PID + art
    python tools/port.py map   <source> <NAME.MAP>                copy + remap everything
    python tools/port.py ledger                                   regenerate ASSETS.md

WHY REMAPPING IS NOT OPTIONAL
-----------------------------
Nothing in Fallout 2 is identified by name. A proto is "line N of items.lst",
a script is "line N of scripts.lst", a GVAR is "the Nth line of vault13.gam",
and a piece of art is "line N of art\\scenery\\scenery.lst". Two mods that both
add content occupy the same N for different things. So a .map lifted straight
out of Sonora will render Sonora's scenery indices against OUR scenery list and
produce garbage - or, worse, something that looks almost right.

Porting therefore means: copy the bytes, allocate fresh indices at the FOC end
of each list, and patch every embedded reference to point at the new numbers.

EVERY port is written to the ledger (mod/assets.csv), because reusing another
project's work in a public release means crediting - and sometimes asking - the
people who made it. Reconstructing that later from memory is not possible.

Source installs are READ-ONLY. Nothing here ever writes outside this project.
"""

from __future__ import annotations

import argparse
import csv
import datetime as _dt
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib import (  # noqa: E402
    GameIndex,
    TYPE_NAME,
    parse_lst,
    parse_msg,
    parse_pro,
    pid_index,
    pid_type,
)
from ids import ART_INDEX_CAP, ART_KEY, FOC_START, PROTO_KEY, check_art_name, engine_art_name  # noqa: E402

# Proto display names come from a message-file lookup keyed by the proto's own
# message_id (see gecko's ObjectPalettePanel::protoDisplayName). A ported proto
# used to keep the SOURCE's raw message_id unpatched, which either resolved to
# nothing (raw filename shown instead) or - worse - coincidentally matched an
# UNRELATED existing base/RPU entry at that same number, showing a wrong,
# misleading name (confirmed: a ported scenery proto showed as "Couch"). Fixed
# by always allocating a fresh, collision-free message_id in FOC's own space
# and copying the real name text over from the source's own message file.
MSG_FILE_FOR_TYPE = {
    "items": "pro_item.msg",
    "critters": "pro_crit.msg",
    "scenery": "pro_scen.msg",
    "walls": "pro_wall.msg",
}
# Base names FOC renames in place, by file and message id. A vanilla proto
# that FOC uses for one named character shows that character's name in the
# party and combat text.
BASE_MSG_OVERRIDES = {
    "pro_crit.msg": {
        9500: "Kilgore",        # male Trapper, proto 95: only Kilgore wears it
    },
}
# Comfortably above the highest ID any of these files use today (max observed:
# scenery 233400) - not tied to base's own range the way proto/art indices are,
# since message ids are looked up by exact key, not by array position, so no
# "filler gap" is needed, just a number RPU/base will never plausibly reach.
FOC_START_MSG = 5_000_000

# HARD ENGINE LIMIT. A FID packs the art index into bits 0-11 only:
#     art.cc artBuildFilePath:  v3 = fid & 0xFFF
#     art.cc buildFid:          ... | (frmId & 0xFFF)
# so an art index of 4096 or more is NOT ADDRESSABLE - the engine silently reads
# `index & 0xFFF` instead and draws a completely unrelated sprite. This is a
# property of the FID format, not a tunable: the remaining bits are spoken for
# (12-15 weapon code, 16-23 anim type, 24-27 object type, 28-30 rotation), and
# fallout2-ce has the same limit. sfall widens it to 14 bits for TILES ONLY
# (Tiles.cpp art_id_hack, `MoreTiles`); scenery/walls/items get nothing.
#
# Ignoring this is how ~3800 ported scenery entries ended up unreachable and
# rendering as random base-game art (confirmed in-game: a ported crate box
# drew as chdflag3.frm, a flag). The lists now hold more ported art than fits,
# on purpose: tools/repack_art.py keeps everything a map, stamp or script uses
# below the cap and parks the rest of the palette above it, and validate.py
# fails if anything used ends up past it. So a new allocation above the cap is
# allowed - it just has to be repacked once something uses it.


def check_art_index(idx: int, art_dir: str, name: str) -> None:
    """Flag an art index the engine cannot address until it is repacked."""
    if idx >= ART_INDEX_CAP:
        print(f"      note: art\\{art_dir}\\{name} got index {idx}, above the engine's "
              f"{ART_INDEX_CAP} cap - run tools/repack_art.py once a map or stamp uses it")


ROOT = Path(__file__).resolve().parent.parent
MOD = ROOT / "mod"
IDS = MOD / "ids"
SOURCES_FILE = ROOT / "tools" / "sources.json"
from foclib import config as _config  # noqa: E402
LEDGER = MOD / "assets.csv"
LEDGER_MD = ROOT / "ASSETS.md"

LEDGER_FIELDS = ["date", "source", "kind", "source_path", "source_id",
                 "dest_path", "dest_id", "notes"]


# ------------------------------------------------------------------- sources

def load_sources() -> dict[str, dict]:
    if not SOURCES_FILE.is_file():
        return {}
    raw = json.loads(SOURCES_FILE.read_text(encoding="utf-8"))
    # keys starting with '_' are documentation blocks, not source entries
    srcs = {k: v for k, v in raw.items() if not k.startswith("_")}
    # workflow/config.json says where each game is on THIS machine
    return _config.apply_sources(srcs)


def open_source(name: str) -> GameIndex:
    srcs = load_sources()
    if name not in srcs:
        sys.exit(f"unknown source '{name}'. Known: {', '.join(sorted(srcs)) or '(none)'}\n"
                 f"Add it to {SOURCES_FILE.relative_to(ROOT)}")
    path = Path(srcs[name]["path"])
    if not path.is_dir():
        sys.exit(f"source '{name}' path does not exist: {path}")
    # pristine=False: we want the source exactly as IT resolves things
    return GameIndex(path, pristine=False)


# -------------------------------------------------------------------- ledger

def read_ledger() -> list[dict]:
    if not LEDGER.is_file():
        return []
    with LEDGER.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def append_ledger(rows: list[dict]) -> None:
    """
    Append rows, skipping any that duplicate an existing import.

    Re-porting the same thing is a normal thing to do (a source got updated, or
    a previous run was interrupted). It must not grow the ledger each time, or
    the credits file ends up claiming we imported one FRM nine times.
    """
    if not rows:
        return
    existing = read_ledger()
    seen = {(r["source"], r["kind"], r["source_path"].lower(), r["source_id"].lower())
            for r in existing}
    rows = [r for r in rows
            if (r["source"], r["kind"], r["source_path"].lower(),
                r["source_id"].lower()) not in seen]
    if not rows:
        return
    LEDGER.parent.mkdir(parents=True, exist_ok=True)
    with LEDGER.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=LEDGER_FIELDS)
        w.writeheader()
        for r in existing + rows:
            w.writerow({k: r.get(k, "") for k in LEDGER_FIELDS})


def ledger_row(source, kind, source_path, source_id, dest_path, dest_id, notes=""):
    return {
        "date": _dt.date.today().isoformat(),
        "source": source, "kind": kind,
        "source_path": source_path, "source_id": str(source_id),
        "dest_path": dest_path, "dest_id": str(dest_id),
        "notes": notes,
    }


def regen_ledger_md() -> None:
    rows = read_ledger()
    srcs = load_sources()
    out = ["# Imported assets", "",
           "Every asset in this mod that came from somewhere else, with its",
           "original ID and the ID it was remapped to. Generated from",
           "`mod/assets.csv` by `python tools/port.py ledger` - do not edit by hand.",
           "",
           "Reusing another project's work in a public release means crediting the",
           "original authors, and for some projects asking them first. This table",
           "is what makes that possible later.",
           ""]

    if srcs:
        out += ["## Sources", "",
                "| Name | Project | Path | Credit / licence notes |",
                "|---|---|---|---|"]
        for k, v in sorted(srcs.items()):
            out.append(f"| `{k}` | {v.get('project','?')} | `{v.get('path','')}` | "
                       f"{v.get('credit','**TODO**')} |")
        out.append("")

    if not rows:
        out += ["## Imports", "", "_Nothing imported yet._", ""]
    else:
        by_src: dict[str, list[dict]] = {}
        for r in rows:
            by_src.setdefault(r["source"], []).append(r)
        out += ["## Imports", "", f"{len(rows)} asset(s) from {len(by_src)} source(s).", ""]
        for src in sorted(by_src):
            rs = by_src[src]
            out += [f"### {src} ({len(rs)})", "",
                    "| Date | Kind | Source | Orig ID | Destination | New ID | Notes |",
                    "|---|---|---|---|---|---|---|"]
            for r in rs:
                out.append(
                    f"| {r['date']} | {r['kind']} | `{r['source_path']}` | "
                    f"{r['source_id']} | `{r['dest_path']}` | {r['dest_id']} | "
                    f"{r['notes']} |")
            out.append("")

    LEDGER_MD.write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"wrote {LEDGER_MD.relative_to(ROOT)} ({len(rows)} import(s))")


# ------------------------------------------------------------------ registry

def registry_lines(path: Path) -> list[str]:
    if not path.is_file():
        return []
    return path.read_text(encoding="utf-8").splitlines()


def registry_entries(path: Path) -> list[str]:
    return [l.strip() for l in registry_lines(path) if not l.strip().startswith("#")]


def allocate(registry: Path, name: str, start: int) -> int:
    """Append `name` to a registry file and return its absolute index."""
    entries = registry_entries(registry)
    while entries and entries[-1] == "":
        entries.pop()
    for i, e in enumerate(entries):
        if e.lower() == name.lower():
            return start + i          # already allocated - idempotent
    lines = registry_lines(registry)
    while lines and not lines[-1].strip():
        lines.pop()
    lines.append(name)
    registry.parent.mkdir(parents=True, exist_ok=True)
    registry.write_text("\n".join(lines) + "\n", encoding="utf-8")  # NOSONAR - rewrites the registry it just read
    return start + len(entries)


def msg_registry_path(art_dir: str) -> Path:
    return IDS / f"msg_{art_dir}.txt"


def msg_desc_path(art_dir: str) -> Path:
    """Optional examine-descriptions, keyed by NAME (not position) so the
    positional name registry above stays untouched: one 'name<TAB>description'
    per line. A name with no line here simply has no description."""
    return IDS / f"msg_{art_dir}_desc.txt"


def read_msg_descs(art_dir: str) -> dict[str, str]:
    p = msg_desc_path(art_dir)
    if not p.exists():
        return {}
    out: dict[str, str] = {}
    for line in p.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#") or "\t" not in line:
            continue
        name, desc = line.split("\t", 1)
        out[name.strip().lower()] = desc.strip()
    return out


def msg_id_for_index(i: int) -> int:
    """FOC message id of registry entry i. Entries are spaced TWO apart because
    the engine hardcodes description = name id + 1 (proto.cc protoGetMessage:
    PROTOTYPE_MESSAGE_NAME=0, _DESCRIPTION=1). With names packed one apart,
    examining any FOC object showed the NEXT entry's name as its description
    (or blank for the last one). Even ids are names, odd ids descriptions."""
    return FOC_START_MSG + 2 * i


def write_merged_msg(art_dir: str) -> None:
    """Regenerate mod/data/text/english/game/pro_<type>.msg as every BASE entry
    plus every FOC-allocated one. A full replacement (FOC entries only) would
    blank out every vanilla/RPU name of that type the instant this file is
    deployed - it must always be a superset, never a substitute."""
    fname = MSG_FILE_FOR_TYPE[art_dir]
    base = GameIndex()  # pristine=True default: base + RPU only, not our own layer
    try:
        base_msg = parse_msg(base.vfs.read(f"text\\english\\game\\{fname}"))
    except FileNotFoundError:
        base_msg = {}
    base_msg.update(BASE_MSG_OVERRIDES.get(fname, {}))
    out = [f"{{{mid}}}{{}}{{{text}}}" for mid, text in sorted(base_msg.items())]
    descs = read_msg_descs(art_dir)
    for i, text in enumerate(registry_entries(msg_registry_path(art_dir))):
        mid = msg_id_for_index(i)
        out.append(f"{{{mid}}}{{}}{{{text}}}")
        desc = descs.get(text.strip().lower())
        if desc:
            out.append(f"{{{mid + 1}}}{{}}{{{desc}}}")
    dst = dest_for(f"text\\english\\game\\{fname}")
    dst.parent.mkdir(parents=True, exist_ok=True)
    with dst.open("w", encoding="cp1252", errors="replace", newline="\n") as f:
        f.write("\n".join(out) + "\n")


def record_msg_desc(art_dir: str, name: str, desc: str) -> None:
    """Store an examine-description for `name` (idempotent; first one wins)."""
    if not desc or not desc.strip():
        return
    if name.strip().lower() in read_msg_descs(art_dir):
        return
    p = msg_desc_path(art_dir)
    p.parent.mkdir(parents=True, exist_ok=True)
    header = "" if p.exists() else ("# name<TAB>examine description - keyed by name, "
                                    "see port.msg_desc_path\n")
    with p.open("a", encoding="utf-8") as f:
        f.write(header + f"{name.strip()}\t{desc.strip()}\n")


def ensure_message_id(source: str, src: GameIndex, art_dir: str, source_message_id: int) -> int:
    """Look up the source proto's real name in ITS OWN message file and give it
    a fresh, collision-free id in FOC's space; 0 (no message) if no source name
    is found. Never returns the source's raw message_id unpatched - leaving
    that as-is either shows nothing or, worse, coincidentally matches an
    unrelated existing base entry (confirmed: happened for real).
    Also carries the source's examine description (name id + 1) across."""
    if art_dir not in MSG_FILE_FOR_TYPE:
        return 0
    try:
        src_msg = parse_msg(src.vfs.read(f"text\\english\\game\\{MSG_FILE_FOR_TYPE[art_dir]}"))
    except FileNotFoundError:
        src_msg = {}
    text = src_msg.get(source_message_id)
    if not text or not text.strip():
        return 0
    idx = allocate(msg_registry_path(art_dir), text, 0)     # 0-based registry index
    record_msg_desc(art_dir, text, src_msg.get(source_message_id + 1, ""))
    write_merged_msg(art_dir)
    return msg_id_for_index(idx)


# --------------------------------------------------------------- destinations

def dest_for(game_path: str) -> Path:
    return MOD / "data" / game_path.replace("\\", "/")


def copy_out(src: GameIndex, game_path: str, dest_override: str | None = None) -> Path | None:
    """Copy one file from a source install into mod/data/, verbatim. dest_override
    writes under a different relative path than the source's own (used to prefix
    a filename before it ever touches disk)."""
    try:
        data = src.vfs.read(game_path)
    except FileNotFoundError:
        return None
    dst = dest_for(dest_override if dest_override is not None else game_path)
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(data)
    return dst


# ------------------------------------------------------------------ commands

def cmd_sources(args):
    srcs = load_sources()
    if not srcs:
        print(f"no sources configured. Create {SOURCES_FILE.relative_to(ROOT)}")
        return 1
    for k, v in sorted(srcs.items()):
        ok = "ok " if Path(v["path"]).is_dir() else "MISSING"
        print(f"  {ok}  {k:14s} {v.get('project','')}")
        print(f"          {v['path']}")
    return 0


def cmd_list(args):
    src = open_source(args.source)
    pat = (args.pattern or "").lower()
    names = []
    for prov in src.vfs.providers:
        if prov.kind == "dat":
            names += prov.dat.names()
        elif prov.kind == "dir" and prov.root.is_dir():
            # loose data folders (a generated source like fonline2 is one)
            names += [str(q.relative_to(prov.root)).replace("/", "\\")
                      for q in prov.root.rglob("*") if q.is_file()]
    names = sorted({n for n in names if pat in n.lower()}, key=str.lower)
    for n in names[:args.limit]:
        print("  " + n)
    print(f"({len(names)} match, showing {min(len(names), args.limit)})")
    return 0


def cmd_file(args):
    src = open_source(args.source)
    rows = []
    for gp in args.paths:
        dst = copy_out(src, gp)
        if dst is None:
            print(f"  MISSING in {args.source}: {gp}")
            continue
        print(f"  copied {gp}  ({dst.stat().st_size} bytes)")
        rows.append(ledger_row(args.source, "file", gp, "-",
                               str(dst.relative_to(ROOT)), "-", args.notes))
    append_ledger(rows)
    regen_ledger_md()
    return 0


def art_names_taken(art_dir: str) -> set[str]:
    """Lowercase names an engine_art_name() pick must avoid in art/<art_dir>:
    the base game's own .lst (a same-name file in the FOC layer would shadow the
    original everywhere), every FOC registry entry, and whatever is on disk."""
    base = GameIndex()
    try:
        taken = {n.lower() for n in parse_lst(base.vfs.read(f"art\\{art_dir}\\{art_dir}.lst")) if n}
    except FileNotFoundError:
        taken = set()
    taken |= {e.lower() for e in registry_entries(IDS / f"{ART_KEY[art_dir]}.txt") if e}
    d = dest_for(f"art\\{art_dir}")
    if d.is_dir():
        taken |= {q.name.lower() for q in d.iterdir()}
    return taken


def cmd_art(args):
    """Copy an art file and allocate it an index in the matching art .lst."""
    src = open_source(args.source)
    rows = []
    for gp in args.paths:
        norm = gp.replace("/", "\\")
        parts = norm.split("\\")
        if len(parts) < 3 or parts[0].lower() != "art":
            print(f"  skip {gp}: expected art\\<dir>\\<file>")
            continue
        art_dir = parts[1].lower()
        key = ART_KEY.get(art_dir)
        if key is None:
            print(f"  skip {gp}: art\\{art_dir} has no reserved FOC range")
            continue
        # Name it for the ENGINE, not for us: art.cc::artReadList keeps only 12 chars
        # of a .lst entry, so a "<source>_<name>.frm" prefix (what this used to do)
        # produced 8,839 files the game could never open - invisible objects and the
        # 0x48d9c4 crash whenever one shared a tile. engine_art_name() gives an 8.3
        # name tagged with one source letter, checked against the base game's own
        # list (the FOC layer mounts on top, so a bare vanilla name would silently
        # replace that asset everywhere), the registry and the files on disk.
        # Lowercased: gecko's LstReader lowercases every .lst entry it parses.
        if art_dir == "critters":
            print(f"  skip {gp}: cmd_art doesn't handle critters (6-char basename limit "
                  f"needs cmd_proto's collision-aware naming) - port via a proto instead")
            continue
        prefixed_name = engine_art_name(args.source, parts[-1], art_names_taken(art_dir))
        check_art_name(art_dir, prefixed_name)
        prefixed_norm = "\\".join(parts[:-1] + [prefixed_name])
        dst = copy_out(src, norm, dest_override=prefixed_norm)
        if dst is None:
            print(f"  MISSING in {args.source}: {gp}")
            continue
        # -1: FOC_START is a 1-based line number (ids.py's convention, shared with
        # proto indices), but an art FID's index into the .lst is 0-based - CONFIRMED
        # against fallout2-ce's artBuildFilePath (src/art.cc), which indexes
        # fileNames directly by the fid's low bits with no -1 anywhere. allocate()'s
        # start+i must land on the SAME 0-based array slot lists.art_lst()/_assemble()
        # puts this entry at, which is FOC_START[key]-1+i, not FOC_START[key]+i.
        # (A previous edit here removed this -1 on the mistaken belief FID indices
        # match the proto convention - confirmed wrong via direct engine-source
        # inspection and a live in-game repro. Restored.)
        idx = allocate(IDS / f"{key}.txt", prefixed_name, FOC_START[key] - 1)
        check_art_index(idx, art_dir, prefixed_name)
        print(f"  {gp} -> art\\{art_dir} index {idx}")
        rows.append(ledger_row(args.source, "art", norm, "-",
                               str(dst.relative_to(ROOT)), idx, args.notes))
    append_ledger(rows)
    regen_ledger_md()
    return 0


CRITTER_NAME_LEN = 6  # hard engine limit - gecko's FrmResolver.cpp hardcodes
# frmName.substr(0, 6) when building a critter's animation filename (critter FIDs
# also only carry a 12-bit art index, packing animation info into the rest), so
# there is no room to prefix a full source name onto a critter base the way every
# other art type gets prefixed - anything longer just gets silently truncated,
# colliding every ported critter from the same source onto one broken name.


def vanilla_critter_bases() -> set[str]:
    """Lowercased base names (before the first comma) of every vanilla/RPU
    critters.lst entry - pristine=True: base only, not this mod's own layer."""
    base = GameIndex()
    try:
        names = parse_lst(base.vfs.read("art\\critters\\critters.lst"))
    except FileNotFoundError:
        names = []
    return {n.split(",", 1)[0].strip().lower() for n in names if n}


def unique_critter_base(desired: str, source: str, taken: set[str]) -> str:
    """A <=6-char, lowercase, collision-free critter base name, preferring the
    source's own name unchanged and only disambiguating when it truly collides."""
    desired = desired.lower()[:CRITTER_NAME_LEN]
    if desired not in taken:
        return desired
    tag = (source[:1] or "x").lower()
    candidate = f"{desired[:CRITTER_NAME_LEN - 1]}{tag}"
    n = 2
    while candidate in taken:
        suffix = f"{tag}{n}"
        candidate = f"{desired[:max(0, CRITTER_NAME_LEN - len(suffix))]}{suffix}"[:CRITTER_NAME_LEN]
        n += 1
    return candidate


def critter_art_filename(lst_entry: str) -> str:
    """critters.lst entries are comma-records ("hmwarr,11,1" - base name plus fields
    the engine's own critter animation code reads), not filenames. The idle-pose art
    the game actually renders a placed/default critter with is <basename>aa.frm
    (Frm::STANDING_ANIMATION_SUFFIX in the gecko source). Every other art .lst's
    entry is already a plain filename, so this is only ever applied to critters."""
    base = lst_entry.split(",", 1)[0].strip()
    return f"{base}aa.frm"


def cmd_proto(args):
    """
    Port a proto: copy the .pro, allocate a new PID, pull its art, and patch
    the proto's own pid/fid fields to the new numbers.

    A .pro is big-endian and starts u32 pid, u32 messageId, u32 fid
    (proto.cc::protoRead), so both fields we must rewrite are at fixed offsets.
    """
    src = open_source(args.source)
    rows = []
    for raw in args.pids:
        pid = int(raw, 0)
        t = pid_type(pid)
        if t not in PROTO_KEY:
            print(f"  skip {raw}: type {t} has no reserved FOC range")
            continue
        sp = src.proto_path(pid)
        if sp is None or not src.vfs.exists(sp):
            print(f"  MISSING in {args.source}: proto 0x{pid:08X}")
            continue
        data = bytearray(src.vfs.read(sp))
        proto = parse_pro(bytes(data))

        # Idempotent: if this exact proto was already ported from this source,
        # reuse the ID rather than burning another one.
        already = next((r for r in read_ledger()
                        if r["source"] == args.source and r["kind"] == "proto"
                        and r["source_id"].lower() == f"0x{pid:08x}"), None)
        key = PROTO_KEY[t]
        if already:
            new_pid = int(already["dest_id"], 16)
            new_index = pid_index(new_pid)
            new_name = f"{new_index:08d}.pro"
            print(f"  proto 0x{pid:08X} already ported -> 0x{new_pid:08X}; "
                  f"rewriting in place")
        else:
            new_index = len(registry_entries(IDS / f"{key}.txt")) + FOC_START[key]
            new_name = f"{new_index:08d}.pro"
            new_index = allocate(IDS / f"{key}.txt", new_name, FOC_START[key])
            new_pid = (t << 24) | new_index

        # pull the art this proto points at, and re-point the FID
        new_fid = proto.fid
        art_dir = TYPE_NAME.get(t)
        art_key = ART_KEY.get(art_dir)
        note = args.notes
        if art_key is not None:
            try:
                art_names = parse_lst(src.vfs.read(f"art\\{art_dir}\\{art_dir}.lst"))
            except FileNotFoundError:
                art_names = []
            # The FID's art-index field is 12 bits ONLY for critters (they pack animation
            # info into the other bits - see gecko's FrmResolver.cpp / CRITTER_ID_MASK);
            # every other type uses the full 24-bit base-id field (BASE_ID_MASK). Using the
            # critter-only 12-bit mask here for scenery/walls/items silently wraps any index
            # >= 4096 onto a low (base-game) index instead of the intended FOC-range one -
            # invisible until an art list actually grows past 4096 entries, which a big
            # enough bulk port does.
            id_mask = 0xFFF if art_dir == "critters" else 0xFFFFFF
            old_art_idx = proto.fid & id_mask
            if 0 <= old_art_idx < len(art_names) and art_names[old_art_idx]:
                # The registry entry (what ends up as this line's text in the deployed
                # critters.lst) must stay the raw comma-record for critters - the real
                # engine reads its own fields from it. Only the FILE we copy needs the
                # derived animation filename.
                lst_entry = art_names[old_art_idx]
                copy_name = critter_art_filename(lst_entry) if art_dir == "critters" else lst_entry
                try:
                    src_bytes = src.vfs.read(f"art\\{art_dir}\\{copy_name}")
                except FileNotFoundError:
                    src_bytes = None

                if src_bytes is None:
                    note = (note + " ; ART NOT FOUND - fid left pointing at "
                            f"index {old_art_idx}").strip(" ;")
                else:
                    if art_dir == "critters":
                        # No room to prefix here (see CRITTER_NAME_LEN) - keep the source's
                        # own name and only disambiguate when it truly collides, against
                        # EITHER vanilla's own critters.lst (the gap that let ported critters
                        # silently replace original NPCs' art) OR another already-ported FOC
                        # critter of different content sharing the same name.
                        base, _, rest = lst_entry.partition(",")
                        desired = base.lower()[:CRITTER_NAME_LEN]
                        existing_path = dest_for(f"art\\critters\\{desired}aa.frm")
                        try:
                            vanilla_bytes = GameIndex().vfs.read(f"art\\critters\\{desired}aa.frm")
                        except FileNotFoundError:
                            vanilla_bytes = None
                        collided = (
                            (existing_path.is_file() and existing_path.read_bytes() != src_bytes)
                            or (vanilla_bytes is not None and vanilla_bytes != src_bytes)
                        )
                        if collided:
                            taken = {e.split(",", 1)[0].strip().lower()
                                     for e in registry_entries(IDS / f"{art_key}.txt")}
                            taken |= vanilla_critter_bases()
                            new_base = unique_critter_base(desired, args.source, taken)
                        else:
                            new_base = desired
                        lst_entry = f"{new_base},{rest}" if rest else new_base
                        copy_name = f"{new_base}aa.frm"
                    else:
                        # Engine-safe 8.3 name (see cmd_art): the engine keeps only 12
                        # chars of a .lst entry, so the old "<source>_<name>" prefix made
                        # every ported file unopenable. Tagged with one source letter and
                        # checked against the base list (the FOC layer would otherwise
                        # shadow a same-named vanilla asset), the registry and disk.
                        lst_entry = engine_art_name(args.source, lst_entry, art_names_taken(art_dir))
                        check_art_name(art_dir, lst_entry)
                        copy_name = lst_entry

                    dst = dest_for(f"art\\{art_dir}\\{copy_name}")
                    dst.parent.mkdir(parents=True, exist_ok=True)
                    dst.write_bytes(src_bytes)
                    # -1: see the matching comment in cmd_art - FOC_START is a 1-based
                    # line number but the FID's art index is 0-based (confirmed against
                    # fallout2-ce's artBuildFilePath).
                    a_idx = allocate(IDS / f"{art_key}.txt", lst_entry, FOC_START[art_key] - 1)
                    check_art_index(a_idx, art_dir, lst_entry)
                    new_fid = (proto.fid & ~id_mask) | (a_idx & id_mask)
                    rows.append(ledger_row(
                        args.source, "art", f"art\\{art_dir}\\{copy_name}", old_art_idx,
                        f"mod/data/art/{art_dir}/{copy_name}", a_idx,
                        f"pulled in by proto 0x{pid:08X}"))
            else:
                note = (note + f" ; art index {old_art_idx} not in source "
                        f"{art_dir}.lst - fid unchanged").strip(" ;")

        new_message_id = ensure_message_id(args.source, src, art_dir, proto.message_id)
        if new_message_id == 0:
            note = (note + " ; no source name found - message_id cleared").strip(" ;")

        struct.pack_into(">I", data, 0, new_pid)
        struct.pack_into(">I", data, 4, new_message_id)
        struct.pack_into(">I", data, 8, new_fid)

        dst = dest_for(f"proto\\{art_dir}\\{new_name}")
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(bytes(data))
        if not already:
            print(f"  proto 0x{pid:08X} -> 0x{new_pid:08X} "
                  f"({new_name}, index {new_index})")
        if note:
            print(f"      note: {note}")
        if not already:
            rows.append(ledger_row(args.source, "proto", sp, f"0x{pid:08X}",
                                   str(dst.relative_to(ROOT)),
                                   f"0x{new_pid:08X}", note))

    append_ledger(rows)
    regen_ledger_md()
    print("\nRun tools/gen_ids.py and tools/validate.py next.")
    return 0


def cmd_map(args):
    """
    Port a .map, remapping every embedded PID and script index.

    Rather than re-serialise a format we only partly model, this patches the
    original bytes at the offsets the parser recorded. Anything that cannot be
    remapped is reported and left alone - a loud unresolved reference is far
    better than a silent wrong one.
    """
    src = open_source(args.source)
    name = args.name if args.name.lower().endswith(".map") else args.name + ".map"
    gp = f"maps\\{name}"
    if not src.vfs.exists(gp):
        sys.exit(f"{gp} not found in source '{args.source}'")

    data = bytearray(src.vfs.read(gp))
    m = src.load_map(bytes(data))
    print(f"{name}: version {m.version}, map id {m.map_id}, "
          f"{len(m.objects)} objects, {len(m.script_pids)} map scripts")

    # Build the PID translation by porting each distinct proto we can.
    pids: dict[int, int] = {}
    unresolved: dict[int, int] = {}
    stack = list(m.objects)
    seen = set()
    while stack:
        o = stack.pop()
        stack.extend(o.inventory)
        if o.pid == -1 or o.pid in seen:
            continue
        seen.add(o.pid)

    print(f"  {len(seen)} distinct protos referenced")
    if args.dry_run:
        for pid in sorted(seen):
            t = pid_type(pid)
            print(f"    0x{pid:08X}  {TYPE_NAME.get(t,'?'):9s} index {pid_index(pid)}")
        print("\n--dry-run: nothing written. Port the protos first with "
              "`port.py proto`, then re-run without --dry-run.")
        return 0

    # Reuse anything already ported from this source; report the rest.
    ported = {r["source_id"]: r["dest_id"] for r in read_ledger()
              if r["source"] == args.source and r["kind"] == "proto"}
    for pid in seen:
        key = f"0x{pid:08X}"
        if key in ported:
            pids[pid] = int(ported[key], 16)
        else:
            unresolved[pid] = 1

    if unresolved:
        print(f"\n  {len(unresolved)} proto(s) referenced by this map have not been "
              f"ported yet:")
        for pid in sorted(unresolved):
            t = pid_type(pid)
            print(f"    0x{pid:08X}  {TYPE_NAME.get(t,'?'):9s} index {pid_index(pid)}")
        print("\n  Port them first:")
        print("    python tools/port.py proto {} {}".format(
            args.source, " ".join(f"0x{p:08X}" for p in sorted(unresolved))))
        if not args.force:
            print("\n  Refusing to write a map with unresolved protos "
                  "(use --force to write it anyway).")
            return 1

    # Patch object PIDs.
    patched = 0
    stack = list(m.objects)
    while stack:
        o = stack.pop()
        stack.extend(o.inventory)
        if o.pid in pids:
            struct.pack_into(">i", data, o.pid_offset, pids[o.pid])
            patched += 1

    # Script indices: the source's scripts.lst line -> our allocated line.
    our_scripts = {n.lower(): i for i, n in
                   enumerate(registry_entries(IDS / "scripts.txt"))}
    remapped_scripts = 0
    unmapped_scripts: set[str] = set()
    stack = list(m.objects)
    while stack:
        o = stack.pop()
        stack.extend(o.inventory)
        if o.script_index < 0:
            continue
        sname = src.script_name(o.script_index + 1)
        if not sname:
            continue
        key = sname.lower()
        if key in our_scripts:
            new_line = FOC_START["script"] + our_scripts[key]
            struct.pack_into(">i", data, o.script_index_offset, new_line - 1)
            remapped_scripts += 1
        else:
            unmapped_scripts.add(sname)

    dst = dest_for(f"maps\\{name.upper()}")
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(bytes(data))

    print(f"\n  patched {patched} object PID(s), {remapped_scripts} script index(es)")
    if unmapped_scripts:
        print(f"  {len(unmapped_scripts)} script(s) referenced but not ported "
              f"(their indices still point at the SOURCE's numbering):")
        for s in sorted(unmapped_scripts):
            print(f"    {s}")
    print(f"  wrote {dst.relative_to(ROOT)}")

    append_ledger([ledger_row(
        args.source, "map", gp, m.map_id, str(dst.relative_to(ROOT)), "-",
        f"{patched} pids + {remapped_scripts} scripts remapped"
        + (f"; {len(unmapped_scripts)} scripts UNMAPPED" if unmapped_scripts else ""))])
    regen_ledger_md()
    print("\nRun tools/validate.py - it will re-check every reference in this map.")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("sources").set_defaults(fn=cmd_sources)

    p = sub.add_parser("list"); p.set_defaults(fn=cmd_list)
    p.add_argument("source"); p.add_argument("pattern", nargs="?")
    p.add_argument("--limit", type=int, default=60)

    for name, fn in (("file", cmd_file), ("art", cmd_art)):
        p = sub.add_parser(name); p.set_defaults(fn=fn)
        p.add_argument("source"); p.add_argument("paths", nargs="+")
        p.add_argument("--notes", default="")

    p = sub.add_parser("proto"); p.set_defaults(fn=cmd_proto)
    p.add_argument("source"); p.add_argument("pids", nargs="+")
    p.add_argument("--notes", default="")

    p = sub.add_parser("map"); p.set_defaults(fn=cmd_map)
    p.add_argument("source"); p.add_argument("name")
    p.add_argument("--dry-run", action="store_true")
    p.add_argument("--force", action="store_true")

    sub.add_parser("ledger").set_defaults(fn=lambda a: (regen_ledger_md(), 0)[1])

    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    raise SystemExit(main())
