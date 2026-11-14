#!/usr/bin/env python3
"""
plan_refs.py - the reference sheet that goes beside a site plan: the real
place (tools/real_site.py) and the comparable maps from every original game
and total conversion, rendered, with their measured layout numbers from
build/map_survey/maps.csv (tools/map_survey.py) beside the plan's.

    python tools/plan_refs.py webb --real build/realsite/webbcant.png \\
        --maps olympus:SJBARAKS:0 olympus:SJMAIN2:0 fo2:NAVARRO:0 yesterday:FORTDESO:0
        -> docs/plans/webb_refs.png
"""

from __future__ import annotations

import argparse
import csv
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "docs" / "plans"
SURVEY = ROOT / "build" / "map_survey" / "maps.csv"
COLS = [("play_w", "play w"), ("play_h", "play h"), ("buildings", "bldgs"), ("bld_median_sq", "bldg size"),
        ("bld_gap_median", "gap"), ("road_share", "road"), ("near_building_density", "clutter@bldg"),
        ("open_ground_density", "clutter open")]


def font(n):
    try:
        return ImageFont.truetype("C:/Windows/Fonts/arial.ttf", n)
    except OSError:
        return ImageFont.load_default()


def survey_row(source: str, name: str, elev: int):
    if not SURVEY.exists():
        return None
    with SURVEY.open(encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if r["source"] == source and r["map"].lower() == f"{name.lower()}.map" and int(r["elev"]) == elev:
                return r
    return None


def render(source, name, elev):
    out = ROOT / "build" / "siteplan" / f"ref_{source}_{name}_{elev}.png"
    if not out.exists():
        subprocess.run([sys.executable, str(ROOT / "tools" / "preview_source.py"), source, name, str(elev),
                        "-o", str(out), "--scale", "0.3"], check=True, capture_output=True)
    im = Image.open(out).convert("RGB")
    return im.crop(im.getbbox())


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("name")
    ap.add_argument("--real", nargs="*", default=[])
    ap.add_argument("--real-caption", nargs="*", default=[])
    ap.add_argument("--maps", nargs="*", default=[])
    args = ap.parse_args()
    W = 1900
    tiles = []
    for i, r in enumerate(args.real):
        im = Image.open(ROOT / r).convert("RGB")
        im.thumbnail((W // 2 - 20, 820))
        cap = args.real_caption[i] if i < len(args.real_caption) else Path(r).stem
        tiles.append(("REAL: " + cap, im))
    rows = []
    for spec in args.maps:
        src, name, elev = spec.split(":")
        im = render(src, name, int(elev))
        im.thumbnail((W // 3 - 20, 380))
        tiles.append((f"{src} {name} elev {elev}", im))
        rows.append((f"{src} {name}", survey_row(src, name, int(elev))))
    # layout: real sites two to a row, maps three to a row
    f, fs = font(16), font(13)
    y = 50
    placed = []
    x = 10
    rowh = 0
    for cap, im in tiles:
        if x + im.size[0] > W:
            x, y, rowh = 10, y + rowh + 30, 0
        placed.append((x, y, cap, im))
        x += im.size[0] + 15
        rowh = max(rowh, im.size[1])
    y += rowh + 40
    table_y = y
    H = table_y + 30 + 22 * (len(rows) + 2)
    sheet = Image.new("RGB", (W, H), (236, 230, 214))
    dr = ImageDraw.Draw(sheet)
    dr.text((10, 12), f"{args.name}: references", fill=(20, 10, 0), font=font(24))
    for x, y, cap, im in placed:
        sheet.paste(im, (x, y))
        dr.text((x, y - 20), cap, fill=(20, 10, 0), font=f)
    dr.text((10, table_y), "Measured layout (tools/map_survey.py, squares; clutter = objects per square)", fill=(20, 10, 0), font=f)
    ty = table_y + 26
    dr.text((10, ty), "map", fill=(20, 10, 0), font=fs)
    for k, (_, lab) in enumerate(COLS):
        dr.text((330 + k * 140, ty), lab, fill=(20, 10, 0), font=fs)
    for name, r in rows:
        ty += 22
        dr.text((10, ty), name, fill=(40, 30, 20), font=fs)
        for k, (col, _) in enumerate(COLS):
            v = r.get(col, "") if r else ""
            try:
                v = f"{float(v):.2f}".rstrip("0").rstrip(".") if v not in ("", None) else "-"
            except ValueError:
                pass
            dr.text((330 + k * 140, ty), str(v), fill=(40, 30, 20), font=fs)
    out = OUT / f"{args.name}_refs.png"
    OUT.mkdir(parents=True, exist_ok=True)
    sheet.save(out)
    print(out.relative_to(ROOT))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
