#!/usr/bin/env python3
"""
validate.py - check the mod's ID space and cross-references before it ever runs.

    python tools/validate.py [--verbose] [--warnings-as-errors]

This matters as much as the build. sslc will happily compile a script that
talks to a message node that does not exist, points at a proto that was never
allocated, or reads a GVAR slot occupied by something else - all of which fail
silently or bizarrely at runtime, hours later, in a save you cannot debug.

Checks:
  1  ID space          base has not grown into a FOC range
  2  generated header  foc_ids.h matches mod/ids/*.txt
  3  scripts           scripts.h defines still match scripts.lst line numbers,
                       and every FOC script has a built .int
  4  protos            every .lst entry resolves to a real .pro
  5  GVARs             every GVAR_ referenced resolves, no duplicate names,
                       no define pointing at the wrong slot
  6  messages          every .msg node referenced by a script exists
  7  maps              every proto PID and script index inside a .map resolves
  8  worldmap          maps.txt/city.txt/map.msg registration is consistent
  9  quest log         mod/quests.txt entries resolve (map, GVAR, quests.msg)

Exit code 0 only if there are no errors.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import gen_ids  # noqa: E402
import ids as ids_mod  # noqa: E402
import lists  # noqa: E402
import questlog  # noqa: E402
import worldmap  # noqa: E402
from foclib.paths import inside  # noqa: E402
from foclib import (  # noqa: E402
    GameIndex,
    TYPE_NAME,
    parse_gvar_defines,
    parse_msg,
    pid_index,
    pid_type,
)

ROOT = Path(__file__).resolve().parent.parent
MOD = ROOT / "mod"
SRC = ROOT / "scripts_src" / "foc"
HEADERS = ROOT / "scripts_src" / "headers"
BUILD = ROOT / "build" / "scripts"


class Report:
    def __init__(self, verbose=False):
        self.errors: list[str] = []
        self.warnings: list[str] = []
        self.verbose = verbose
        self._section = ""

    def section(self, name):
        self._section = name
        if self.verbose:
            print(f"\n--- {name}")

    def err(self, msg):
        self.errors.append(f"[{self._section}] {msg}")

    def warn(self, msg):
        self.warnings.append(f"[{self._section}] {msg}")

    def ok(self, msg):
        if self.verbose:
            print(f"    ok: {msg}")


# --------------------------------------------------------------- message scanning

# First argument of each of these is a message-list id. Taken from RPU's
# command.h: Reply(x)->gSay_Reply(NAME,x), mstr(x)->message_str(NAME,x),
# NOption(x,y,z)->giQ_Option(z,NAME,x,y,...), etc.
MSG_FUNCS_SELF = [
    "Reply", "NMessage", "mstr", "display_mstr",
    "NOption", "GOption", "BOption",
    "NLowOption", "GLowOption", "BLowOption",
]
MSG_FUNCS_RANGE = ["Reply_Rand", "display_mstr_rand"]

CALL_RE = {name: re.compile(rf"\b{name}\s*\(\s*([^,()]+?)\s*[,)]")
           for name in MSG_FUNCS_SELF}
RANGE_RE = {name: re.compile(rf"\b{name}\s*\(\s*([^,()]+?)\s*,\s*([^,()]+?)\s*\)")
            for name in MSG_FUNCS_RANGE}
MESSAGE_STR_RE = re.compile(r"\bmessage_str\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*([^,()]+?)\s*\)")


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def collect_defines(text: str) -> dict[str, int]:
    out = {}
    for m in re.finditer(r"#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+\(?\s*(-?\d+)\s*\)?\s*$",
                         text, re.M):
        out[m.group(1)] = int(m.group(2))
    return out


def resolve_int(tok: str, defines: dict[str, int]) -> int | None:
    tok = tok.strip()
    if re.fullmatch(r"-?\d+", tok):
        return int(tok)
    if tok in defines:
        return defines[tok]
    return None


# ------------------------------------------------------------------------ checks

def check_id_space(gi, rep):
    rep.section("1 id-space")
    problems = ids_mod.check(gi)
    for p in problems:
        rep.err(p)
    if not problems:
        rep.ok("base fits below every FOC start")


def check_generated_header(rep):
    rep.section("2 generated-header")
    want = gen_ids.generate()
    have = gen_ids.HEADER.read_text(encoding="utf-8") if gen_ids.HEADER.exists() else ""
    if want != have:
        rep.err("scripts_src/foc/foc_ids.h is stale - run: python tools/gen_ids.py")
    else:
        rep.ok("foc_ids.h matches mod/ids/*.txt")


def check_scripts(gi, rep):
    rep.section("3 scripts")
    full = lists.scripts_lst(gi)

    # 3a. scripts.h SCRIPT_* defines must equal 1-based line numbers.
    h = HEADERS / "scripts.h"
    if not HEADERS.is_dir():
        rep.ok("no scripts_src/headers: script checks skipped (a maps-only mod)")
        return
    if not h.is_file():
        rep.err("scripts_src/headers/scripts.h missing")
    else:
        text = h.read_text(encoding="cp1252", errors="replace")
        defs = {}
        for m in re.finditer(r"#define\s+(SCRIPT_[A-Za-z0-9_]+)\s+\(?(-?\d+)\)?", text):
            defs[m.group(1)] = int(m.group(2))
        bad = 0
        for name, line in sorted(defs.items(), key=lambda kv: kv[1]):
            if line < 1 or line > len(full):
                rep.err(f"{name} = {line} is outside scripts.lst (1..{len(full)})")
                bad += 1
                continue
            entry = full[line - 1]
            expect = name[len("SCRIPT_"):].lower()
            got = Path(entry).stem.lower() if entry else ""
            if expect != got:
                # not always a 1:1 name match in vanilla, so only flag when the
                # line is empty or obviously unrelated
                if not entry:
                    rep.err(f"{name} = {line} points at an empty scripts.lst line")
                    bad += 1
        rep.ok(f"{len(defs)} SCRIPT_* defines checked against scripts.lst, "
               f"{bad} problem(s)")

    # 3b. every FOC script registered must have a compiled .int
    foc = gen_ids.read_registry(MOD / "ids" / "scripts.txt")
    for i, name in enumerate(foc):
        if not name:
            continue
        idx = ids_mod.FOC_START["script"] + i
        built = BUILD / name
        if not built.is_file():
            rep.err(f"scripts.txt line {i+1} allocates {name} at index {idx}, "
                    f"but build/scripts/{name} does not exist - run tools/build.py")
        else:
            rep.ok(f"{name} -> index {idx}")

    # 3c. every built .int must be registered, or the game can never reach it.
    #     sfall global scripts (gl*.int) are the exception: sfall loads them by
    #     filename and they never get a scripts.lst index.
    if BUILD.is_dir():
        registered = {n.lower() for n in foc if n}
        for p in sorted(BUILD.glob("*.int")):
            if p.name.lower().startswith("gl"):
                continue
            if p.name.lower() not in registered:
                rep.err(f"build/scripts/{p.name} is compiled but not listed in "
                        f"mod/ids/scripts.txt - it will have no script index")


def check_protos(gi, rep):
    rep.section("4 protos")
    total = missing = 0
    for t in (0, 1, 2, 3, 4, 5):
        try:
            full = lists.proto_lst(gi, t)
        except lists.Overflow as e:
            rep.err(str(e))
            continue
        for i, entry in enumerate(full):
            if not entry or entry == ids_mod.FILLER["proto"]:
                continue
            total += 1
            idx = i + 1
            path = f"proto\\{TYPE_NAME[t]}\\{entry}"
            local = MOD / "data" / "proto" / TYPE_NAME[t] / entry
            if local.is_file() or gi.vfs.exists(path):
                continue
            missing += 1
            if missing <= 20:
                rep.err(f"{TYPE_NAME[t]}.lst line {idx} names {entry}, "
                        f"but no such .pro exists in the mod or the base")
    if missing > 20:
        rep.err(f"...and {missing - 20} more missing .pro files")
    rep.ok(f"{total} proto .lst entries checked, {missing} missing")


def check_art(gi, rep):
    """
    Art .lst entries must resolve to real FRMs.

    Split by range on purpose. RPU 2.4.33 ships 46 dangling entries in its own
    art/scenery.lst; those are pre-existing and only ever a warning, because an
    error we cannot fix is an error we learn to ignore - and then it hides ours.
    Anything at or above the FOC start IS ours, and is an error.
    """
    rep.section("4b art")
    from ids import ART_KEY
    for art_dir, key in ART_KEY.items():
        if not gen_ids.read_registry(MOD / "ids" / f"{key}.txt"):
            continue
        try:
            full = lists.art_lst(gi, art_dir)
        except lists.Overflow as e:
            rep.err(str(e))
            continue
        start = ids_mod.FOC_START[key]
        base_missing = 0
        base_long = 0
        foc_missing = 0
        checked = 0
        for i, entry in enumerate(full):
            if not entry or entry == ids_mod.FILLER["art"]:
                continue
            line = i + 1
            checked += 1
            # Art entries: base single-file scenery is one .frm; critter art is
            # a frame FAMILY - the .lst carries a prefix + flags (e.g.
            # 'narobe,11') and the engine expands the prefix into frame files
            # (NAROBEAA.FRM, NMRANGBA.FR0, ...). Validate by prefix against
            # both our loose art dir and the vfs (critter.dat/master.dat).
            name = entry.split(",")[0].strip()
            # The engine reads each .lst line with strncpy(dst, line, 12)
            # (art.cc::artReadList), so a longer name can NEVER be opened: the
            # object is invisible, and _obj_insert crashes at 0x48d9c4 the moment
            # it shares a tile. Gecko does not truncate, which is why this has to
            # be caught here and not by looking at the map in the editor.
            if len(name) > ids_mod.ENGINE_ART_NAME_MAX:
                if line >= start:
                    foc_missing += 1
                    rep.err(f"art/{art_dir}.lst line {line} names {name} "
                            f"({len(name)} chars): the engine keeps only "
                            f"{ids_mod.ENGINE_ART_NAME_MAX}, so this file can never "
                            f"load - run tools/fix_art_names.py")
                else:
                    base_long += 1
                continue
            local = MOD / "data" / "art" / art_dir / name
            ok_local = local.is_file() or any(
                p.name.upper().startswith(name.upper())
                for p in (MOD / "data" / "art" / art_dir).glob(f"{name}*")
                if p.is_file()
            )
            ok_vfs = gi.vfs.exists(f"art\\{art_dir}\\{name}")
            if not ok_vfs and not ok_local:
                # critter family: probe for the first frame letter pair
                for probe in (name[:6].upper() + "AA.FRM",
                              name[:6].upper() + "AA.FR0",
                              name.upper() + "AA.FRM"):
                    if gi.vfs.exists(f"art\\{art_dir}\\{probe}"):
                        ok_vfs = True
                        break
            if ok_local or ok_vfs:
                continue
            if line >= start:
                foc_missing += 1
                rep.err(f"art/{art_dir}.lst line {line} is in the FOC range and "
                        f"names {entry}, but no such file exists")
            else:
                base_missing += 1
        if base_missing:
            rep.warn(f"art/{art_dir}.lst has {base_missing} dangling entr"
                     f"{'y' if base_missing == 1 else 'ies'} in the BASE range "
                     f"(pre-existing in RPU 2.4.33, not caused by this mod)")
        if base_long:
            rep.warn(f"art/{art_dir}.lst has {base_long} BASE-range name(s) over "
                     f"{ids_mod.ENGINE_ART_NAME_MAX} chars (RPU's own, unloadable)")
        # A FID carries only 12 bits of art index (art.cc::artBuildFilePath masks
        # with 0xFFF), so every entry from line 4097 on is unreachable in-game: a
        # proto pointing there draws index & 0xFFF instead - a random other
        # sprite. Entries up there are fine as long as nothing USES them (the
        # ported palette parks there on purpose - see repack_art.py); the check
        # that something used has landed above the cap is check_art_cap below.
        rep.ok(f"art/{art_dir}.lst: {checked} entries checked, "
               f"{foc_missing} FOC-range problem(s)")


def check_art_cap(gi, rep):
    """Any art a map, stamp or script actually uses must sit below the 4096
    cap, or the game draws an unrelated sprite where gecko shows the right one."""
    rep.section("4c art cap")
    import repack_art
    used, counts = repack_art.collect_used(repack_art.Protos(), GameIndex(pristine=False))
    for art_dir, key in ids_mod.ART_KEY.items():
        start = ids_mod.FOC_START[key] - 1
        over = sorted(i for i in used[art_dir] if i >= ids_mod.ART_INDEX_CAP)
        if over:
            rep.err(f"art/{art_dir}: {len(over)} used entr{'y' if len(over) == 1 else 'ies'} at "
                    f"index >= {ids_mod.ART_INDEX_CAP} (e.g. {over[:5]}) - the game cannot "
                    f"address them. Run: python tools/repack_art.py")
        n_foc = sum(1 for i in used[art_dir] if i >= start)
        if n_foc and not over:
            rep.ok(f"art/{art_dir}: {n_foc} used FOC entries, all below {ids_mod.ART_INDEX_CAP}")
    rep.ok(f"scanned {counts['maps']} map files, {counts['stamps']} stamps, "
           f"{counts['script_pids']} script-named protos")


def check_gvars(gi, rep, verbose):
    rep.section("5 gvars")
    try:
        slots = lists.gvar_names(gi)
    except lists.Overflow as e:
        rep.err(str(e))
        return

    # 5a. duplicate names across the whole slot list
    named = [n for n in slots if n and not n.startswith("GVAR_FOC_RESERVED")
             and not n.startswith("GVAR_UNUSED_")]
    for name, c in Counter(named).items():
        if c > 1:
            where = [i for i, n in enumerate(slots) if n == name]
            rep.err(f"{name} occupies {c} slots {where} - reads and writes will "
                    f"disagree about which one they mean")

    # 5b. GVAR_ defines in headers must point at the slot holding that name.
    #     (RPU 2.4.33 ships 38 pre-existing mismatches between global.h and
    #     vault13.gam; those are the base's problem, reported as warnings so
    #     they cannot mask a NEW mistake of ours.)
    defines: dict[str, int] = {}
    for h in ("global.h",):
        p = HEADERS / h
        if p.is_file():
            defines.update(parse_gvar_defines(
                p.read_text(encoding="cp1252", errors="replace")))
    foc_header = SRC / "foc_ids.h"
    foc_defines = {}
    if foc_header.is_file():
        foc_defines = parse_gvar_defines(
            foc_header.read_text(encoding="utf-8", errors="replace"))

    base_bad = 0
    for name, slot in defines.items():
        if not (0 <= slot < len(slots)) or slots[slot] != name:
            base_bad += 1
    if base_bad:
        rep.warn(f"{base_bad} GVAR defines in the BASE's global.h do not match "
                 f"vault13.gam slots (pre-existing RPU 2.4.33 inconsistency, "
                 f"not caused by this mod). Treat vault13.gam as authoritative.")

    for name, slot in foc_defines.items():
        if not (0 <= slot < len(slots)):
            rep.err(f"{name} = {slot} is outside vault13.gam (0..{len(slots)-1})")
        elif slots[slot] != name:
            rep.err(f"{name} is #defined as slot {slot}, but that slot holds "
                    f"{slots[slot] or '<unnamed>'}")
        else:
            rep.ok(f"{name} -> slot {slot}")

    # 5c. every GVAR_ referenced by our scripts must be defined somewhere
    known = set(defines) | set(foc_defines)
    for ssl in sorted(SRC.rglob("*.ssl")):
        text = strip_comments(ssl.read_text(encoding="cp1252", errors="replace"))
        local = set(collect_defines(text))
        for m in re.finditer(r"\b(GVAR_[A-Za-z0-9_]+)\b", text):
            g = m.group(1)
            if g not in known and g not in local:
                rep.err(f"{ssl.name} references {g}, which is not defined in "
                        f"global.h, foc_ids.h, or the script itself")


def check_messages(gi, rep):
    rep.section("6 messages")
    dialog_dir = MOD / "data" / "text" / "english" / "dialog"
    checked = unchecked = 0

    for ssl in sorted(SRC.rglob("*.ssl")):
        stem = ssl.stem
        raw = ssl.read_text(encoding="cp1252", errors="replace")
        text = strip_comments(raw)
        defines = collect_defines(raw)

        local = dialog_dir / f"{stem}.msg"
        if local.is_file():
            msgs = parse_msg(local.read_bytes())
            source = f"mod/.../dialog/{stem}.msg"
        else:
            try:
                msgs = parse_msg(gi.vfs.read(f"text\\english\\dialog\\{stem}.msg"))
                source = f"base text\\english\\dialog\\{stem}.msg"
            except FileNotFoundError:
                msgs = None
                source = None

        wanted: set[int] = set()
        for name, rx in CALL_RE.items():
            for m in rx.finditer(text):
                v = resolve_int(m.group(1), defines)
                if v is None:
                    unchecked += 1
                else:
                    wanted.add(v)
        for name, rx in RANGE_RE.items():
            for m in rx.finditer(text):
                a = resolve_int(m.group(1), defines)
                b = resolve_int(m.group(2), defines)
                if a is None or b is None:
                    unchecked += 1
                else:
                    wanted.update(range(a, b + 1))

        if wanted and msgs is None:
            rep.err(f"{ssl.name} references {len(wanted)} message node(s) but no "
                    f"{stem}.msg exists in the mod or the base")
            continue

        for n in sorted(wanted):
            checked += 1
            if n not in msgs:
                rep.err(f"{ssl.name} uses message {n}, which is not in {source}")

        # message_str(SCRIPT_OTHER, n) - resolve the other script's msg file
        for m in MESSAGE_STR_RE.finditer(text):
            target, arg = m.group(1), m.group(2)
            v = resolve_int(arg, defines)
            if v is None:
                unchecked += 1
                continue
            if target in ("NAME",):
                continue  # already covered by mstr/Reply above
            line = None
            for src in (defines,):
                if target in src:
                    line = src[target]
            if line is None:
                hp = HEADERS / "scripts.h"
                if hp.is_file():
                    sdefs = {}
                    for mm in re.finditer(r"#define\s+(SCRIPT_[A-Za-z0-9_]+)\s+\(?(-?\d+)\)?",
                                          hp.read_text(encoding="cp1252", errors="replace")):
                        sdefs[mm.group(1)] = int(mm.group(2))
                    line = sdefs.get(target)
            if line is None:
                continue
            full = lists.scripts_lst(gi)
            if not (1 <= line <= len(full)):
                continue
            other = Path(full[line - 1]).stem
            op = MOD / "data" / "text" / "english" / "dialog" / f"{other}.msg"
            if op.is_file():
                omsgs = parse_msg(op.read_bytes())
            else:
                try:
                    omsgs = parse_msg(gi.vfs.read(f"text\\english\\dialog\\{other}.msg"))
                except FileNotFoundError:
                    continue
            checked += 1
            if v not in omsgs:
                rep.err(f"{ssl.name} uses message_str({target}, {v}) but "
                        f"{other}.msg has no node {v}")

    rep.ok(f"{checked} message references resolved, {unchecked} not statically "
           f"resolvable (computed at runtime)")
    if unchecked:
        rep.warn(f"{unchecked} message reference(s) use a computed id and could "
                 f"not be checked")


def check_maps(gi, gi_full, rep):
    """gi is the pristine base view (what the ID space is assembled against);
    gi_full also sees the deployed FOC layer, which a map needs for sizing
    objects whose protos are OURS (scenery subtypes carry extra fields) and
    for deciding whether a PID resolves anywhere the game will look."""
    rep.section("7 maps")
    map_dir = MOD / "data" / "maps"
    maps = sorted(map_dir.glob("*.[Mm][Aa][Pp]")) if map_dir.is_dir() else []
    if not maps:
        rep.ok("no mod maps yet")
        return

    full_scripts = lists.scripts_lst(gi)
    for mp in maps:
        try:
            m = gi_full.load_map(mp.read_bytes())
        except Exception as e:  # noqa: BLE001
            rep.err(f"{mp.name} failed to parse: {type(e).__name__}: {e}")
            continue

        bad_pids: dict[int, int] = defaultdict(int)
        bad_scripts: dict[int, int] = defaultdict(int)
        stack_hexes: dict[tuple[int, int], int] = defaultdict(int)
        stack = list(m.objects)
        while stack:
            o = stack.pop()
            stack.extend(o.inventory)
            if o.pid != -1 and gi_full.proto(o.pid) is None:
                bad_pids[o.pid] += 1
            if o.script_index >= 0:
                line = o.script_index + 1
                if not (1 <= line <= len(full_scripts)) or not full_scripts[line - 1]:
                    bad_scripts[o.script_index] += 1
            # Same-hex stacks of visible objects are NOT a defect. Measured
            # 2026-09-21 against master.dat: Arroyo has 690 stacked hexes in
            # 4,010 objects, NCR1 854/6,671, Modoc 842/5,130 - barred windows
            # (steel bars over wall pieces), fence corners, invisible block.frm
            # on beds/rocks, weeds under trash. REDWELL.MAP sits at 111/2,991,
            # below every vanilla town. The old per-hex error came from the
            # generator era, when the slice map had a self-imposed no-stack
            # rule; the map is gecko-edited now. Keep a per-map count as an
            # informational line only, so a sudden jump is still visible.
            t = pid_type(o.pid)
            if t in (0, 1, 2, 3, 4):
                stack_hexes[(o.tile, o.elevation)] += 1

        stacked = sum(1 for n in stack_hexes.values() if n > 1)
        rep.ok(f"{mp.name}: {len(m.objects)} objects, {stacked} hexes with "
               f"stacked visible objects (informational; vanilla towns run 3-20%)")

        for pid, n in sorted(bad_pids.items()):
            rep.err(f"{mp.name}: {n} object(s) use proto 0x{pid:08X} "
                    f"({TYPE_NAME.get(pid_type(pid), '?')} index {pid_index(pid)}) "
                    f"which does not resolve")
        for si, n in sorted(bad_scripts.items()):
            rep.err(f"{mp.name}: {n} object(s) use script index {si} "
                    f"(scripts.lst line {si+1}) which does not resolve")

        for pid, sidx in m.script_pids:
            if sidx >= 0:
                line = sidx + 1
                if not (1 <= line <= len(full_scripts)) or not full_scripts[line - 1]:
                    rep.err(f"{mp.name}: map script table references index {sidx} "
                            f"(scripts.lst line {line}) which does not resolve")

        # the map's own script, from the header (map.cc uses
        # gMapHeader.scriptIndex - 1, so the stored value is the 1-based line)
        if m.map_script_id > 0:
            line = m.map_script_id
            if not (1 <= line <= len(full_scripts)) or not full_scripts[line - 1]:
                rep.err(f"{mp.name}: header map script index {line} "
                        f"(scripts.lst line {line}) does not resolve")
            else:
                rep.ok(f"{mp.name}: map script = {full_scripts[line - 1]} "
                       f"(line {line})")

        if not bad_pids and not bad_scripts:
            rep.ok(f"{mp.name}: {len(m.objects)} objects, "
                   f"{len(m.script_pids)} scripts, all resolve")


def check_worldmap(gi, rep):
    """
    Check 8: the maps.txt / city.txt / map.msg registration.

    Everything here works over the GENERATED text (the same functions deploy
    uses), so validate and deploy can never disagree about what the game will
    load. Among other things this catches: a map outside maps.txt (unreachable
    - the original sin this check exists for), an entrance pointing at a
    missing lookup_name, overlapping worldmap circles, and missing map.msg
    names that would make saves/automap show "Error".
    """
    rep.section("8 worldmap")
    registry = worldmap.read_maps_registry()
    if not registry:
        rep.ok("no FOC maps registered")
        return
    problems = worldmap.check(gi, registry)
    for p in problems:
        rep.err(p)
    if not problems:
        rep.ok(f"{len(registry)} FOC map(s): maps.txt + city.txt + map.msg "
               f"resolve over the base's {worldmap.base_map_count(gi)} maps / "
               f"{worldmap.base_area_count(gi)} areas")


def check_questlog(gi, rep):
    """
    Check 9: the Pip-Boy quest log. Every mod/quests.txt entry names a
    registered map, a registered GVAR and an existing quests.msg line, and
    shows before it is struck through. Built by the same function deploy uses.
    """
    rep.section("9 quest log")
    try:
        text, problems = questlog.build(gi)
    except (questlog.QuestLogError, worldmap.WorldmapError) as e:
        rep.err(str(e))
        return
    for p in problems:
        rep.err(p)
    if not problems:
        n = sum(1 for l in text.splitlines() if l and not l.startswith("#"))
        rep.ok(f"{n} quest log entr{'y' if n == 1 else 'ies'} resolve")


# -------------------------------------------------------------------------- main

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--warnings-as-errors", action="store_true")
    ap.add_argument("--base", type=Path, default=None)
    args = ap.parse_args()

    base = inside(args.base) if args.base else None
    gi = GameIndex(base)
    gi_full = GameIndex(base, pristine=False)
    rep = Report(args.verbose)

    check_id_space(gi, rep)
    check_generated_header(rep)
    check_scripts(gi, rep)
    check_protos(gi, rep)
    check_art(gi, rep)
    check_art_cap(gi, rep)
    check_gvars(gi, rep, args.verbose)
    check_messages(gi, rep)
    check_maps(gi, gi_full, rep)
    check_worldmap(gi, rep)
    check_questlog(gi, rep)

    print()
    for w in rep.warnings:
        print(f"WARN  {w}")
    for e in rep.errors:
        print(f"ERROR {e}")

    n_e, n_w = len(rep.errors), len(rep.warnings)
    print()
    if n_e:
        print(f"VALIDATION FAILED: {n_e} error(s), {n_w} warning(s)")
        return 1
    if n_w and args.warnings_as_errors:
        print(f"VALIDATION FAILED (warnings as errors): {n_w} warning(s)")
        return 1
    print(f"validation passed: 0 errors, {n_w} warning(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())