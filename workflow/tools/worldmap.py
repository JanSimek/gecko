#!/usr/bin/env python3
"""
worldmap.py - register FOC maps with the engine (maps.txt / city.txt / map.msg)
and fit worldmap.txt to FOC's West Texas worldmap art.

FOC's worldmap has only FOC locations: every base (vanilla/RPU) area is kept
as a hidden, locked section so area numbering never moves (hide_base_areas),
and worldmap.txt's per-subtile data is flattened to match the replacement art
(merged_worldmap_txt). Both are explained where the constants are defined.

    python tools/worldmap.py            # show what would be generated
    python tools/worldmap.py --check    # exit non-zero if anything is wrong

Why this exists: deploy.py writes the ID-space files (scripts.lst, vault13.gam,
proto/art .lst) but nothing yet generated data\\maps.txt, data\\city.txt or
text\\english\\game\\map.msg - so FOC maps were invisible to the worldmap and
had no names in saves/automap. A map is unreachable until it is in maps.txt AND
a city.txt area points an entrance at it.

ENGINE SEMANTICS (verified in reference/fallout2-ce/src/worldmap.cc, map.cc):

  * maps.txt is read densely from [Map 000]: wmMapInit loops mapIdx from 0
    until a section lacks "lookup_name", so there can be NO GAPS - a FOC map
    at id 200 needs sections 0..200 all present. The gap 173..199 is written
    as harmless reserved sections (never entered).
  * map getmsg ids are map * 3 + elevation + 200 in map.msg; city names /
    worldmap labels are 1500 + area in the SAME map.msg (map.cc::mapGetName,
    worldmap.cc::wmGetAreaName).
  * city.txt is read densely from [Area 00] ("Area %02d"), one section per
    area. wmAreaInit in CE enforces wmMaxAreaNum == CITY_COUNT, but sfall's
    "cities limit patch" (Worldmap.cpp::MapLimitsPatches, unconditional) jumps
    over that check in the shipped binary, so appending areas is safe.
  * an entrance's map field is the lookup_name of a maps.txt section,
    resolved by wmParseFindMapIdxMatch (index into wmMapInfoList - so the
    section's INDEX must equal the map id everywhere else uses).
  * entrance elevation/tile -1 means "use the map header's own defaults".
  * townmap_art_idx/label are 0-BASED art\\intrface\\intrface.lst indices
    (verified: 156 -> twnarroy.frm at lst line 157). -1 = no town map screen,
    so clicking the worldmap marker enters the map directly.
  * an exit grid object with dest map -2 sends the party to wmWorldMap()
    (map.cc::mapHandleTransition) - that is how REDWELL's exits already work.

The deploy layer (base\\mods\\foc\\) REPLACES these files wholesale (folders
and .dats do not merge), so the generated files must contain the full base
content verbatim plus our appends - never just our sections.

The data for each map comes from mod/ids/maps.txt, the project's ID registry.

MULTI-MAP AREAS: a map line with area_of=<id> does NOT get its own worldmap
area; it becomes another entrance of the area owned by map <id> (the way Vault
City's Courtyard / Downtown / Vault are one area). Entrance order is what
matters for a city with no town-map art: wmAreaFindFirstValidMap enters the
FIRST entrance whose state is On, and entering any saved map flips its own
entrance On (map.cc: wmMapMarkMapEntranceState on load). So an entrance=0 map
that starts Off takes over the area the moment the player is put on it - which
is how Redwell swaps from RWCORE to RWCORE2 after the first night, with no
script call. entrance=<n> sets the slot (dense from 0 across the area; the
owner defaults to 0, attached maps to the next free slot in registry order),
entrance_state=On|Off the initial state (owner On, attached Off).
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib.paths import inside  # noqa: E402
from foclib import GameIndex  # noqa: E402
from foclib import config as _config  # noqa: E402
from ids import FOC_START  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
IDS = ROOT / "mod" / "ids"

MAPS_VFS = "data\\maps.txt"
CITY_VFS = "data\\city.txt"
MAP_MSG_VFS = "text\\english\\game\\map.msg"
WORLDMAP_VFS = "data\\worldmap.txt"

# The base's 61 areas keep their [Area NN] sections, so area numbers (ours
# start at 61, vanilla scripts' AREA_* constants, saves) do not move, but every
# one is written start_state=Off + lock_state=On: never drawn, never clickable,
# and wmAreaSetVisibleState refuses to reveal a locked area unless forced
# (only mark_area_known forces it, and only vanilla content calls that on
# vanilla areas). Area 21 "Car Outta Gas" is left alone: the engine writes its
# state directly when the car runs dry, bypassing the lock anyway.
BASE_AREAS_LEFT_ALONE = {21}

# FOC's worldmap art (mod/data/art/intrface/WRLDMP00..19.FRM, 4x5 tiles of
# 350x300) is West Texas, not California, so the base's per-subtile data in
# worldmap.txt no longer matches anything under it: its walk masks would wall
# off "ocean" in the middle of the desert, Mountain/Ocean terrain would slow
# travel over flat ground, and its encounter tables are vanilla regions (Arroyo,
# Navarro, NCR...) with vanilla special encounters. Every subtile becomes
# Desert and rolls FOC's own encounter table (FOC_ENCOUNTER_TABLE, added after
# the base's tables by merged_worldmap_txt) at SUBTILE_FREQ ([Data]: Rare = 6%
# per check).
SUBTILE_TERRAIN = "Desert"
SUBTILE_FREQ = "Rare"

# The Permian Basin's random encounters: geckos, wild dogs, radscorpions and
# stray brahmin. No raiders (the owner's call), and no "Special" entry: those
# are vanilla's set pieces. Maps are the base's desert encounter maps.
#
# Every critter needs its encounter script (Script:N is the scripts.lst LINE):
# without one it just stands there. Vanilla's "Dog" and "Rad_Scorpions"
# groups have none, so FOC brings its own (FOC_ENCOUNTER_GROUPS: ECWldDog 632,
# ECScorp 616). Geckos are Easy_Gecko (ZCGecko 270), brahmin Brahmin
# (ZCBrahmn 203). The enc_NN numbers must run without gaps.
FOC_ENCOUNTER_NAME = "FOC_Basin"
FOC_ENCOUNTER_GROUPS = [
    "[Encounter: FOC_Wild_Dogs]",
    "type_00=ratio:60%, pid:16777298, Script:632      ; Wild Dog",
    "type_01=ratio:40%, pid:16777762, Script:632      ; Red Wild Dog",
    "position=Surrounding",
    "",
    "[Encounter: FOC_Scorpions]",
    "type_00=ratio:70%, pid:16777221, Script:616      ; Small Radscorpion",
    "type_01=ratio:30%, pid:16777222, Script:616      ; Large Radscorpion",
    "position=Surrounding",
    "",
]
FOC_ENCOUNTER_TABLE = [
    "maps=Desert Encounter 1, Desert Encounter 2, Desert Encounter 3",
    "enc_00=Chance:30%,Enc:(2-4) Easy_Gecko AMBUSH Player",
    "enc_01=Chance:20%,Enc:(2-4) FOC_Wild_Dogs AMBUSH Player",
    "enc_02=Chance:20%,Enc:(2-4) FOC_Scorpions AMBUSH Player,If(Player(Level) > 2)",
    "enc_03=Chance:10%,Enc:(1-3) Brahmin",
]
# "You encounter: ..." for each enc_NN above, in order. worldmap.msg id is
# 3000 + 50 * table number + entry (worldmap.cc); merged_worldmap_msg adds them.
FOC_ENCOUNTER_TEXT = [
    "Some geckos.",
    "A pack of wild dogs.",
    "Some radscorpions.",
    "A few stray brahmin.",
]
WORLDMAP_MSG_VFS = "text\\english\\game\\worldmap.msg"
SUBTILE_COLS, SUBTILE_ROWS = 7, 6      # SUBTILE_GRID_WIDTH x HEIGHT, key "col_row"

# Worldmap sphere overlays (art\\intrface\\WRLDSPR0/1/2.frm) frame sizes,
# measured from the FRMs. These bound the clickable rect per \"size\" in
# wmMatchWorldPosToArea.
SPHERE_PX = {"small": 12, "medium": 25, "large": 49}

# Reserved sections between the base's last map id and FOC_START['map'].
# They exist so a FOC map's section index equals its id when maps.txt is read
# densely from 0. They are never entered (no entrance references them).
RESERVED_MAP = "focrsvd"
RESERVED_NAME = "FOC reserved"

# Default ambience for a FOC village - copied from RPU's Arroyo section.
DEFAULT_AMBIENT = ("gntlwin1:25, gntlwind:25, dogbark:20, dogbark1:20, "
                   "gustwind:5, gustwin1:5")


class WorldmapError(Exception):
    pass


# ------------------------------------------------------------------ registry

_SETTING_RE = re.compile(r"([A-Za-z_]\w*)=(.*?)(?=\s+[A-Za-z_]\w*=|$)")


def read_maps_registry(path: Path | None = None) -> list[dict]:
    """
    Parse mod/ids/maps.txt into map records.

    Line format:
        <id> <FILENAME.MAP> <Display Name> [key=value ...]

    The display name may contain spaces ("Lamesa Depot"); it runs from after
    the filename to the first key=value token. A setting's value also runs
    to the next key=value token, so values may contain spaces too
    (area_name=Lamesa Depot, ambient=a:1, b:2).

    The display name also serves as the worldmap lookup name unless a
    lookup_name= setting overrides it.
    """
    path = path or (IDS / "maps.txt")
    out = []
    if not path.is_file():
        return out
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        s = raw.strip()
        if not s or s.startswith("#"):
            continue
        head = s.split(None, 2)
        if len(head) < 3:
            raise WorldmapError(
                f"mod/ids/maps.txt line needs at least "
                f"'<id> <FILE.MAP> <Name>': {raw!r}")
        try:
            map_id = int(head[0])
        except ValueError:
            raise WorldmapError(
                f"mod/ids/maps.txt: map id must be an integer: {raw!r}")
        filename = head[1]
        rest = head[2]

        m = re.search(r"(?:^|\s)[A-Za-z_]\w*=", rest)
        if m:
            name = rest[:m.start()].strip()
            settings_str = rest[m.start():].strip()
        else:
            name, settings_str = rest.strip(), ""
        if not name:
            raise WorldmapError(
                f"mod/ids/maps.txt: no display name before the first "
                f"key=value in line {raw!r}")

        rec = {"id": map_id, "filename": filename, "name": name, "settings": {}}
        pos = 0
        for km in _SETTING_RE.finditer(settings_str):
            if settings_str[pos:km.start()].strip():
                raise WorldmapError(
                    f"mod/ids/maps.txt: cannot parse settings near "
                    f"{settings_str[pos:km.start()]!r} in line {raw!r}")
            rec["settings"][km.group(1)] = km.group(2).strip()
            pos = km.end()
        if settings_str[pos:].strip():
            raise WorldmapError(
                f"mod/ids/maps.txt: trailing junk {settings_str[pos:]!r} "
                f"in line {raw!r}")
        out.append(rec)
    return out


def _lookup(rec: dict) -> str:
    return rec["settings"].get("lookup_name", rec["name"])


def _owner_id(rec: dict) -> int | None:
    """The map id whose area this map joins, or None if it owns an area."""
    v = rec["settings"].get("area_of")
    if v is None:
        return None
    try:
        return int(v)
    except ValueError:
        raise WorldmapError(
            f"map {rec['filename']}: area_of={v!r} must be a map id")


def area_groups(registry: list[dict]) -> list[tuple[dict, list[dict]]]:
    """
    [(owner_rec, [every map of that area, owner included, in entrance order])]
    in registry order of the owners. Area ids are assigned in this order:
    base_area_count + position.
    """
    by_id = {rec["id"]: rec for rec in registry}
    owners = [rec for rec in registry if _owner_id(rec) is None]
    members: dict[int, list[dict]] = {o["id"]: [o] for o in owners}
    for rec in registry:
        oid = _owner_id(rec)
        if oid is None:
            continue
        if oid not in by_id:
            raise WorldmapError(
                f"map {rec['filename']}: area_of={oid} is not a registered map")
        if _owner_id(by_id[oid]) is not None:
            raise WorldmapError(
                f"map {rec['filename']}: area_of={oid} ({by_id[oid]['filename']}) "
                f"is itself attached to another area - point at the owner")
        members[oid].append(rec)

    out = []
    for o in owners:
        group = members[o["id"]]
        # explicit entrance=<n> slots first, then fill the rest in order
        slots: dict[int, dict] = {}
        pending = []
        for rec in group:
            v = rec["settings"].get("entrance")
            if v is None:
                pending.append(rec)
                continue
            try:
                n = int(v)
            except ValueError:
                raise WorldmapError(
                    f"map {rec['filename']}: entrance={v!r} must be an integer")
            if n in slots:
                raise WorldmapError(
                    f"area of {o['filename']}: entrance slot {n} claimed by both "
                    f"{slots[n]['filename']} and {rec['filename']}")
            slots[n] = rec
        nxt = 0
        for rec in pending:
            while nxt in slots:
                nxt += 1
            slots[nxt] = rec
        if sorted(slots) != list(range(len(slots))):
            raise WorldmapError(
                f"area of {o['filename']}: entrance slots {sorted(slots)} are not "
                f"dense from 0 - the engine stops reading at the first gap")
        out.append((o, [slots[i] for i in sorted(slots)]))
    return out


def _entrance_line(rec: dict, is_owner: bool) -> str:
    s = rec["settings"]
    if "entrance_line" in s:            # full override, verbatim
        return s["entrance_line"]
    state = s.get("entrance_state", "On" if is_owner else "Off")
    if state.lower() not in ("on", "off"):
        raise WorldmapError(
            f"map {rec['filename']}: entrance_state={state!r} must be On or Off")
    return f"{state.capitalize()},-1,-1,{_lookup(rec)},-1,-1,0"


# -------------------------------------------------------------- base reading

def base_map_count(gi: GameIndex) -> int:
    """Number of [Map NNN] sections the BASE (pristine) maps.txt has."""
    return _count_sections(gi, MAPS_VFS, r"\[Map\s+(\d+)\]", "map")


def base_area_count(gi: GameIndex) -> int:
    """Number of [Area NN] sections the BASE (pristine) city.txt has."""
    return _count_sections(gi, CITY_VFS, r"\[Area\s+(\d+)\]", "area")


def _count_sections(gi: GameIndex, vfs_path: str, rx: str, what: str) -> int:
    try:
        text = gi.vfs.read_text(vfs_path)
    except FileNotFoundError as e:
        raise WorldmapError(f"cannot register FOC maps: base has no {vfs_path} "
                            f"({e}); nothing to extend")
    ids = [int(m) for m in re.findall(rx, text, re.I)]
    if not ids:
        raise WorldmapError(f"base {vfs_path} has no {what} sections")
    # Dense from 0: the engine reads until the first missing section.
    # Report the hole rather than guessing, like ids.py does for base growth.
    expect = list(range(len(ids)))
    if sorted(ids) != expect:
        raise WorldmapError(
            f"base {vfs_path} {what} sections are not dense from 0 "
            f"(found {min(ids)}..{max(ids)}, {len(ids)} of them)")
    return max(ids) + 1


# ------------------------------------------------------------- section text

def _map_section(rec: dict) -> list[str]:
    s = rec["settings"]
    if rec["id"] < FOC_START["map"]:
        raise WorldmapError(
            f"map {rec['filename']}: id {rec['id']} is below the FOC map "
            f"start {FOC_START['map']}")
    stem = Path(rec["filename"]).stem.lower()
    lines = [f"[Map {rec['id']:03d}]"]
    lines.append(f"lookup_name={_lookup(rec)}")
    lines.append(f"map_name={stem}")
    music = s.get("music")
    if music:
        lines.append(f"music={music}")
    ambient = s.get("ambient", DEFAULT_AMBIENT).strip()
    if ambient:
        lines.append(f"ambient_sfx={ambient}")
    lines.append(f"saved={s.get('saved', 'Yes')}")
    lines.append(f"can_rest_here={s.get('can_rest_here', 'Yes,Yes,Yes')}")
    return lines


def _area_section(rec: dict, area_id: int, members: list[dict] | None = None) -> list[str]:
    s = rec["settings"]
    members = members or [rec]
    pos = s.get("world_pos")
    if not pos:
        raise WorldmapError(
            f"map {rec['filename']} (id {rec['id']}) has no world_pos=x,y - "
            f"it would never appear on the worldmap. Add it to its line in "
            f"mod/ids/maps.txt.")
    size = s.get("size", "Small").strip().lower()
    if size not in SPHERE_PX:
        raise WorldmapError(
            f"map {rec['filename']}: size={size!r} must be one of "
            f"{sorted(SPHERE_PX)}")
    area_name = s.get("area_name", rec["name"])
    lines = [f"[Area {area_id:02d}]"]
    lines.append(f"area_name={area_name}")
    lines.append(f"world_pos={pos}")
    lines.append(f"start_state={s.get('start_state', 'On')}")
    lines.append(f"size={size.capitalize()}")
    lines.append(f"townmap_art_idx={s.get('townmap_art_idx', -1)}")
    lines.append(f"townmap_label_art_idx={s.get('townmap_label_art_idx', -1)}")
    if "entrance_0" in s and len(members) == 1:      # legacy single-map override
        lines.append(f"entrance_0={s['entrance_0']}")
        return lines
    for i, m in enumerate(members):
        lines.append(f"entrance_{i}={_entrance_line(m, m is rec)}")
    return lines


def _msg_entries(rec: dict, area_id: int | None, base_map_msg: str) -> list[str]:
    """
    map.msg entries for one map: {map*3+elev+200} per elevation, and - for a
    map that owns an area - the city/area name at {1500+area}. Empty texts for
    unused elevations match how RPU fills all three elevation slots per map.
    """
    s = rec["settings"]
    map_id = rec["id"]
    elev0 = s.get("map_msg_0", rec["name"])
    elev1 = s.get("map_msg_1", "")
    elev2 = s.get("map_msg_2", "")
    out = [
        f"{{{map_id * 3 + 200}}}{{}}{{{elev0}}}",
        f"{{{map_id * 3 + 201}}}{{}}{{{elev1}}}",
        f"{{{map_id * 3 + 202}}}{{}}{{{elev2}}}",
    ]
    if area_id is not None:
        area_name = s.get("area_name", rec["name"])
        out.append(f"{{{1500 + area_id}}}{{}}{{{area_name}}}")
    return out


# ------------------------------------------------------------- the merges

def merged_maps_txt(gi: GameIndex, registry: list[dict] | None = None) -> str:
    registry = registry if registry is not None else read_maps_registry()
    base = gi.vfs.read_text(MAPS_VFS)
    base = base.rstrip("\r\n")
    n = base_map_count(gi)

    if FOC_START["map"] < n:
        raise WorldmapError(
            f"base maps.txt already has {n} maps, past FOC_START map at "
            f"{FOC_START['map']} - raise it in tools/ids.py and re-point "
            f"existing maps")

    # The engine reads maps.txt densely from [Map 000] and stops at the first
    # section without a lookup_name, so EVERY id from n up to the last FOC map
    # must have a section - both the n..FOC_START filler and any gaps BETWEEN
    # registered FOC maps (e.g. 201..206 when only 200 and 207 are built).
    by_id = {rec["id"]: rec for rec in registry}
    last = max([FOC_START["map"] - 1, *by_id]) if by_id else FOC_START["map"] - 1

    out = [base]
    for i in range(n, last + 1):
        out.append("")
        rec = by_id.get(i)
        if rec is not None and i >= FOC_START["map"]:
            out.extend(_map_section(rec))
        else:
            out.append(f"[Map {i:03d}]")
            out.append(f"lookup_name={RESERVED_NAME} {i}")
            out.append(f"map_name={RESERVED_MAP}{i}")

    return "\r\n".join(out) + "\r\n"


def hide_base_areas(city_txt: str) -> str:
    """Rewrite every base [Area NN] as hidden and locked (see BASE_AREAS_LEFT_ALONE)."""
    parts = re.split(r"(?m)^(?=\[Area\s+\d+\])", city_txt)
    out = [parts[0]]
    for sec in parts[1:]:
        area_id = int(re.match(r"\[Area\s+(\d+)\]", sec).group(1))
        if area_id in BASE_AREAS_LEFT_ALONE:
            out.append(sec)
            continue
        sec, n = re.subn(r"(?mi)^(\s*start_state\s*=\s*)[^\s;]+", r"\g<1>Off", sec)
        if n != 1:
            raise WorldmapError(f"base city.txt area {area_id}: expected one start_state line, found {n}")
        sec, n = re.subn(r"(?mi)^(\s*lock_state\s*=\s*)[^\s;]+", r"\g<1>On", sec)
        if n == 0:
            sec = re.sub(r"(?mi)^(\s*start_state\s*=[^\r\n]*)(\r?\n)",
                         r"\g<1>\g<2>lock_state=On\g<2>", sec, count=1)
        out.append(sec)
    return "".join(out)


def merged_city_txt(gi: GameIndex, registry: list[dict] | None = None) -> str:
    registry = registry if registry is not None else read_maps_registry()
    base = gi.vfs.read_text(CITY_VFS)
    if _config.own_worldmap():
        base = hide_base_areas(base)
    base = base.rstrip("\r\n")
    area0 = base_area_count(gi)

    out = [base]
    for i, (owner, members) in enumerate(area_groups(registry)):
        out.append("")
        out.extend(_area_section(owner, area0 + i, members))
    return "\r\n".join(out) + "\r\n"


def merged_map_msg(gi: GameIndex, registry: list[dict] | None = None) -> str:
    registry = registry if registry is not None else read_maps_registry()
    base = gi.vfs.read_text(MAP_MSG_VFS)
    base = base.rstrip("\r\n")
    area0 = base_area_count(gi)

    out = [base]
    area_of_owner = {owner["id"]: area0 + i
                     for i, (owner, _m) in enumerate(area_groups(registry))}
    for rec in registry:
        out.extend(_msg_entries(rec, area_of_owner.get(rec["id"]), base))
    return "\r\n".join(out) + "\r\n"


def _foc_tile_section(sec: str) -> str:
    """One base [Tile N] section rewritten for FOC's art (see SUBTILE_TERRAIN)."""
    name = re.match(r"\[Tile\s+\d+\]", sec).group(0)
    art = re.search(r"(?mi)^\s*art_idx\s*=\s*(\d+)", sec)
    if art is None:
        raise WorldmapError(f"base worldmap.txt {name} has no art_idx")
    lines = [name, f"art_idx={art.group(1)}"]
    diff = re.search(r"(?mi)^\s*encounter_difficulty\s*=\s*(-?\d+)", sec)
    if diff:
        lines.append(f"encounter_difficulty={diff.group(1)}")
    lines.append("; x_offset_yoffset=terrain,fill,morning,afternoon,night,encounter_type")
    for row in range(SUBTILE_ROWS):
        for col in range(SUBTILE_COLS):
            m = re.search(rf"(?m)^\s*{col}_{row}\s*=([^\r\n;]*)", sec)
            if m is None:
                raise WorldmapError(f"base worldmap.txt {name} has no subtile {col}_{row}")
            lines.append(f"{col}_{row}={SUBTILE_TERRAIN},No_Fill,"
                         f"{SUBTILE_FREQ},{SUBTILE_FREQ},{SUBTILE_FREQ},{FOC_ENCOUNTER_NAME}")
    return "\r\n".join(lines) + "\r\n\r\n"


