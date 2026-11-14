#!/usr/bin/env python3
"""
tilewfc.py - lay floor tiles the way Fallout 2's designers did.

Fallout 2's ground is not scattered tiles. It is a grammar of pieces that fit
by their edges:
  * a street is a fixed cross-section - sidewalk (curb16), kerb (curb11 /
    curb08), edge lane (road09), asphalt (road02), centre line (road07,
    FADE*), edge lane (road03) - with its own set for the other axis
    (curb02 / road01 / road06 / road05 / curb13) and corner pieces where
    streets cross (EPAGH*, RNDCRN*);
  * a broken road edge is a run of edge pieces (road22, road23/24, road25
    over road33 ... road26) sitting on sand (edgs*);
  * a building floor is a framed set (tp164/tp157/tp161 top, tp160 left,
    tp158 right, tp163/tp159/tp162 bottom, fill inside; towr01-09; carpets).

learn() reads every Fallout 2 map and records which tile has been placed to
the right of (x+1) and below (y+1) which. solve() fills a region square by
square (wave function collapse: the most constrained square first) so that
EVERY pair of neighbours is a pair Fallout 2's own maps contain. A zone map
from the site plan says which family each square draws from (street, walk,
sand, a floor set), and a wear field biases the street toward its broken
pieces in coherent patches.

    python tools/tilewfc.py learn
    python tools/tilewfc.py demo        # -> build/tilewfc/demo.png
"""

from __future__ import annotations

import argparse
import heapq
import json
import math
import os
import random
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "tilewfc"

FAMILIES = [
    ("road", r"^(road\d+|road\d+b|fade\d+|epagh\d+|rndcrn\d+)\.frm$"),
    ("kerb", r"^(curb\d+|crb\w*)\.frm$"),
    ("sand", r"^(edg\d+|edgs\d+|dearth\d+)\.frm$"),
    ("tp", r"^(tp1[5-6]\d|carpt\d+)\.frm$"),
    ("towr", r"^towr\d+\.frm$"),
    ("carpet", r"^(carpet\d+|carbn\d+)\.frm$"),
    ("brick", r"^brick\d+\.frm$"),
    ("cmt", r"^cmt\d+\.frm$"),
    ("plank", r"^(brn\d+|plk\d+|wd\w+)\.frm$"),
    ("cave", r"^cav40(09|10|00|03|08)\.frm$"),
    ("vault", r"^vtg\w+\.frm$"),
    ("metal", r"^(plt\d+|grt\d+|stlflr\d*|rst\d+|mstrflr\d*|shipg\d+)\.frm$"),
]
FAM_RE = [(k, re.compile(rx, re.I)) for k, rx in FAMILIES]
BROKEN = re.compile(r"^(road(1\d|2\d|3[0-3])|road\d+b|edgs\d+)\.frm$", re.I)


def family(name: str) -> str | None:
    for k, rx in FAM_RE:
        if rx.match(name):
            return k
    return None


def learn():
    import port
    from foclib import MapFile, parse_lst
    src = port.open_source("fo2")
    tl = [x.split(",")[0].lower() for x in parse_lst(src.vfs.read("art\\tiles\\tiles.lst"))]
    names = set()
    for prov in src.vfs.providers:
        if prov.kind == "dat":
            names |= {n.split("\\")[-1] for n in prov.dat.names()
                      if n.lower().startswith("maps\\") and n.lower().endswith(".map")}
        else:
            d = prov.root / "maps"
            if d.is_dir():
                names |= {f for f in os.listdir(d) if f.lower().endswith(".map")}
    R, D, F = defaultdict(Counter), defaultdict(Counter), Counter()
    seen = set()
    for n in sorted(names, key=str.lower):
        if n.lower() in seen:
            continue
        seen.add(n.lower())
        try:
            m = MapFile.parse(src.vfs.read(f"maps\\{n}"), src.proto)
        except Exception:
            continue
        for ei in range(len(m.elevations)):
            fl = [v & 0xFFF for v in m.floors[ei]]
            for k in range(10000):
                a = fl[k]
                if a <= 1 or a >= len(tl):
                    continue
                F[a] += 1
                if k % 100 < 99 and 1 < fl[k + 1] < len(tl):
                    R[a][fl[k + 1]] += 1
                if k < 9900 and 1 < fl[k + 100] < len(tl):
                    D[a][fl[k + 100]] += 1
    keep = {t for t, c in F.items() if c >= 3 and family(tl[t])}
    data = {"names": {str(t): tl[t] for t in keep}, "freq": {str(t): F[t] for t in keep},
            "family": {str(t): family(tl[t]) for t in keep},
            "R": {str(a): {str(b): c for b, c in R[a].items() if b in keep} for a in keep},
            "D": {str(a): {str(b): c for b, c in D[a].items() if b in keep} for a in keep}}
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "grammar.json").write_text(json.dumps(data), encoding="utf-8")
    print(f"learned {len(keep)} tiles: {dict(Counter(data['family'].values()))}")


