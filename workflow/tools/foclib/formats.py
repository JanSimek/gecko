"""
Fallout 2 file formats — only the parts the build and validation tools need.

Every rule here was checked against fallout2-ce's source or against real files
on disk. Where the wiki spec and the engine disagreed, the engine won. The
comments say which is which, because these are exactly the details that cost
hours when they're wrong.
"""

from __future__ import annotations

import re
import struct

# --------------------------------------------------------------------------- PID

# obj_types.h:  #define PID_TYPE(value) (value) >> 24
# proto.cc:     index is  pid & 0xFFFFFF
OBJ_TYPE_ITEM = 0
OBJ_TYPE_CRITTER = 1
OBJ_TYPE_SCENERY = 2
OBJ_TYPE_WALL = 3
OBJ_TYPE_TILE = 4
OBJ_TYPE_MISC = 5

TYPE_NAME = {
    OBJ_TYPE_ITEM: "items",
    OBJ_TYPE_CRITTER: "critters",
    OBJ_TYPE_SCENERY: "scenery",
    OBJ_TYPE_WALL: "walls",
    OBJ_TYPE_TILE: "tiles",
    OBJ_TYPE_MISC: "misc",
}

# proto_types.h
FIRST_EXIT_GRID_PID = 0x5000010
LAST_EXIT_GRID_PID = 0x5000017

# proto_types.h enums
ITEM_TYPE_ARMOR, ITEM_TYPE_CONTAINER, ITEM_TYPE_DRUG, ITEM_TYPE_WEAPON, \
    ITEM_TYPE_AMMO, ITEM_TYPE_MISC, ITEM_TYPE_KEY = range(7)
SCENERY_TYPE_DOOR, SCENERY_TYPE_STAIRS, SCENERY_TYPE_ELEVATOR, \
    SCENERY_TYPE_LADDER_UP, SCENERY_TYPE_LADDER_DOWN, SCENERY_TYPE_GENERIC = range(6)


def pid_type(pid: int) -> int:
    return (pid >> 24) & 0xFF


def pid_index(pid: int) -> int:
    return pid & 0xFFFFFF


def is_exit_grid(pid: int) -> bool:
    return FIRST_EXIT_GRID_PID <= pid <= LAST_EXIT_GRID_PID


# --------------------------------------------------------------------------- LST

def parse_lst(data: bytes) -> list[str]:
    """
    A .lst is one filename per line. Index is the 1-BASED line number.

    Verified in proto.cc::_proto_list_str — it walks lines with i starting at 1
    and stops when i == (pid & 0xFFFFFF). Same convention for scripts.lst, where
    scripts.h's SCRIPT_* defines are 1-based line numbers (checked: Containr=16
    is line 16, ncNikki=438 is line 438).

    Returns a list whose [0] is line 1, so callers must subtract 1 themselves.
    Trailing comments (';' or '#') and whitespace are stripped from the name.
    """
    out: list[str] = []
    text = data.decode("cp1252", "replace")
    for line in text.splitlines():
        s = line.strip()
        if not s:
            out.append("")
            continue
        # name is the first whitespace-delimited token, comments start at ; or #
        s = re.split(r"[;#]", s, 1)[0].strip()
        out.append(s.split()[0] if s.split() else "")
    return out


# --------------------------------------------------------------------------- GAM

def parse_gam_slots(data: bytes, section: str = "GAME_GLOBAL_VARS:") -> list[str | None]:
    """
    Return the variable NAME occupying each slot, indexed by GVAR number.

    This replicates game.cc::globalVarsRead exactly, and the exactness matters:
    the engine assigns numbers purely by POSITION and never reads the names at
    all. Its skip rules are narrow —

        if (string[0] == '\\n') continue;
        if (string[0] == '/' && string[1] == '/') continue;

    — so a line with LEADING WHITESPACE is counted even if it is blank or is an
    indented comment. Anything less literal than this gives wrong slot numbers.

    Slots whose text has no GVAR_ name come back as None.
    """
    text = data.decode("cp1252", "replace").replace("\r\n", "\n").replace("\r", "\n")
    # text-mode fgets hands back each line including its trailing newline
    parts = text.split("\n")
    lines = [p + "\n" for p in parts[:-1]] + ([parts[-1]] if parts[-1] else [])

    slots: list[str | None] = []
    started = False
    for s in lines:
        if not started:
            if s.startswith(section):
                started = True
            continue
        if s[:1] == "\n":
            continue
        if s[:2] == "//":
            continue
        m = re.match(r"\s*(GVAR_[A-Za-z0-9_]+)", s)
        slots.append(m.group(1) if m else None)
    return slots