def merged_worldmap_txt(gi: GameIndex) -> str:
    """
    The base worldmap.txt with every [Tile N] section replaced for FOC's art:
    same art_idx (the WRLDMP tiles are replaced by name), no walk mask, flat
    desert, no random encounters. Everything before the tiles (frequencies,
    terrain types, random-map lists, encounter tables) is kept verbatim.
    """
    base = gi.vfs.read_text(WORLDMAP_VFS)
    parts = re.split(r"(?m)^(?=\[)", base)
    out = []
    tiles = 0
    last_table = -1
    table_at = 0
    for sec in parts:
        if re.match(r"\[Tile\s+\d+\]", sec):
            out.append(_foc_tile_section(sec))
            tiles += 1
        else:
            m = re.match(r"\[Encounter Table\s+(\d+)\]", sec)
            if m:
                last_table = int(m.group(1))
                table_at = len(out)
            out.append(sec)
    if tiles != 20:
        raise WorldmapError(f"base worldmap.txt has {tiles} tiles; FOC's art has 20")
    if last_table < 0:
        raise WorldmapError("base worldmap.txt has no [Encounter Table N] sections")
    # the engine reads tables by number until one is missing, so ours is the next
    ours = ("\r\n".join(FOC_ENCOUNTER_GROUPS) + "\r\n"
            + f"[Encounter Table {last_table + 1}]\r\nlookup_name={FOC_ENCOUNTER_NAME}\r\n"
            + "\r\n".join(FOC_ENCOUNTER_TABLE) + "\r\n\r\n")
    out.insert(table_at + 1, ours)
    return "".join(out)


