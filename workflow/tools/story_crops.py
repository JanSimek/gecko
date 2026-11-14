#!/usr/bin/env python3
"""
story_crops.py - render close-ups of the vignettes tools/story_survey.py
found, so the scenes can be studied by eye.

    python tools/story_crops.py --must remains --classes 3 --per-source 3
        -> build/story_survey/crops_<tag>.png (a contact sheet)

Picks vignettes with at least --classes story classes (and every --must
class), at most --per-source from each game and one per map, renders each
map elevation in full and crops a box around the scene.
"""

from __future__ import annotations

import argparse
import csv
import random
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))

import port  # noqa: E402
import render  # noqa: E402
from foclib import MapFile  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
SURV = ROOT / "build" / "story_survey"


def crop_scene(src, mapname, elev, x, y, box=420):
    m = MapFile.parse(src.vfs.read(f"maps\\{mapname}"), src.proto)
    ei = m.elevations.index(elev)
    img = render.render_map(src, m, ei)
    pts = []
    for sq, tid in enumerate(m.floors[ei]):
        if tid > 1:
            pts.append(render.square_xy(sq))
    for o in m.objects:
        if o.elevation == elev and 0 <= o.tile < 40000:
            px, py = render.hex_xy_screen(o.tile)
            pts.append((px - 100, py - 200))
            pts.append((px + 100, py + 60))
    minx = min(p[0] for p in pts)
    miny = min(p[1] for p in pts)
    cx, cy = render.square_xy(y * 100 + x)
    cx, cy = cx - minx + 32, cy - miny + 12
    return img.crop((int(cx - box), int(cy - box * 0.7), int(cx + box), int(cy + box * 0.7))).convert("RGB")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--must", default="")
    ap.add_argument("--classes", type=int, default=3)
    ap.add_argument("--per-source", type=int, default=2)
    ap.add_argument("--sources", default="fo1,fo2,sonora,nevada,resurrection,sotj,yesterday")
    ap.add_argument("--tag", default="")
    ap.add_argument("--seed", type=int, default=7)
    args = ap.parse_args()
    must = [m for m in args.must.split(",") if m]
    rows = list(csv.DictReader(open(SURV / "vignettes.csv", encoding="utf-8")))
    rng = random.Random(args.seed)
    rng.shuffle(rows)
    pick, used_maps, per = [], set(), {}
    for r in rows:
        kinds = r["combo"].split("+")
        if r["source"] not in args.sources.split(","):
            continue
        if len([k for k in kinds if k != "person"]) < args.classes or any(m not in kinds for m in must):
            continue
        key = (r["source"], r["map"].lower())
        if key in used_maps or per.get(r["source"], 0) >= args.per_source:
            continue
        used_maps.add(key)
        per[r["source"]] = per.get(r["source"], 0) + 1
        pick.append(r)
    tiles = []
    for r in pick:
        try:
            src = port.open_source(r["source"])
            im = crop_scene(src, r["map"], int(r["elev"]), int(r["x"]), int(r["y"]))
        except Exception as e:
            print("skip", r["source"], r["map"], e)
            continue
        im.thumbnail((600, 420))
        d = ImageDraw.Draw(im)
        d.rectangle((0, 0, 600, 17), fill=(0, 0, 0))
        d.text((4, 3), f"{r['source']} {r['map']} ({r['x']},{r['y']}) {r['combo']} [{r['where']}]", fill=(255, 255, 0))
        tiles.append(im)
        print(r["source"], r["map"], r["combo"], "|", r["names"][:150])
    cols = 3
    W = 600 * cols
    H = 420 * ((len(tiles) + cols - 1) // cols)
    sheet = Image.new("RGB", (W, max(H, 420)), (20, 20, 20))
    for i, im in enumerate(tiles):
        sheet.paste(im, ((i % cols) * 600, (i // cols) * 420))
    tag = args.tag or ("_".join(must) or "any") + f"_{args.classes}"
    out = SURV / f"crops_{tag}.png"
    sheet.save(out)
    print(out.relative_to(ROOT))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
