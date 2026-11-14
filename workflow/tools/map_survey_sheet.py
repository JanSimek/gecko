#!/usr/bin/env python3
"""
map_survey_sheet.py - render the most typical town of each source (the one
closest to its source's medians in build/map_survey/summary.json) so the
numbers can be checked against real layouts.

    python tools/map_survey_sheet.py            -> build/map_survey/typical_<source>.png
"""

from __future__ import annotations

import csv
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import port  # noqa: E402
import render  # noqa: E402
from foclib.formats import MapFile  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "map_survey"
KEYS = ["play_w", "play_h", "buildings", "bld_median_sq", "bld_gap_median",
        "road_share", "near_building_density", "open_ground_density", "empty_share"]


def f(v):
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def main() -> int:
    summary = json.loads((OUT / "summary.json").read_text(encoding="utf-8"))
    rows = list(csv.DictReader((OUT / "maps.csv").open(encoding="utf-8")))
    for src, kinds in summary.items():
        if src == "ALL" or "town" not in kinds:
            continue
        medians = kinds["town"]
        cands = [r for r in rows if r["source"] == src and r["kind"] == "town" and r["elev"] == "0"]
        best, bd = None, None
        for r in cands:
            d = 0.0
            for k in KEYS:
                a, b = f(r.get(k)), f(medians.get(k))
                if a is None or b is None:
                    continue
                d += abs(a - b) / (abs(b) + 1e-6 if abs(b) > 0.05 else 1)
            if bd is None or d < bd:
                best, bd = r, d
        if not best:
            continue
        gi = port.open_source(src)
        m = MapFile.parse(gi.vfs.read("maps\\" + best["map"]), gi.proto)
        img = render.render_map(gi, m, 0, True)
        img.thumbnail((1800, 1000))
        out = OUT / f"typical_{src}.png"
        img.save(out)
        print(f"{src}: {best['map']} -> {out.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