def foc_encounter_table_number(gi: GameIndex) -> int:
    """The number merged_worldmap_txt gives FOC's table: one past the base's last."""
    nums = [int(n) for n in re.findall(r"(?m)^\[Encounter Table\s+(\d+)\]",
                                       gi.vfs.read_text(WORLDMAP_VFS))]
    if not nums:
        raise WorldmapError("base worldmap.txt has no [Encounter Table N] sections")
    return max(nums) + 1


def merged_worldmap_msg(gi: GameIndex) -> str:
    """The base worldmap.msg plus the "You encounter: ..." names of FOC's table.
    Without them the engine prints "Error" in the message window."""
    enc_lines = [l for l in FOC_ENCOUNTER_TABLE if l.startswith("enc_")]
    if len(enc_lines) != len(FOC_ENCOUNTER_TEXT):
        raise WorldmapError("FOC_ENCOUNTER_TEXT needs one name per enc_NN line")
    first = 3000 + 50 * foc_encounter_table_number(gi)
    base = gi.vfs.read_text(WORLDMAP_MSG_VFS).rstrip("\r\n")
    for i in range(len(enc_lines)):
        if re.search(r"(?m)^\{%d\}" % (first + i), base):
            raise WorldmapError(f"worldmap.msg already has {{{first + i}}}")
    ours = [f"{{{first + i}}}{{}}{{{t}}}" for i, t in enumerate(FOC_ENCOUNTER_TEXT)]
    return base + "\r\n\r\n# FOC_Basin encounter names (tools/worldmap.py)\r\n" + "\r\n".join(ours) + "\r\n"