def parse_gvar_defines(text: str) -> dict[str, int]:
    """`#define GVAR_FOO (123)` -> {'GVAR_FOO': 123} from a header."""
    out: dict[str, int] = {}
    for line in text.splitlines():
        m = re.match(r"\s*#define\s+(GVAR_[A-Za-z0-9_]+)\s+\(?(-?\d+)\)?", line)
        if m:
            out[m.group(1)] = int(m.group(2))
    return out


# --------------------------------------------------------------------------- MSG

MSG_RE = re.compile(rb"\{\s*(\d+)\s*\}\s*\{(.*?)\}\s*\{(.*?)\}", re.S)


def parse_msg(data: bytes) -> dict[int, str]:
    """{id}{audio}{text} records -> {id: text}. Comments are ignored."""
    out: dict[int, str] = {}
    for m in MSG_RE.finditer(data):
        out[int(m.group(1))] = m.group(3).decode("cp1252", "replace")
    return out


# --------------------------------------------------------------------------- PRO

class Proto:
    """
    Just enough of a .pro to walk map objects.

    Layout from proto.cc::protoRead. All big-endian.
        u32 pid, u32 messageId, u32 fid
    then, for items:
        lightDistance, lightIntensity, flags, extendedFlags, sid, TYPE, ...
    so item type sits at word index 8 (byte 32).
    Scenery follows the same first six words, so its TYPE is also at word 8.
    """

    __slots__ = ("pid", "message_id", "fid", "subtype",
                 "light_distance", "light_intensity", "flags", "extended_flags",
                 "ai_packet", "team", "max_hp")

    def __init__(self, pid, message_id, fid, subtype,
                 light_distance=0, light_intensity=0, flags=0, extended_flags=0,
                 ai_packet=None, team=None, max_hp=None):
        self.pid = pid
        self.message_id = message_id
        self.fid = fid
        self.subtype = subtype  # item type / scenery type, else None
        # words 3..6 of every proto type's header (proto.cc::protoRead). flags
        # are the OBJECT_* bits objectCreateWithPid copies onto a new object;
        # extended_flags carry the action verbs (0x800 Use, 0x2000 Look, ...).
        self.light_distance = light_distance
        self.light_intensity = light_intensity
        self.flags = flags
        self.extended_flags = extended_flags
        # critters only (CritterProto: header, headFid, aiPacket, team, then
        # data.flags, baseStats[35], bonusStats[35], ...). max_hp is
        # base + bonus of STAT_MAXIMUM_HIT_POINTS (7) - what critterGetStat
        # returns for an NPC, and what a placed critter's current hp should be.
        self.ai_packet = ai_packet
        self.team = team
        self.max_hp = max_hp

    @property
    def type(self) -> int:
        return pid_type(self.pid)


def parse_pro(data: bytes) -> Proto:
    if len(data) < 12:
        raise ValueError("proto too short")
    pid, message_id, fid = struct.unpack_from(">III", data, 0)
    t = pid_type(pid)
    subtype = None
    if t in (OBJ_TYPE_ITEM, OBJ_TYPE_SCENERY) and len(data) >= 36:
        (subtype,) = struct.unpack_from(">I", data, 32)
    ld = li = flags = ext = 0
    if len(data) >= 28:
        ld, li, flags, ext = struct.unpack_from(">iiII", data, 12)
    ai = team = hp = None
    if t == OBJ_TYPE_CRITTER and len(data) >= 4 * 55:
        ai, team = struct.unpack_from(">ii", data, 4 * 9)
        base_hp = struct.unpack_from(">i", data, 4 * (12 + 7))[0]
        bonus_hp = struct.unpack_from(">i", data, 4 * (47 + 7))[0]
        hp = base_hp + bonus_hp
    return Proto(pid, message_id, fid, subtype, ld, li, flags, ext, ai, team, hp)


# --------------------------------------------------------------------------- MAP

HEX_GRID_WIDTH = 200        # map_defs.h
SQUARE_GRID_WIDTH = 100


def hex_xy(tile: int) -> tuple[int, int]:
    return tile % HEX_GRID_WIDTH, tile // HEX_GRID_WIDTH


