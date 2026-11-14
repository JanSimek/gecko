"""
foclib — shared plumbing for the FOC mod build/validate/port tools.

`GameIndex` is the thing most callers want: it wraps a base install and answers
"what does this ID mean", using the same resolution order the engine uses.
"""

from __future__ import annotations

from pathlib import Path

from .dat2 import Dat2, Dat2Error
from .formats import (
    OBJ_TYPE_CRITTER,
    OBJ_TYPE_ITEM,
    OBJ_TYPE_MISC,
    OBJ_TYPE_SCENERY,
    OBJ_TYPE_TILE,
    OBJ_TYPE_WALL,
    TYPE_NAME,
    MapFile,
    Proto,
    hex_to_square,
    hex_xy,
    is_exit_grid,
    parse_gam_slots,
    parse_gvar_defines,
    parse_lst,
    parse_msg,
    parse_pro,
    pid_index,
    pid_type,
)
from .vfs import GameVFS
from . import config as _config

ROOT = Path(__file__).resolve().parent.parent.parent

__all__ = [
    "GameIndex", "GameVFS", "FOC_LAYER", "Dat2", "Dat2Error", "MapFile", "Proto", "ROOT",
    "parse_lst", "parse_msg", "parse_pro", "parse_gam_slots", "parse_gvar_defines",
    "pid_type", "pid_index", "is_exit_grid", "TYPE_NAME",
    "hex_to_square", "hex_xy",
    "OBJ_TYPE_ITEM", "OBJ_TYPE_CRITTER", "OBJ_TYPE_SCENERY",
    "OBJ_TYPE_WALL", "OBJ_TYPE_TILE", "OBJ_TYPE_MISC",
]


# The mod deploys as a FOLDER mod under base\mods\<FOC_LAYER>\, which sfall
# supports natively (LoadOrder.cpp accepts folders as well as .dat files).
FOC_LAYER = "foc"

# Anything matching these is OUR output, not the base. A pristine index must
# skip them: otherwise reading the ID baseline after a deploy counts our own
# additions as part of the base, and every later allocation is off by that many
# entries.
_OURS = ["mods\\" + FOC_LAYER, "loose:"]


class GameIndex:
    """
    The base's ID space, resolved through the engine's override chain.

    pristine=True (the default) gives the BASE ONLY view - what the ID space
    looks like before this mod is layered on. Allocation and range checks must
    be computed against that.

    pristine=False gives the full view including the deployed mod, for asking
    "what will the game actually load".
    """

    def __init__(self, base: Path | None = None, extra_dirs=None,
                 pristine: bool = True):
        self.base = Path(base) if base else _config.base_dir()
        # Not every install is laid out like vanilla. Nevada Extended ships
        # Patch000/001.dat with no master.dat at all, and FOnline names its
        # archive fallout.dat. Require only that SOMETHING readable is here.
        if not self.base.is_dir():
            raise FileNotFoundError(f"no install at {self.base}")
        has_data = (
            (self.base / "master.dat").is_file()
            or any(self.base.glob("*.dat"))
            or (self.base / "data").is_dir()
            or (self.base / "mods").is_dir()
        )
        if not has_data:
            raise FileNotFoundError(
                f"nothing loadable at {self.base} - no .dat archives and no "
                f"data/ or mods/ directory"
            )
        self.pristine = pristine
        self.vfs = GameVFS(
            self.base,
            extra_dirs=extra_dirs,
            exclude=_OURS if pristine else None,
        )

    # -- scripts -----------------------------------------------------------

    @property
    def scripts(self) -> list[str]:
        """scripts.lst names, [0] == line 1. SCRIPT_* defines are 1-based."""
        if not hasattr(self, "_scripts"):
            self._scripts = parse_lst(self.vfs.read("scripts\\scripts.lst"))
        return self._scripts

    def script_name(self, one_based_line: int) -> str | None:
        i = one_based_line - 1
        if 0 <= i < len(self.scripts):
            return self.scripts[i] or None
        return None

    def script_line(self, name: str) -> int | None:
        """1-based scripts.lst line for a script filename (with or without .int)."""
        want = name.lower()
        if not want.endswith(".int"):
            want += ".int"
        for i, n in enumerate(self.scripts):
            if n.lower() == want:
                return i + 1
        return None

    # -- protos ------------------------------------------------------------

    def proto_lst(self, obj_type: int) -> list[str]:
        if not hasattr(self, "_proto_lsts"):
            self._proto_lsts = {}
        if obj_type not in self._proto_lsts:
            name = TYPE_NAME.get(obj_type)
            path = f"proto\\{name}\\{name}.lst"
            try:
                self._proto_lsts[obj_type] = parse_lst(self.vfs.read(path))
            except FileNotFoundError:
                self._proto_lsts[obj_type] = []
        return self._proto_lsts[obj_type]

    def proto_filename(self, pid: int) -> str | None:
        """proto.cc::_proto_list_str — index is the 1-based line in the type .lst."""
        lst = self.proto_lst(pid_type(pid))
        i = pid_index(pid) - 1
        if 0 <= i < len(lst) and lst[i]:
            return lst[i]
        return None

    def proto_path(self, pid: int) -> str | None:
        fn = self.proto_filename(pid)
        if fn is None:
            return None
        return f"proto\\{TYPE_NAME[pid_type(pid)]}\\{fn}"

    def proto(self, pid: int) -> Proto | None:
        if not hasattr(self, "_protos"):
            self._protos = {}
        if pid in self._protos:
            return self._protos[pid]
        path = self.proto_path(pid)
        result = None
        if path is not None:
            try:
                result = parse_pro(self.vfs.read(path))
            except (FileNotFoundError, ValueError):
                result = None
        self._protos[pid] = result
        return result

    # -- globals -----------------------------------------------------------

    @property
    def gvar_slots(self) -> list[str | None]:
        if not hasattr(self, "_gvars"):
            self._gvars = parse_gam_slots(self.vfs.read("data\\vault13.gam"))
        return self._gvars

    # -- maps --------------------------------------------------------------

    def load_map(self, data: bytes) -> MapFile:
        return MapFile.parse(data, self.proto)