# --------------------------------------------------------------- validation

def check(gi: GameIndex, registry: list[dict] | None = None) -> list[str]:
    """
    Return problems with the worldmap registration; empty means it is sound.
    Deploy and validate both call this so the two can never disagree.
    """
    problems: list[str] = []
    registry = registry if registry is not None else read_maps_registry()
    if not registry:
        return problems

    base_maps = base_map_count(gi)
    base_areas = base_area_count(gi)

    try:
        groups = area_groups(registry)
    except WorldmapError as e:
        return [str(e)]

    # --- ids sit where they claim
    seen_ids: set[int] = set()
    for rec in registry:
        mid = rec["id"]
        if mid in seen_ids:
            problems.append(f"map id {mid} ({rec['filename']}) is listed twice")
        seen_ids.add(mid)
        if mid < FOC_START["map"]:
            problems.append(
                f"map {rec['filename']}: id {mid} is below FOC map start "
                f"{FOC_START['map']}")
        if mid < base_maps:
            problems.append(
                f"map {rec['filename']}: id {mid} collides with the base's "
                f"map {mid} ({base_maps} base maps)")

    # --- the merged maps.txt resolves every map
    maps_txt = merged_maps_txt(gi, registry)
    lookup_by_id: dict[int, str] = {}
    for m in re.finditer(r"\[Map\s+(\d+)\]\s+lookup_name=([^\r\n]+)",
                         maps_txt, re.I):
        lookup_by_id[int(m.group(1))] = m.group(2).strip()
    for rec in registry:
        if rec["id"] not in lookup_by_id:
            problems.append(
                f"map {rec['filename']}: no [Map {rec['id']:03d}] section in "
                f"generated maps.txt")
            continue
        stem = Path(rec["filename"]).stem.lower()
        mm = re.search(rf"\[Map {rec['id']:03d}\]\s+lookup_name=[^\r\n]+\s+"
                       rf"map_name=([^\r\n]+)", maps_txt)
        if mm and mm.group(1).strip() != stem:
            problems.append(
                f"map {rec['filename']}: map_name {mm.group(1)!r} != filename "
                f"stem {stem!r}")
    seen_lookup: set[str] = set()
    for mid, lname in lookup_by_id.items():
        k = lname.lower()
        if k in seen_lookup:
            problems.append(
                f"lookup_name {lname!r} (map {mid}) is not unique - "
                f"entrances could resolve to the wrong map")
        seen_lookup.add(k)

    # --- city.txt entrances resolve, areas do not overlap On areas
    city_txt = merged_city_txt(gi, registry)
    areas = parse_areas(city_txt)
    for area_id, area in sorted(areas.items()):
        for ent in area["entrances"]:
            lname = ent["map"]
            if lname.lower() not in {v.lower() for v in lookup_by_id.values()}:
                problems.append(
                    f"city.txt area {area_id} entrance {ent['idx']} references "
                    f"map {lname!r}, which is not a maps.txt lookup_name")
    for area_id, area in sorted(areas.items()):
        if area["start_state"].lower() != "on":
            continue
        d = SPHERE_PX[area["size"].lower()]
        w = h = d
        x, y = area["world_pos"]
        for other_id, other in sorted(areas.items()):
            if other_id == area_id:
                continue
            if other["start_state"].lower() != "on":
                continue
            od = SPHERE_PX[other["size"].lower()]
            ow = oh = od
            ox, oy = other["world_pos"]
            if x < ox + ow and ox < x + w and y < oy + oh and oy < y + h:
                problems.append(
                    f"city.txt area {area_id} ({area['name']}) worldmap "
                    f"circle overlaps area {other_id} ({other['name']}) at "
                    f"({x},{y}) vs ({ox},{oy})")

    # --- map.msg has every id the engine will ask for
    msg = merged_map_msg(gi, registry)
    have_ids = set(int(m) for m in re.findall(r"\{(\d+)\}\{", msg))
    for rec in registry:
        mid = rec["id"]
        need = {mid * 3 + 200, mid * 3 + 201, mid * 3 + 202}
        missing = need - have_ids
        if missing:
            problems.append(
                f"map.msg is missing elevation-name entries "
                f"{sorted(missing)} for map {mid} ({rec['filename']})")
    for i, (owner, _m) in enumerate(groups):
        aid = base_areas + i
        if (1500 + aid) not in have_ids:
            problems.append(
                f"map.msg is missing area-name entry {{1500+{aid}}} for "
                f"{owner['filename']}")

    # --- no base area can show up on FOC's map
    base_ids = (set(range(base_areas)) - BASE_AREAS_LEFT_ALONE) if _config.own_worldmap() else set()
    for area_id in sorted(base_ids & set(areas)):
        if areas[area_id]["start_state"].lower() != "off":
            problems.append(f"city.txt base area {area_id} ({areas[area_id]['name']}) "
                            f"still starts visible")
    locked = {int(m.group(1)) for m in re.finditer(
        r"\[Area\s+(\d+)\][^\[]*?lock_state=On", city_txt, re.I)}
    if base_ids - locked:
        problems.append(f"city.txt base areas not locked: {sorted(base_ids - locked)}")

    # --- worldmap.txt + FOC's worldmap art
    try:
        wm = merged_worldmap_txt(gi) if _config.own_worldmap() else ""
        if re.search(r"(?mi)^\s*walk_mask_name", wm):
            problems.append("generated worldmap.txt still has a walk_mask_name")
    except WorldmapError as e:
        problems.append(str(e))
    art_dir = ROOT / "mod" / "data" / "art" / "intrface"
    for t in (range(20) if _config.own_worldmap() else ()):
        p = art_dir / f"WRLDMP{t:02d}.FRM"
        if not p.is_file():
            problems.append(f"FOC worldmap tile {p.name} missing from mod/data/art/intrface")
            continue
        w, h = struct.unpack_from(">HH", p.read_bytes(), 0x3E)
        if (w, h) != (350, 300):
            problems.append(f"{p.name} is {w}x{h}, the engine draws tiles as 350x300")

    # --- music files exist in the running install
    for rec in registry:
        music = rec["settings"].get("music")
        if music:
            d = gi.base / "sound" / "music"
            mod_d = Path(__file__).resolve().parent.parent / "mod" / "data" / "sound" / "music"
            # sfall plays mp3/wma/wav in place of a missing .acm (ddraw.ini AllowDShowSound=1)
            p = next((q for q in (x / f"{music}{e}" for x in (d, mod_d) for e in (".acm", ".mp3", ".wma", ".wav")) if q.is_file()), d / f"{music}.acm")
            if not p.is_file():
                problems.append(
                    f"map {rec['filename']}: music={music!r} - no "
                    f"sound/music/{music}.acm in the install")

    return problems