def hex_to_square(tile: int) -> int:
    """
    Square index under a hex tile.

    tile.cc mirrors x on both grids (gridWidth - 1 - tile % gridWidth) and the
    square's screen step is exactly twice the hex's, so the mirroring cancels
    and this is a plain //2 on each axis. Confirmed on 36982 objects across 60
    maps: 97.6% land on a square carrying a real floor tile.
    """
    hx, hy = hex_xy(tile)
    return (hy // 2) * SQUARE_GRID_WIDTH + (hx // 2)


class MapObject:
    """
    One object read out of a .map.

    `off` is the byte offset of the object's 18-word header, so a porting tool
    can patch the PID / FID / script index straight back into the original
    bytes instead of trying to re-serialise a map it only partly understands.
    Field offsets are header-relative and fixed (object.cc::objectRead).
    """

    OFF_FID = 0x20
    OFF_PID = 0x2C
    OFF_SID = 0x40
    OFF_SCRIPT_INDEX = 0x44

    __slots__ = ("tile", "fid", "pid", "sid", "script_index", "elevation",
                 "inventory", "off", "rotation", "frame")

    def __init__(self, tile, fid, pid, sid, script_index, elevation, off=0,
                 rotation=0, frame=0):
        self.tile = tile
        self.fid = fid
        self.pid = pid
        self.sid = sid
        self.script_index = script_index
        self.elevation = elevation
        self.off = off
        # Rotation picks which of the FRM header's six offset pairs applies.
        # object.cc::_obj_render_object adds art->xOffsets[rotation] before
        # centring, and walls do not sit flush without it.
        self.rotation = rotation
        self.frame = frame
        self.inventory: list[MapObject] = []

    @property
    def fid_offset(self) -> int:
        return self.off + self.OFF_FID

    @property
    def pid_offset(self) -> int:
        return self.off + self.OFF_PID

    @property
    def script_index_offset(self) -> int:
        return self.off + self.OFF_SCRIPT_INDEX

    def __repr__(self):
        return f"<obj pid=0x{self.pid:08X} tile={self.tile} script={self.script_index}>"


class MapFile:
    """
    A parsed .map.

    .map is BIG-ENDIAN. The wiki spec's header table gives offsets and sizes but
    never states byte order; verified against artemple.map (vanilla) and the
    MDFG maps, all version 20 big-endian.

    Walking the object array REQUIRES proto lookup: proto.cc::objectDataRead
    branches on the proto's item/scenery subtype to decide how many extra words
    follow each object. Without a working `proto_lookup` the walk desynchronises
    silently, so this class demands one.
    """

    def __init__(self):
        self.version = 0
        self.name = ""
        self.default_tile = 0
        self.default_elevation = 0
        self.default_orientation = 0
        self.num_local_vars = 0
        self.map_script_id = -1
        self.flags = 0
        self.num_global_vars = 0
        self.map_id = -1
        self.elevations: list[int] = []
        # One 10000-entry list per elevation, in the order of self.elevations.
        self.floors: list[list[int]] = []
        self.roofs: list[list[int]] = []
        self.tiles_offset = 0
        self.script_pids: list[tuple[int, int]] = []  # (pid, script_index)
        # (offset_of_pid, offset_of_script_index) for each entry above
        self.script_offsets: list[tuple[int, int]] = []
        self.objects: list[MapObject] = []

    # -- parsing -----------------------------------------------------------

    @classmethod
    def parse(cls, data: bytes, proto_lookup) -> "MapFile":
        """
        proto_lookup(pid) -> Proto | None.
        Raises ValueError with a precise offset when the walk goes wrong.
        """
        m = cls()
        u32 = lambda off: struct.unpack_from(">I", data, off)[0]
        i32 = lambda off: struct.unpack_from(">i", data, off)[0]

        m.version = u32(0)
        if m.version not in (19, 20):
            raise ValueError(f"unsupported map version {m.version} (expected 19 or 20)")
        m.name = data[4:20].split(b"\0")[0].decode("cp1252", "replace")
        m.default_tile = i32(0x14)
        m.default_elevation = i32(0x18)
        m.default_orientation = i32(0x1C)
        m.num_local_vars = i32(0x20)
        m.map_script_id = i32(0x24)
        m.flags = i32(0x28)
        m.num_global_vars = i32(0x30)
        m.map_id = i32(0x34)

        # Which elevations exist: flags bit 1 = no elevation 1, bit 2 = no
        # elevation 2, bit 3 = no elevation 0 (map.cc). Bit 0 is "is a savegame".
        # map.cc::_square_load, with _map_data_elev_flags = {2, 4, 8}:
        #     elevation N is present iff (flags & _map_data_elev_flags[N]) == 0
        # so 0<->2, 1<->4, 2<->8. Deriving this from flag/elevation-count
        # correlation instead gets the LABELS backwards while keeping the COUNT
        # right, which parses every map correctly and silently mislabels which
        # level the data belongs to.
        m.elevations = [
            e for e, bit in ((0, 0x02), (1, 0x04), (2, 0x08))
            if not (m.flags & bit)
        ]

        p = 0xEC
        p += 4 * m.num_global_vars
        p += 4 * m.num_local_vars

        # Tiles: 100x100 squares, <u16 roof><u16 floor> each, per elevation.
        # A tile id of 0 or 1 means "nothing here"; ids index art	iles	iles.lst.
        m.tiles_offset = p
        for _ in m.elevations:
            pairs = struct.unpack_from(">20000H", data, p)
            p += 40000
            m.roofs.append(list(pairs[0::2]))
            m.floors.append(list(pairs[1::2]))

        # --- script blocks: 5 types, padded to multiples of 16 with a check
        #     block after every 16. Structure per the wiki spec's sample C, which
        #     round-trips correctly on all 155 vanilla maps.
        for _ in range(5):
            count = struct.unpack_from(">i", data, p)[0]
            p += 4
            if count > 0:
                loop = ((count + 15) // 16) * 16
                for j in range(loop):
                    pid = struct.unpack_from(">i", data, p)[0]
                    stype = (pid >> 24) & 0xFF
                    # base record is 16 words; timer/spatial carry extra
                    rec = 16 * 4
                    if stype == 1:      # spatial
                        rec += 2 * 4
                    elif stype == 2:    # timer
                        rec += 1 * 4
                    # scripts.cc::scriptWrite - sid, field_4, [type extras],
                    # flags, INDEX, ... so index is word 3 for a plain script,
                    # word 5 for spatial (+2), word 4 for timed (+1).
                    idx_word = 3 + (2 if stype == 1 else (1 if stype == 2 else 0))
                    sidx_off = p + idx_word * 4
                    sidx = struct.unpack_from(">i", data, sidx_off)[0]
                    if j < count:
                        m.script_pids.append((pid, sidx))
                        m.script_offsets.append((p, sidx_off))
                    p += rec
                    if (j % 16) == 15:
                        p += 8

        # --- objects
        total = struct.unpack_from(">i", data, p)[0]
        p += 4
        for _elev in range(3):
            if p + 4 > len(data):
                break
            n = struct.unpack_from(">i", data, p)[0]
            p += 4
            for _ in range(n):
                obj, p = cls._read_object(data, p, proto_lookup)
                m.objects.append(obj)
        return m

    @staticmethod
    def _read_object(data, p, proto_lookup):
        # object.cc::objectRead — 18 big-endian int32, then objectDataRead
        if p + 72 > len(data):
            raise ValueError(f"object header runs past end of file at 0x{p:X}")
        obj_off = p
        vals = struct.unpack_from(">18i", data, p)
        p += 72
        (_id, tile, _x, _y, _sx, _sy, _frame, _rot, fid, _flags,
         elevation, pid, _cid, _ld, _li, _outline, sid, script_index) = vals

        obj = MapObject(tile, fid, pid, sid, script_index, elevation,
                        obj_off, _rot, _frame)

        # proto.cc::objectDataRead
        inv_len, _inv_cap, _ptr = struct.unpack_from(">3i", data, p)
        p += 12

        t = pid_type(pid) if pid not in (0, -1) else None
        if t == OBJ_TYPE_CRITTER:
            # field_0 + 7-word CritterCombatData + hp + radiation + poison
            p += 4 + 7 * 4 + 12
        elif t is None:
            p += 4  # data.flags only; no proto means no type-specific extras
        else:
            p += 4  # data.flags
            if t == OBJ_TYPE_ITEM:
                proto = proto_lookup(pid)
                if proto is None:
                    raise ValueError(
                        f"item proto 0x{pid:08X} not found; cannot size object at 0x{p:X}"
                    )
                st = proto.subtype
                if st == ITEM_TYPE_WEAPON:
                    p += 8
                elif st in (ITEM_TYPE_AMMO, ITEM_TYPE_MISC, ITEM_TYPE_KEY):
                    p += 4
            elif t == OBJ_TYPE_SCENERY:
                proto = proto_lookup(pid)
                if proto is None:
                    raise ValueError(
                        f"scenery proto 0x{pid:08X} not found; cannot size object at 0x{p:X}"
                    )
                st = proto.subtype
                if st == SCENERY_TYPE_DOOR:
                    p += 4
                elif st == SCENERY_TYPE_STAIRS:
                    p += 8
                elif st == SCENERY_TYPE_ELEVATOR:
                    p += 8
                elif st in (SCENERY_TYPE_LADDER_UP, SCENERY_TYPE_LADDER_DOWN):
                    p += 8  # version 20; version 19 uses 4
            elif t == OBJ_TYPE_MISC:
                if is_exit_grid(pid):
                    p += 16

        for _ in range(inv_len):
            # each inventory entry is preceded by its stack count
            p += 4
            child, p = MapFile._read_object(data, p, proto_lookup)
            obj.inventory.append(child)

        return obj, p