class Grammar:
    def __init__(self):
        d = json.loads((OUT / "grammar.json").read_text(encoding="utf-8"))
        self.tiles = sorted(int(t) for t in d["names"])
        self.idx = {t: i for i, t in enumerate(self.tiles)}
        self.name = {int(t): n for t, n in d["names"].items()}
        self.freq = {int(t): n for t, n in d["freq"].items()}
        self.fam = {int(t): f for t, f in d["family"].items()}
        n = len(self.tiles)
        self.Rm = [0] * n          # tiles allowed to the right (x+1)
        self.Lm = [0] * n          # allowed to the left
        self.Dm = [0] * n          # below (y+1)
        self.Um = [0] * n
        for a, nb in d["R"].items():
            ia = self.idx[int(a)]
            for b in nb:
                ib = self.idx[int(b)]
                self.Rm[ia] |= 1 << ib
                self.Lm[ib] |= 1 << ia
        for a, nb in d["D"].items():
            ia = self.idx[int(a)]
            for b in nb:
                ib = self.idx[int(b)]
                self.Dm[ia] |= 1 << ib
                self.Um[ib] |= 1 << ia
        self.fam_mask = defaultdict(int)
        for t in self.tiles:
            self.fam_mask[self.fam[t]] |= 1 << self.idx[t]
        self.rare_mask = 0
        for t in self.tiles:
            if re.match(r"^(epagh|rndcrn|road3[4-9]|curb1[7-9])", self.name[t], re.I):
                self.rare_mask |= 1 << self.idx[t]
        self.broken_mask = 0
        for t in self.tiles:
            if BROKEN.match(self.name[t]):
                self.broken_mask |= 1 << self.idx[t]

    def mask_of(self, families):
        m = 0
        for f in families:
            m |= self.fam_mask[f]
        return m


def bits(m):
    while m:
        low = m & -m
        yield low.bit_length() - 1
        m ^= low


def solve(zones: dict, G: Grammar, rng: random.Random, wear=None, fixed=None):
    """zones: {(x, y): mask}. wear: {(x, y): 0..1} raises the weight of broken
    pieces. fixed: {(x, y): tile id}. Returns {(x, y): tile id} and the
    number of squares that had to be relaxed (no fitting piece)."""
    dom = dict(zones)
    if fixed:
        for k, t in fixed.items():
            dom[k] = 1 << G.idx[t]
    wear = wear or {}
    relaxed = set()
    out = {}
    nbrs = {(1, 0): (G.Rm, G.Lm), (-1, 0): (G.Lm, G.Rm), (0, 1): (G.Dm, G.Um), (0, -1): (G.Um, G.Dm)}
    cache = {}

    def allowed_from(cell, d):
        m = dom[cell]
        key = (m, d)
        if key in cache:
            return cache[key]
        fwd = nbrs[d][0]
        a = 0
        for i in bits(m):
            a |= fwd[i]
        if len(cache) < 200000:
            cache[key] = a
        return a

    def propagate(start):
        stack = [start]
        while stack:
            c = stack.pop()
            if c in relaxed:
                continue
            for d in nbrs:
                n = (c[0] + d[0], c[1] + d[1])
                if n not in dom or n in out or n in relaxed:
                    continue
                a = allowed_from(c, d)
                nm = dom[n] & a
                if nm == dom[n]:
                    continue
                if nm == 0:
                    relaxed.add(n)          # nothing fits: leave it free
                    continue
                dom[n] = nm
                stack.append(n)

    for c in list(dom):
        if fixed and c in fixed:
            out[c] = fixed[c]
    for c in list(out):
        propagate(c)
    heap = [(bin(m).count("1") + rng.random(), c) for c, m in dom.items() if c not in out]
    heapq.heapify(heap)
    while heap:
        _, c = heapq.heappop(heap)
        if c in out:
            continue
        m = dom[c]
        cnt = bin(m).count("1")
        if c in relaxed:
            m = zones[c]
        choices = list(bits(m))
        if not choices:
            choices = list(bits(zones[c])) or [0]
        w = []
        wv = wear.get(c, 0.0)
        for i in choices:
            t = G.tiles[i]
            f = math.sqrt(G.freq[t])
            if (G.broken_mask >> i) & 1:
                f *= 0.02 + 4 * max(0.0, wv - 0.4) ** 2
            if (G.rare_mask >> i) & 1:
                f *= 0.04
            w.append(f)
        i = rng.choices(choices, weights=w)[0]
        out[c] = G.tiles[i]
        dom[c] = 1 << i
        if c not in relaxed:
            propagate(c)
        # neighbours' entropy changed
        for d in nbrs:
            n = (c[0] + d[0], c[1] + d[1])
            if n in dom and n not in out:
                heapq.heappush(heap, (bin(dom[n]).count("1") + rng.random(), n))
    return out, len(relaxed)


