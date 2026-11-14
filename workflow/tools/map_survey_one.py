#!/usr/bin/env python3
"""
map_survey_one.py - measure one FOC map the way map_survey.py measures the
originals, next to the all-source town medians.

    python tools/map_survey_one.py mod/data/maps/CLCOURT.MAP [more maps...]
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import map_survey as ms  # noqa: E402
from foclib.paths import inside  # noqa: E402
from foclib import GameIndex, parse_lst  # noqa: E402
from foclib.formats import MapFile, pid_type  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
KEYS = ["floor_w", "floor_h", "play_w", "play_h", "content_w", "content_h", "buildings",
        "bld_median_sq", "bld_gap_median", "road_share", "road_sides", "exits", "exit_sides",
        "obj_per_sq", "near_building_density", "open_ground_density", "sc_plants", "sc_junk",
        "sc_lamps", "sc_fences", "empty_share", "ground_tile_kinds", "start_to_exit"]


def main() -> int:
    gi = GameIndex(pristine=False)
    art = {t: parse_lst(gi.vfs.read(f"art\\{d}\\{d}.lst"))
           for t, d in ((0, "items"), (1, "critters"), (2, "scenery"), (3, "walls"), (5, "misc"))}
    tiles = parse_lst(gi.vfs.read("art\\tiles\\tiles.lst"))
    summary = json.loads((ROOT / "build" / "map_survey" / "summary.json").read_text(encoding="utf-8"))
    town = summary["ALL"]["town"]
    cols = []
    for p in sys.argv[1:]:
        m = MapFile.parse(inside(p).read_bytes(), gi.proto)  # NOSONAR - path checked by foclib.paths.inside()
        r = ms.survey_elev(m, 0, art, tiles) or {}
        scroll = sum(1 for o in m.objects if o.pid == 0x500000C)
        r["scroll_blockers"] = scroll
        cols.append((Path(p).name, r))
    print("measure".ljust(22), "towns".rjust(8), *(n[:12].rjust(12) for n, _ in cols))
    for k in KEYS + ["scroll_blockers"]:
        print(k.ljust(22), str(town.get(k, "")).rjust(8), *(str(r.get(k)).rjust(12) for _, r in cols))
    return 0


if __name__ == "__main__":
    sys.exit(main())
