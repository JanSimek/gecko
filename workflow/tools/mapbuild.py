#!/usr/bin/env python3
"""
mapbuild.py - write a valid Fallout 2 .map from scratch.

Everything here mirrors foclib.formats' reader, which is verified against all
270 maps in the base (816,781 objects, zero failures). Writer and reader being
symmetric is the correctness argument: tools/selftest/test_mapbuild.py writes a
map, reads it back with the normal parser, and compares.

THE ELEVATION TRAP
------------------
A single-level map lives on ELEVATION 0. From map.cc::_square_load with
_map_data_elev_flags = {2, 4, 8}, elevation N is present iff
(flags & flag[N]) == 0:

    flags bit 0x02 -> elevation 0 absent
    flags bit 0x04 -> elevation 1 absent
    flags bit 0x08 -> elevation 2 absent

173 of 270 base maps use flags=0x0C, which is 4|8 - elevations 1 and 2 absent,
elevation 0 present.

Get it wrong and the map still loads, scripts still bind, and walls and objects
still draw - because objects carry their own elevation field - but the tile data
sits on a level the player never stands on. Three of MDFG's four maps have
exactly this defect. It was NOT, however, the cause of the black floor
reproduced here - see EMPTY_TILE below - so treat it as a real bug worth
preventing rather than as the explanation for anything.

This class derives the flags from the elevations it was given and refuses a
start elevation that is not among them, so the mistake cannot be made silently.

All values are BIG-ENDIAN. The wiki spec never says so.
"""

from __future__ import annotations

import struct

SQ_W = 100
SQ_H = 100
HEX_W = 200
SQUARES = SQ_W * SQ_H

MAP_VERSION = 20

# "No tile here" is 1, NOT 0. The engine compares against
# buildFid(OBJ_TYPE_TILE, 1, ...) (map.cc), and across four real maps 64,876
# empty floor squares carry 1 and not one carries 0. Initialising the grid to
# zero hands the renderer ~9,900 squares claiming tile index 0 - a real index,
# pointing at art that is not there - and the floor does not draw at all, while
# walls and objects, which carry their own art, render normally.
#
# That is the black floor. It looks exactly like an elevation problem and is not.
EMPTY_TILE = 1

# proto_types.h enums, needed to size an object's type-specific extras
ITEM_TYPE_WEAPON, ITEM_TYPE_AMMO, ITEM_TYPE_MISC, ITEM_TYPE_KEY = 3, 4, 5, 6
SCENERY_TYPE_DOOR, SCENERY_TYPE_STAIRS, SCENERY_TYPE_ELEVATOR = 0, 1, 2
SCENERY_TYPE_LADDER_UP, SCENERY_TYPE_LADDER_DOWN = 3, 4
FIRST_EXIT_GRID_PID, LAST_EXIT_GRID_PID = 0x5000010, 0x5000017


class MapObjectSpec:
    """One object to write. `extras` is the type-specific tail, in words."""

    __slots__ = ("pid", "fid", "tile", "elevation", "script_index", "sid",
                 "rotation", "frame", "flags", "extras", "inventory", "count")

    def __init__(self, pid, fid, tile, elevation, script_index=-1, sid=-1,
                 rotation=0, frame=0, flags=0, extras=(), inventory=None, count=1):
        self.pid = pid
        self.fid = fid
        self.tile = tile
        self.elevation = elevation
        self.script_index = script_index
        self.sid = sid
        self.rotation = rotation
        self.frame = frame
        self.flags = flags
        self.extras = tuple(extras)
        self.inventory = inventory or []
        self.count = count             # the stack's size, when this is in an inventory


