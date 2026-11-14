#!/usr/bin/env python3
"""
groundkit.py - learn how Fallout 2 lays its ground tiles, and grow new ground
for FOC maps that follows the same rules: broken asphalt with sand in its
edges, kerbs and sidewalk fragments, dirt worn round buildings, the way the
Den, Klamath and New Reno look.

    python tools/groundkit.py learn       # -> build/groundkit/ground.json
    python tools/groundkit.py demo        # -> build/groundkit/demo.png

Only Fallout 2's own tiles are used, so every tile id is already FOC's (FOC
runs on Fallout 2's art) and nothing is copied: a tile is chosen square by
square so that it matches the tile to its left and the tile above it, using
only pairs that Fallout 2's maps actually put side by side.

Each tile has a class from its name:
  road     road*, asph*            the asphalt
  walk     curb*, sdwk*, swalk*    kerbs and sidewalks
  edge     edg*                    the ragged edge between asphalt and dirt
  dirt     everything else outdoors (dirt, sand, rock, grass)
grow(target) takes a target class per square (from a site plan: roads,
yards, decay noise) and fills it, preferring the target class but always
keeping the neighbour rules, which is what makes the edges ragged.
"""

from __future__ import annotations

import argparse
import json
import os
import random
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "groundkit"

CLASS_RES = [("road", re.compile(r"^(road|asph|strt|street)", re.I)),
             ("walk", re.compile(r"^(curb|crb|sdwk|swalk|side)", re.I)),
             ("edge", re.compile(r"^(fade|edgs)", re.I)),
             ("dirt", re.compile(r"^(edg|dearth|rndcrn)", re.I))]
SKIP = re.compile(r"^(bld2043|cav|mine|vlt|vault|flr|rf|roof|tp|brick|wood|wd|carpt|met|grat|grid|black|blk|reserved)", re.I)


def tclass(name: str) -> str:
    for k, rx in CLASS_RES:
        if rx.match(name):
            return k
    return "other"


STYLES = {
    "ruin_town": ["denbus1", "denbus2", "denres1", "kladwtwn", "klamall", "newr1", "newr2", "newr3", "newr4",
                  "newrvb", "reddown", "redment", "broken1", "broken2"],
    "desert_town": ["modmain", "modbrah", "modgard", "gecksetl", "arvillag", "arvill2", "vill", "raiders1"],
    "desert": ["desert1", "desert2", "desert3", "desert4", "desert5", "desert6", "desert7", "desrt10", "desrt11"],
}


def learn():
    for style, maps in STYLES.items():
        learn_style(style, maps)


def learn_style(style, maps):
    import port
    from foclib import MapFile, parse_lst
    src = port.open_source("fo2")
    tl = [x.split(",")[0] for x in parse_lst(src.vfs.read("art\\tiles\\tiles.lst"))]
    right, down = defaultdict(Counter), defaultdict(Counter)
    freq = Counter()
    names = set()
    for prov in src.vfs.providers:
        if prov.kind == "dat":
            names |= {n.split("\\")[-1].lower() for n in prov.dat.names()
                      if n.lower().startswith("maps\\") and n.lower().endswith(".map")}
        else:
            d = prov.root / "maps"
            if d.is_dir():
                names |= {f.lower() for f in os.listdir(d) if f.lower().endswith(".map")}
    names = {n for n in names if n[:-4] in maps}
    for name in sorted(names):
        try:
            m = MapFile.parse(src.vfs.read(f"maps\\{name}"), src.proto)
        except Exception:
            continue
        for ei in range(len(m.elevations)):
            fl = [v & 0xFFF for v in m.floors[ei]]
            rf = [v & 0xFFF for v in m.roofs[ei]]
            outdoor = [v > 1 and v < len(tl) and not SKIP.match(tl[v]) and rf[k] <= 1 for k, v in enumerate(fl)]
            if sum(outdoor) < 1500:          # outdoor levels only
                continue
            for k in range(10000):
                if not outdoor[k]:
                    continue
                a = fl[k]
                freq[a] += 1
                if k % 100 < 99 and outdoor[k + 1]:
                    right[a][fl[k + 1]] += 1
                if k < 9900 and outdoor[k + 100]:
                    down[a][fl[k + 100]] += 1
    freq = Counter({t: n for t, n in freq.items() if n >= 6})
    data = {"names": {str(t): tl[t] for t in freq},
            "classes": {str(t): tclass(tl[t]) for t in freq},
            "freq": {str(t): n for t, n in freq.items()},
            "right": {str(a): {str(b): n for b, n in c.items() if n >= 2 and b in freq} for a, c in right.items() if a in freq},
            "down": {str(a): {str(b): n for b, n in c.items() if n >= 2 and b in freq} for a, c in down.items() if a in freq}}
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / f"ground_{style}.json").write_text(json.dumps(data), encoding="utf-8")
    cls = Counter(data["classes"].values())
    print(f"{style}: learned {len(freq)} tiles {dict(cls)} from {len(names)} maps")