def demo():
    import groundkit
    G = Grammar()
    rng = random.Random(3)
    L = Layout(G, (24, 24, 76, 76), groundkit.Noise(rng, 7), wear=0.35)
    L.sidewalk((24, 44, 76, 46)); L.sidewalk((24, 52, 76, 54))
    L.street_x(47, 24, 76)
    L.sidewalk((44, 24, 46, 76)); L.sidewalk((54, 24, 56, 76))
    L.street_y(53, 24, 76)
    L.crossing((47, 47, 53, 51))
    L.floor((28, 28, 40, 40), "towr")
    tiles, nrel = L.solve(rng)
    print("relaxed", nrel)
    render_tiles(tiles, OUT / "demo.png")


def render_tiles(tiles, out):
    import render
    from foclib import GameIndex
    from PIL import Image
    gi = GameIndex(pristine=False)
    ac = render.ArtCache(gi, render.load_palette(gi))
    pts = [render.square_xy(y * 100 + x) for (x, y) in tiles]
    minx, miny = min(p[0] for p in pts), min(p[1] for p in pts)
    maxx, maxy = max(p[0] for p in pts), max(p[1] for p in pts)
    im = Image.new("RGB", (maxx - minx + 100, maxy - miny + 60))
    for (x, y), t in sorted(tiles.items(), key=lambda kv: (kv[0][1], -kv[0][0])):
        ti = ac.tile(t)
        if ti is None:
            continue
        px, py = render.square_xy(y * 100 + x)
        im.paste(ti, (px - minx, py - miny), ti)
    im.save(out)
    print(out)


# ------------------------------------------------------------------ layouts
def names_mask(G, pats):
    rx = [re.compile(p, re.I) for p in pats]
    m = 0
    for t in G.tiles:
        if any(r.match(G.name[t]) for r in rx):
            m |= 1 << G.idx[t]
    return m