def parse_areas(city_txt: str) -> dict[int, dict]:
    """Parse city.txt into {area_id: {...}} for validation."""
    out: dict[int, dict] = {}
    cur: dict | None = None
    cur_id = -1
    for line in city_txt.splitlines():
        s = line.split(";", 1)[0].strip()
        if not s:
            continue
        m = re.match(r"\[Area\s+(\d+)\]", s, re.I)
        if m:
            cur_id = int(m.group(1))
            cur = {"id": cur_id, "name": "", "world_pos": (0, 0),
                   "start_state": "Off", "size": "Small", "entrances": []}
            out[cur_id] = cur
            continue
        if cur is None or "=" not in s:
            continue
        k, _, v = s.partition("=")
        k = k.strip().lower()
        v = v.strip()
        if k == "area_name":
            cur["name"] = v
        elif k == "world_pos":
            try:
                x, y = (int(t) for t in v.split(",")[:2])
                cur["world_pos"] = (x, y)
            except ValueError:
                pass
        elif k == "start_state":
            cur["start_state"] = v
        elif k == "size":
            cur["size"] = v
        elif k.startswith("entrance_"):
            idx = int(k.split("_", 1)[1])
            fields = [f.strip() for f in v.split(",")]
            cur["entrances"].append({
                "idx": idx,
                "state": fields[0] if fields else "",
                "map": fields[3] if len(fields) > 3 else "",
            })
    return out