_G = {}


def g(style="ruin_town"):
    if style not in _G:
        d = json.loads((OUT / f"ground_{style}.json").read_text(encoding="utf-8"))
        d["right"] = {int(a): {int(b): n for b, n in c.items()} for a, c in d["right"].items()}
        d["down"] = {int(a): {int(b): n for b, n in c.items()} for a, c in d["down"].items()}
        d["classes"] = {int(a): c for a, c in d["classes"].items()}
        d["freq"] = {int(a): n for a, n in d["freq"].items()}
        d["names"] = {int(a): n for a, n in d["names"].items()}
        _G[style] = d
    return _G[style]


def grow(target: dict, rng: random.Random, palette: dict | None = None, rect=(0, 0, 99, 99), style="ruin_town"):
    """target: {(sx, sy): class}; returns {(sx, sy): tile}. palette:
    {class: [tile ids]} limits which tiles a class may use (keeps a map to
    one look), else every tile of that class is allowed."""
    G = g(style)
    allowed = {}
    for t, c in G["classes"].items():
        allowed.setdefault(c, []).append(t)
    if palette:
        for c, ts in palette.items():
            allowed[c] = [t for t in ts if t in G["classes"]] or allowed.get(c, [])
    out = {}
    x0, y0, x1, y1 = rect
    for sy in range(y0, y1 + 1):
        for sx in range(x0, x1 + 1):
            want = target.get((sx, sy))
            if want is None:
                continue
            left = out.get((sx - 1, sy))
            up = out.get((sx, sy - 1))
            cand = Counter()
            if left is not None and up is not None:
                r = G["right"].get(left, {})
                d = G["down"].get(up, {})
                for t in set(r) & set(d):
                    cand[t] = r[t] * d[t]
            elif left is not None:
                cand.update(G["right"].get(left, {}))
            elif up is not None:
                cand.update(G["down"].get(up, {}))
            # prefer the wanted class, then tiles the palette allows
            pool = {t: n for t, n in cand.items() if G["classes"].get(t) == want and t in set(allowed.get(want, []))}
            if not pool:
                pool = {t: n for t, n in cand.items() if G["classes"].get(t) == want}
            if not pool:
                pool = {t: G["freq"][t] for t in allowed.get(want, [])}
            ts = list(pool)
            w = [pool[t] ** 0.6 for t in ts]
            out[(sx, sy)] = rng.choices(ts, weights=w)[0]
    return out


