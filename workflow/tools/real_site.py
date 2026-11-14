#!/usr/bin/env python3
"""
real_site.py - fetch a real place's layout from OpenStreetMap (Overpass API)
and draw it to scale, north up, as the reference a FOC site plan is adapted
from. Results are cached in build/realsite/<name>.json.

    python tools/real_site.py webb 32.195 -101.545 32.235 -101.500
    python tools/real_site.py midland 31.990 -102.085 32.005 -102.065 --streets

Draws build/realsite/<name>.png: runways, taxiways and aprons grey, hangars
dark, buildings brown, roads and streets as lines, with a scale bar. Map data
(c) OpenStreetMap contributors, ODbL.
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / "build" / "realsite"
URL = "https://overpass-api.de/api/interpreter"


def fetch(name: str, bbox, streets: bool, extra: list[str]) -> dict:
    p = CACHE / f"{name}.json"
    if p.exists():
        return json.loads(p.read_text(encoding="utf-8"))
    s, w, n, e = bbox
    b = f"({s},{w},{n},{e})"
    parts = [f'way["aeroway"]{b};', f'way["building"]{b};', f'way["landuse"]{b};', f'way["man_made"]{b};',
             f'way["amenity"]{b};', f'way["leisure"]{b};', f'node["man_made"]{b};']
    if streets:
        parts.append(f'way["highway"]{b};')
    parts += [x.replace("BBOX", b) for x in extra]
    q = "[out:json][timeout:90];(" + "".join(parts) + ");out geom;"
    CACHE.mkdir(parents=True, exist_ok=True)
    import re
    import time
    for attempt in range(4):
        subprocess.run(["curl", "-s", "-m", "120", "-A", "FOC-mod-site-planner/1.0", "-H", "Accept: application/json",
                        "-o", str(p), "--data-urlencode", f"data={q}", URL], check=True)
        text = p.read_text(encoding="utf-8", errors="replace")
        if text.lstrip().startswith("{"):
            return json.loads(text)
        msg = " ".join(re.sub(r"<[^>]+>", " ", text).split())[-300:]
        print(f"Overpass refused (attempt {attempt + 1}): {msg}")
        p.unlink()
        time.sleep(20 * (attempt + 1))
    raise SystemExit("Overpass gave no data")


def font(n):
    for f in ("C:/Windows/Fonts/arial.ttf",):
        try:
            return ImageFont.truetype(f, n)
        except OSError:
            pass
    return ImageFont.load_default()


def draw(name: str, d: dict, bbox, px_per_m: float):
    s, w, n, e = bbox
    lat0 = (s + n) / 2
    mx = 111320 * math.cos(math.radians(lat0))
    my = 110540
    W = int((e - w) * mx * px_per_m) + 80
    H = int((n - s) * my * px_per_m) + 120

    def P(lat, lon):
        return 40 + (lon - w) * mx * px_per_m, 60 + (n - lat) * my * px_per_m

    im = Image.new("RGB", (W, H), (236, 228, 208))
    dr = ImageDraw.Draw(im)
    f, fs = font(14), font(11)
    order = []
    for el in d.get("elements", []):
        if el["type"] != "way" or "geometry" not in el:
            continue
        t = el.get("tags", {})
        pts = [P(g["lat"], g["lon"]) for g in el["geometry"]]
        closed = len(pts) > 2 and pts[0] == pts[-1]
        if t.get("aeroway") in ("aerodrome",) or t.get("landuse"):
            order.append((0, el, pts, closed))
        elif t.get("aeroway") in ("apron", "runway", "taxiway"):
            order.append((1, el, pts, closed))
        elif t.get("highway"):
            order.append((2, el, pts, closed))
        else:
            order.append((3, el, pts, closed))
    for z, el, pts, closed in sorted(order, key=lambda o: o[0]):
        t = el.get("tags", {})
        a = t.get("aeroway")
        if z == 0:
            if closed:
                dr.polygon(pts, outline=(200, 190, 160))
            continue
        if a == "runway":
            wdt = max(3, int(45 * px_per_m)) if not closed else 1
            if closed:
                dr.polygon(pts, fill=(70, 70, 74))
            else:
                dr.line(pts, fill=(70, 70, 74), width=wdt)
        elif a == "taxiway":
            dr.line(pts, fill=(120, 120, 124), width=max(2, int(20 * px_per_m)))
        elif a == "apron":
            dr.polygon(pts, fill=(150, 150, 154)) if closed else dr.line(pts, fill=(150, 150, 154), width=3)
        elif t.get("highway"):
            dr.line(pts, fill=(250, 250, 250), width=max(2, int(12 * px_per_m)))
        elif t.get("building") or a in ("hangar", "terminal"):
            col = (60, 60, 70) if (a == "hangar" or t.get("building") == "hangar") else (150, 110, 70)
            if closed:
                dr.polygon(pts, fill=col, outline=(40, 30, 20))
        elif closed:
            dr.polygon(pts, outline=(120, 140, 120))
        label = t.get("name") or t.get("ref")
        if label and len(pts) > 1:
            cx = sum(p[0] for p in pts) / len(pts)
            cy = sum(p[1] for p in pts) / len(pts)
            dr.text((cx, cy), label, fill=(20, 20, 120), font=fs)
    # scale bar: 200 m
    L = 200 * px_per_m
    dr.line([(40, H - 40), (40 + L, H - 40)], fill=(0, 0, 0), width=4)
    dr.text((40, H - 34), "200 m", fill=(0, 0, 0), font=f)
    dr.text((40, 14), f"{name}: OpenStreetMap, north up", fill=(0, 0, 0), font=f)
    dr.text((W - 260, H - 34), "(c) OpenStreetMap contributors", fill=(60, 60, 60), font=fs)
    out = CACHE / f"{name}.png"
    im.save(out)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("name")
    ap.add_argument("s", type=float)
    ap.add_argument("w", type=float)
    ap.add_argument("n", type=float)
    ap.add_argument("e", type=float)
    ap.add_argument("--streets", action="store_true")
    ap.add_argument("--scale", type=float, default=0.5, help="pixels per metre")
    ap.add_argument("--refetch", action="store_true")
    args = ap.parse_args()
    bbox = (args.s, args.w, args.n, args.e)
    if args.refetch:
        (CACHE / f"{args.name}.json").unlink(missing_ok=True)
    d = fetch(args.name, bbox, args.streets, [])
    out = draw(args.name, d, bbox, args.scale)
    print(f"{len(d.get('elements', []))} elements -> {out.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