# --------------------------------------------------------------------- main

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="exit non-zero if the worldmap registration is broken")
    ap.add_argument("--base", type=Path, default=None)
    args = ap.parse_args()

    gi = GameIndex(inside(args.base) if args.base else None)
    registry = read_maps_registry()
    problems = check(gi, registry)

    if args.check:
        for p in problems:
            print("ERROR: " + p)
        if not problems:
            print(f"worldmap: {len(registry)} FOC map(s) registered over a "
                  f"base of {base_map_count(gi)} maps / "
                  f"{base_area_count(gi)} areas")
        return 1 if problems else 0

    print(f"base: {base_map_count(gi)} maps, {base_area_count(gi)} areas, "
          f"{len(registry)} FOC map(s)")
    for i, (owner, members) in enumerate(area_groups(registry)):
        area = base_area_count(gi) + i
        print(f"  area {area} {owner['settings'].get('area_name', owner['name'])!r} "
              f"world_pos={owner['settings'].get('world_pos', '<missing!>')}")
        for k, rec in enumerate(members):
            state = _entrance_line(rec, rec is owner).split(",")[0]
            print(f"    entrance_{k} [{rec['id']:03d}] {Path(rec['filename']).stem:<12} "
                  f"lookup={_lookup(rec)!r:<20} {state}")
    for p in problems:
        print("ERROR: " + p)
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())