def edge_ring(target: dict) -> dict:
    """Dirt squares touching asphalt become 'edge' (Fallout 2's edg* tiles,
    the ragged asphalt-to-sand transition)."""
    out = dict(target)
    for (x, y), c in target.items():
        if c != "dirt":
            continue
        if any(target.get((x + dx, y + dy)) in ("road", "walk") for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
            out[(x, y)] = "edge"
    return out


def decay_target(base: dict, rng: random.Random, crumble=0.18, drift=0.12):
    """Turn a clean plan target into a decayed one: asphalt squares near
    dirt crumble to dirt, and sand drifts onto asphalt in patches, so the
    road survives as islands with ragged edges."""
    out = dict(base)
    for (x, y), c in base.items():
        if c != "road":
            continue
        n_dirt = sum(1 for d in ((1, 0), (-1, 0), (0, 1), (0, -1)) if base.get((x + d[0], y + d[1])) == "dirt")
        if n_dirt and rng.random() < crumble * n_dirt:
            out[(x, y)] = "dirt"
    for _ in range(int(len(base) * drift / 12)):
        x, y = rng.choice(list(base))
        r = rng.randint(1, 3)
        for dx in range(-r, r + 1):
            for dy in range(-r, r + 1):
                if out.get((x + dx, y + dy)) == "road" and rng.random() < 0.7:
                    out[(x + dx, y + dy)] = "dirt"
    return out


def demo():
    rng = random.Random(4)
    target = {}
    for y in range(20, 70):
        for x in range(20, 80):
            target[(x, y)] = "road" if 40 <= y <= 46 or 46 <= x <= 52 else "dirt"
    target = decay_target(target, rng)
    tiles = grow(target, rng, rect=(20, 20, 79, 69))
    import render
    from foclib import GameIndex
    gi = GameIndex(pristine=False)
    pal = render.load_palette(gi)
    rr = render.ArtCache(gi, pal) if hasattr(render, "ArtCache") else None
    from PIL import Image
    im = Image.new("RGB", (5200, 3000), (0, 0, 0))
    ox, oy = 0, 1500
    cache = rr
    for (x, y), t in tiles.items():
        px, py = render.square_xy(y * 100 + x)
        ti = cache.tile(t) if cache else None
        if ti is not None:
            im.paste(ti, (px + ox - 2000, py + oy), ti)
    im = im.crop(im.getbbox())
    im.thumbnail((1800, 1200))
    im.save(OUT / "demo.png")
    print(OUT / "demo.png")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("cmd", choices=["learn", "demo"])
    args = ap.parse_args()
    learn() if args.cmd == "learn" else demo()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


# ------------------------------------------------------------ designed decay
ASPHALT = ["road02.frm", "road03.frm", "road04.frm", "road05.frm", "road08.frm", "road16.frm"]
HOLED = ["road%02d.frm" % i for i in range(10, 22)]
CRUMB = ["road%02d.frm" % i for i in range(22, 34)]
LINE = ["road06.frm", "road07.frm"]
WALK = ["curb16.frm"]


class Noise:
    """Smooth value noise in squares: coherent patches, not speckle."""

    def __init__(self, rng, cell=5):
        self.cell, self.rng, self.g = cell, rng, {}

    def _v(self, i, j):
        if (i, j) not in self.g:
            self.g[(i, j)] = self.rng.random()
        return self.g[(i, j)]

    def __call__(self, x, y):
        c = self.cell
        i, j = x // c, y // c
        fx, fy = (x % c) / c, (y % c) / c
        fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
        a = self._v(i, j) * (1 - fx) + self._v(i + 1, j) * fx
        b = self._v(i, j + 1) * (1 - fx) + self._v(i + 1, j + 1) * fx
        return a * (1 - fy) + b * fy


def paint(target: dict, rng: random.Random, wear=0.35, style="ruin_town", lines=()):
    """Paint a decayed ground: target {(sx, sy): 'road'|'walk'|'dirt'}.
    wear 0..1: how far gone the pavement is. lines: squares that carry a
    painted centre line. Returns {(sx, sy): tile name}."""
    n1, n2 = Noise(rng, 6), Noise(rng, 2)
    out = {}
    paved = {k for k, c in target.items() if c in ("road", "walk")}

    def edge_dist(x, y):
        for r in range(1, 4):
            if any((x + dx, y + dy) not in paved for dx in range(-r, r + 1) for dy in range(-r, r + 1)):
                return r
        return 4
    # Fallout 2's plain desert floor, weighted as its maps use it
    G = g("desert")
    keep = re.compile(r"^edg(5\d\d\d|2003|3000|400[0-3])\.frm$", re.I)
    dirt = [(G["names"][t], n) for t, n in G["freq"].items() if keep.match(G["names"][t])]
    dnames = [d for d, _ in dirt]
    dw = [n for _, n in dirt]
    for (x, y), c in target.items():
        if c == "dirt":
            # asphalt crumbs spill a little way into the sand
            near = any((x + dx, y + dy) in paved for dx in (-1, 0, 1) for dy in (-1, 0, 1))
            if near and rng.random() < 0.25 * wear + 0.1:
                out[(x, y)] = rng.choice(CRUMB)
            else:
                out[(x, y)] = rng.choices(dnames, weights=dw)[0]
            continue
        d = 0.75 * n1(x, y) + 0.15 * n2(x, y) + 0.1 * rng.random()
        d += wear - 0.35 + (0.18 if edge_dist(x, y) == 1 else 0.06 if edge_dist(x, y) == 2 else 0)
        if d > 0.98:
            out[(x, y)] = rng.choices(dnames, weights=dw)[0]
        elif d > 0.80:
            out[(x, y)] = rng.choice(CRUMB)
        elif d > 0.64:
            out[(x, y)] = rng.choice(HOLED)
        elif c == "walk":
            out[(x, y)] = rng.choice(WALK)
        elif (x, y) in lines and d < 0.55:
            out[(x, y)] = LINE[0]
        else:
            out[(x, y)] = rng.choice(ASPHALT)
    return out