class Layout:
    """Zones for solve(), built from a site plan: streets with Fallout 2's
    real cross-sections, sidewalks, sand, building floors."""

    def __init__(self, G: Grammar, rect, wear_noise=None, wear=0.3, base="sand"):
        self.G = G
        self.zones, self.wear = {}, {}
        x0, y0, x1, y1 = rect
        self.rect = rect
        self.sand = names_mask(G, [r"^edg\d+\.frm$", r"^edgs\d+\.frm$"])
        self.kerb = G.mask_of(["kerb"])
        self.broken = names_mask(G, [r"^road(1\d|2\d|3[0-3])\.frm$", r"^road\d+b\.frm$"])
        self.wn = wear_noise
        self.w = wear
        if base:
            for y in range(y0, y1 + 1):
                for x in range(x0, x1 + 1):
                    self.zones[(x, y)] = self.sand
                    self.wear[(x, y)] = 0.0

    def _wear(self, x, y):
        return min(1.0, max(0.0, (self.wn(x, y) if self.wn else 0.5) - 0.5 + self.w))

    def _set(self, x, y, mask, allow_sand=True):
        if (x, y) not in self.zones:
            return
        w = self._wear(x, y)
        m = mask
        if w > 0.45:
            m |= self.broken
        if allow_sand and w > 0.62:
            m |= self.sand
        self.zones[(x, y)] = m
        self.wear[(x, y)] = w

    def sidewalk(self, rect):
        x0, y0, x1, y1 = rect
        plain = names_mask(self.G, [r"^curb1[6-9]\.frm$"])
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                w = self._wear(x, y)
                self._set(x, y, plain | (self.kerb if w > 0.62 else 0), True)

    def street_y(self, x_hi, y0, y1):
        """A street running along y, 7 squares wide, x_hi down to x_hi-6:
        curb11 | road09 | road02 | centre | road02 | road03 | curb08."""
        G = self.G
        roles = [[r"^curb1[1-2]\.frm$", r"^curb4[0-4]\.frm$"], [r"^road09\.frm$"], [r"^road0[2-5]\.frm$"],
                 [r"^road07\.frm$", r"^fade00[13]\.frm$", r"^road02\.frm$"], [r"^road0[2-5]\.frm$"],
                 [r"^road03\.frm$"], [r"^curb0[89]\.frm$", r"^curb44\.frm$"]]
        for i, pats in enumerate(roles):
            m = names_mask(G, pats)
            for y in range(y0, y1 + 1):
                self._set(x_hi - i, y, m, i not in (0, 6))

    def street_x(self, y_lo, x0, x1):
        """A street running along x, 5 squares wide, y_lo to y_lo+4:
        curb02 | road01 | centre | road05 | curb13."""
        G = self.G
        roles = [[r"^curb0[2-7]\.frm$"], [r"^road01\.frm$"], [r"^road06\.frm$", r"^fade00[24]\.frm$"],
                 [r"^road05\.frm$"], [r"^curb13\.frm$", r"^curb48\.frm$"]]
        for i, pats in enumerate(roles):
            m = names_mask(G, pats)
            for x in range(x0, x1 + 1):
                self._set(x, y_lo + i, m, i not in (0, 4))

    def crossing(self, rect):
        """Where streets meet: asphalt and Fallout 2's corner pieces."""
        x0, y0, x1, y1 = rect
        m = names_mask(self.G, [r"^road0[2-58]\.frm$", r"^epagh\d+\.frm$", r"^rndcrn\d+\.frm$"])
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self._set(x, y, m, False)

    def runway_x(self, y_lo, x0, x1, width=7):
        """A runway or wide road along x with no kerbs: edge lane, asphalt,
        a centre line, asphalt, edge lane."""
        G = self.G
        mid = width // 2
        for i in range(width):
            if i == 0:
                pats = [r"^road01\.frm$"]
            elif i == width - 1:
                pats = [r"^road05\.frm$"]
            elif i == mid:
                pats = [r"^road06\.frm$", r"^fade00[24]\.frm$"]
            else:
                pats = [r"^road0[2-4]\.frm$", r"^road08\.frm$", r"^road16\.frm$"]
            m = names_mask(G, pats)
            for x in range(x0, x1 + 1):
                self._set(x, y_lo + i, m, True)

    def road_y(self, x_hi, y0, y1, width=3):
        """A plain road along y with no kerbs: road09 | asphalt | road03."""
        G = self.G
        for i in range(width):
            pats = [r"^road09\.frm$"] if i == 0 else [r"^road03\.frm$"] if i == width - 1 else [r"^road0[2-4]\.frm$"]
            m = names_mask(G, pats)
            for y in range(y0, y1 + 1):
                self._set(x_hi - i, y, m, True)

    def apron(self, rect):
        """A paved yard (parade ground, apron): plain asphalt."""
        x0, y0, x1, y1 = rect
        m = names_mask(self.G, [r"^road0[2-58]\.frm$", r"^road16\.frm$"])
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self._set(x, y, m, True)

    def floor(self, rect, family):
        x0, y0, x1, y1 = rect
        m = self.G.mask_of([family] if isinstance(family, str) else family)
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.zones[(x, y)] = m
                self.wear[(x, y)] = 0.0

    def floor_cells(self, cells, family):
        m = self.G.mask_of([family] if isinstance(family, str) else family)
        for c in cells:
            self.zones[c] = m
            self.wear[c] = 0.0

    def clear(self, cells):
        for c in cells:
            self.zones.pop(c, None)
            self.wear.pop(c, None)

    def solve(self, rng):
        return solve(self.zones, self.G, rng, self.wear)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("cmd", choices=["learn", "demo"])
    args = ap.parse_args()
    learn() if args.cmd == "learn" else demo()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