class MapBuilder:
    def __init__(self, name: str, elevations=(1,), start_tile: int = 20100,
                 start_elevation: int | None = None, start_orientation: int = 0,
                 map_id: int = -1, map_script_index: int = -1):
        self.elevations = tuple(sorted(set(elevations)))
        if not self.elevations:
            raise ValueError("a map needs at least one elevation")
        for e in self.elevations:
            if e not in (0, 1, 2):
                raise ValueError(f"elevation {e} is not 0, 1 or 2")

        if start_elevation is None:
            # the map's lowest declared level; elevation 0 in the normal
            # single-level case
            start_elevation = self.elevations[0]
        if start_elevation not in self.elevations:
            raise ValueError(
                f"start elevation {start_elevation} is not among the elevations "
                f"this map declares {self.elevations}. That is the MDFG "
                f"black-floor bug: the map loads, objects draw, and the floor "
                f"cannot, because its tiles are on a level the player never "
                f"stands on."
            )

        self.name = name.upper()
        self.start_tile = start_tile
        self.start_elevation = start_elevation
        self.start_orientation = start_orientation
        self.map_id = map_id
        self.map_script_index = map_script_index   # 1-based; -1 for none

        # roof/floor id per square, per declared elevation
        self.floors = {e: [EMPTY_TILE] * SQUARES for e in self.elevations}
        self.roofs = {e: [EMPTY_TILE] * SQUARES for e in self.elevations}
        self.objects: list[MapObjectSpec] = []
        # Script table, keyed by script TYPE. Without an entry here an object's
        # script never binds, whatever its script_index field says - see
        # object.cc::objectLoadAllInternal, which looks the script up by SID and
        # then OVERWRITES scriptIndex from it.
        self.scripts: dict[int, list[int]] = {}

    # -- flags -------------------------------------------------------------

    @property
    def flags(self) -> int:
        # map.cc::_map_data_elev_flags = {2, 4, 8}: elevation N is present
        # iff (flags & flag[N]) == 0.
        f = 0
        if 0 not in self.elevations:
            f |= 0x02
        if 1 not in self.elevations:
            f |= 0x04
        if 2 not in self.elevations:
            f |= 0x08
        return f

    # -- tiles -------------------------------------------------------------

    def set_floor(self, elevation: int, sx: int, sy: int, tile_id: int) -> None:
        if not (0 <= sx < SQ_W and 0 <= sy < SQ_H):
            return
        self.floors[elevation][sy * SQ_W + sx] = tile_id or EMPTY_TILE

    def set_roof(self, elevation: int, sx: int, sy: int, tile_id: int) -> None:
        if not (0 <= sx < SQ_W and 0 <= sy < SQ_H):
            return
        self.roofs[elevation][sy * SQ_W + sx] = tile_id or EMPTY_TILE

    def fill_floor(self, elevation, x0, y0, x1, y1, pick) -> int:
        """
        `pick` is either a tile id or f(sx, sy) -> tile id.

        Real maps do not floor an area with one tile: the median map uses 91
        distinct floor tiles. Passing a function is the point.
        """
        n = 0
        for sy in range(y0, y1 + 1):
            for sx in range(x0, x1 + 1):
                tid = pick(sx, sy) if callable(pick) else pick
                if tid:
                    self.set_floor(elevation, sx, sy, tid)
                    n += 1
        return n

    # -- scripts -----------------------------------------------------------

    def add_script(self, script_type: int, script_index: int) -> int:
        """
        Register a script and return the SID to put on an object.

        script_index is the 0-BASED scripts.lst index, i.e. one less than the
        SCRIPT_* define (those are 1-based line numbers).

        Types, from real maps: 1 spatial, 2 timed, 3 items and scenery,
        4 critters.
        """
        lst = self.scripts.setdefault(script_type, [])
        lst.append(script_index)
        return (script_type << 24) | (len(lst) - 1)

    def _write_scripts(self, out: bytearray) -> None:
        """
        Five sections in type order. Each is a count, then records padded up to
        a multiple of 16, with two check words after every group of 16 - the
        first being the number of REAL entries in that group (verified against
        BROKEN2.map: 16, 16, 16, 5 for a count of 53).

        Padding in real maps is 0xCCCCCCCC, i.e. uninitialised memory; zeros
        work equally well since only the count is read back.
        """
        for stype in range(5):
            entries = self.scripts.get(stype, [])
            out += struct.pack(">i", len(entries))
            if not entries:
                continue
            loop = ((len(entries) + 15) // 16) * 16
            group_real = 0
            for j in range(loop):
                if j < len(entries):
                    sid = (stype << 24) | j
                    words = [sid, -1]
                    if stype == 1:            # spatial: built_tile, radius
                        words += [-1, 0]
                    elif stype == 2:          # timed: time
                        words += [0]
                    words += [
                        0,              # flags
                        entries[j],     # index into scripts.lst (0-based)
                        0,              # program pointer, meaningless in a file
                        -1,             # ownerId - the object's SID is the link
                        -1,             # localVarsOffset
                        0,              # localVarsCount
                        0, 0, 0, 0, 0,  # returnValue, action, fixedParam,
                                        # actionBeingUsed, scriptOverrides
                        0, 0, 0,        # field_48, howMuch, field_50
                    ]
                    group_real += 1
                else:
                    words = [0, -1] + [0] * 14
                out += struct.pack(f">{len(words)}i", *words)

                if (j % 16) == 15:
                    out += struct.pack(">2i", group_real, 0)
                    group_real = 0

    # -- objects -----------------------------------------------------------

    def add(self, obj: MapObjectSpec) -> MapObjectSpec:
        if obj.elevation not in self.elevations:
            raise ValueError(
                f"object at elevation {obj.elevation} but the map only declares "
                f"{self.elevations}"
            )
        self.objects.append(obj)
        return obj

    # -- serialisation -----------------------------------------------------

    def _write_object(self, out: bytearray, o: MapObjectSpec) -> None:
        out += struct.pack(
            ">18i",
            0,                 # id - the engine assigns its own
            o.tile,
            0, 0,              # x, y   (0 in 100% of real objects)
            0, 0,              # sx, sy (recomputed when rendered)
            o.frame,
            o.rotation,
            o.fid,
            o.flags,
            o.elevation,
            o.pid,
            -1,                # cid (-1 in 100% of real objects)
            0, 0,              # light distance / intensity
            0,                 # outline
            o.sid,
            o.script_index,
        )
        # objectDataRead: inventory header, then type-specific extras
        out += struct.pack(">3i", len(o.inventory), len(o.inventory), 0)
        out += struct.pack(">i", 0)          # data.flags / critter field_0
        for v in o.extras:
            out += struct.pack(">i", v)
        for child in o.inventory:
            out += struct.pack(">i", child.count)   # stack count
            self._write_object(out, child)

    def to_bytes(self) -> bytes:
        out = bytearray()

        name = self.name.encode("cp1252", "replace")[:15]
        out += struct.pack(">I", MAP_VERSION)
        out += name + b"\0" * (16 - len(name))
        out += struct.pack(
            ">iiiiiiiiiI",
            self.start_tile,
            self.start_elevation,
            self.start_orientation,
            0,                       # num_local_vars
            self.map_script_index,   # 1-based; engine stores index-1
            self.flags,
            1,                       # darkness - 1 in all 270 base maps
            0,                       # num_global_vars
            self.map_id,
            0,                       # time ticks
        )
        out += b"\0" * (44 * 4)      # unknown/zero block -> header ends at 0xEC
        assert len(out) == 0xEC, len(out)

        # no global or local vars, so the tile block starts here
        for e in self.elevations:
            roofs, floors = self.roofs[e], self.floors[e]
            out += struct.pack(">20000H",
                               *[v for pair in zip(roofs, floors) for v in pair])

        self._write_scripts(out)

        by_elev = {e: [o for o in self.objects if o.elevation == e]
                   for e in (0, 1, 2)}
        out += struct.pack(">i", len(self.objects))
        for e in (0, 1, 2):
            group = by_elev[e]
            out += struct.pack(">i", len(group))
            for o in group:
                self._write_object(out, o)

        return bytes(out)


# ------------------------------------------------------------- proto helpers

def scenery_extras(subtype: int | None) -> tuple:
    if subtype == SCENERY_TYPE_DOOR:
        return (0,)
    if subtype in (SCENERY_TYPE_STAIRS, SCENERY_TYPE_ELEVATOR,
                   SCENERY_TYPE_LADDER_UP, SCENERY_TYPE_LADDER_DOWN):
        return (0, 0)
    return ()


def item_extras(subtype: int | None) -> tuple:
    if subtype == ITEM_TYPE_WEAPON:
        return (0, -1)
    if subtype in (ITEM_TYPE_AMMO, ITEM_TYPE_MISC, ITEM_TYPE_KEY):
        return (0,)
    return ()


def critter_extras() -> tuple:
    # 7-word CritterCombatData then hp / radiation / poison
    return (0, 0, 0, 0, 1, 0, -1, 0, 0, 0)


def extras_for(obj_type: int, pid: int, subtype: int | None) -> tuple:
    if obj_type == 1:                     # critter
        return critter_extras()
    if obj_type == 0:                     # item
        return item_extras(subtype)
    if obj_type == 2:                     # scenery
        return scenery_extras(subtype)
    if obj_type == 5 and FIRST_EXIT_GRID_PID <= pid <= LAST_EXIT_GRID_PID:
        return (-1, -1, 0, 0)             # dest map / tile / elevation / rotation
    return ()


def hex_of(sx: int, sy: int, ox: int = 0, oy: int = 0) -> int:
    """Hex tile at the top-left corner of square (sx, sy), plus a hex offset."""
    return (sy * 2 + oy) * HEX_W + (sx * 2 + ox)
