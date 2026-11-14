#!/usr/bin/env python3
"""
paint_corners.py - paint ground into the void corners that a map's edge box
(maps/<name>.edg, tools/map_edges.py) still takes in, the way the originals have
floor painted out past their boxes.

    python tools/paint_corners.py --preview build/paint Clairemont.map LAMESA.MAP RWRUINS.MAP
    python tools/paint_corners.py --write Clairemont.map LAMESA.MAP RWRUINS.MAP

Why: across about 1,200 boxes in Fallout, Fallout 2 and the TCs (tools/edge_survey.py),
a box never covers void; 30-68% of the floor lies beyond it. Some of FOC's maps end
their floor so close to the town that the box has to take in a few corners of void.

How (not the rejected 2026-10-03 fill, which picked each new tile at random from the
tiles nearby and broke the ground into a patchwork): each void patch is filled with
a copy of a whole block of the map's own ground, shifted into place, so every tile
keeps the neighbours it was painted with and the only seam is where the patch meets
the old edge. The shift is chosen from the map itself: which tile sits beside which
in its existing floor is counted, and the shift whose seam pairs are the most like
that wins (a road reaching the patch is carried on the same way, since its pairs
are road-beside-road). The block copied from must be outdoor ground or street, and
none of it may be void or part of another patch.

Only squares whose tile reaches into the box (grown by GROW px) are painted, on
elevation 0; nothing else on the map changes. Both copies of the map are written
(old ones to _safety_backups/paint_corners/); refuses while gecko or the game runs.
"""

from __future__ import annotations

import argparse
import math
import shutil
import struct
import subprocess
import sys
import time
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib.paths import bare_name, inside  # noqa: E402
from foclib import GameIndex, parse_lst  # noqa: E402
from foclib.formats import MapFile  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
from foclib import config as _config  # noqa: E402
AUTH = ROOT / "mod" / "data" / "maps"
DEPL = ROOT / "base" / "mods" / "foc" / "maps"
BACKUP = ROOT / "_safety_backups" / "paint_corners"
W = 200
GROW = 64                     # px the box is grown by: painted ground reaches this far past it
MAX_SHIFT = 40                # squares a copied block may come from
OUTDOOR = ("edg", "edgs", "dearth", "fade", "road", "curb", "crbm", "line", "stop", "brn")


def hex_xy(t):
    x, y = t % W, t // W
    v3 = W - 1 - x
    return 48 * (v3 // 2) + 32 * (v3 & 1) + 16 * y, -12 * (v3 // 2) + 12 * y


def sq_poly(sq):
    c, r = sq % 100, sq // 100
    hx, hy = hex_xy(2 * r * W + 2 * c)
    sx, sy = hx - 48, hy - 2
    return [(sx + 48, sy), (sx + 80, sy + 24), (sx + 32, sy + 36), (sx, sy + 12)]


def edg_box(name):
    for p in AUTH.iterdir():
        if p.suffix.lower() == ".edg" and p.stem.lower() == Path(name).stem.lower():
            v = struct.unpack(">%di" % (p.stat().st_size // 4), p.read_bytes())
            L, T, R, B = v[3:7]
            x0, x1 = sorted((hex_xy(L)[0], hex_xy(R)[0]))
            y0, y1 = sorted((hex_xy(T)[1], hex_xy(B)[1]))
            return x0, y0, x1, y1
    sys.exit(f"{name}: no edge file; run tools/map_edges.py first")


def family(name):
    return name.lower().rsplit(".", 1)[0].rstrip("0123456789")


def neighbours(sq):
    c, r = sq % 100, sq // 100
    for dc, dr, d in ((1, 0, "h"), (-1, 0, "h-"), (0, 1, "v"), (0, -1, "v-")):
        if 0 <= c + dc < 100 and 0 <= r + dr < 100:
            yield (r + dr) * 100 + c + dc, d


def plan(name, gi, tiles):
    raw = (AUTH / name).read_bytes()
    m = MapFile.parse(raw, gi.proto)
    fl = list(m.floors[0])
    painted = lambda s: (fl[s] & 0xFFF) > 1  # noqa: E731
    x0, y0, x1, y1 = edg_box(name)
    x0, y0, x1, y1 = x0 - GROW, y0 - GROW, x1 + GROW, y1 + GROW
    todo = set()
    for s in range(10000):
        if painted(s):
            continue
        pts = sq_poly(s)
        if max(p[0] for p in pts) >= x0 and min(p[0] for p in pts) <= x1 and \
                max(p[1] for p in pts) >= y0 and min(p[1] for p in pts) <= y1:
            todo.add(s)
    # which tile sits beside which, in the map's own floor
    pairs = Counter()
    for s in range(10000):
        if not painted(s):
            continue
        for n, d in neighbours(s):
            if d in ("h", "v") and painted(n):
                pairs[(d, fl[s] & 0xFFF, fl[n] & 0xFFF)] += 1
    total = sum(pairs.values())

    def pair_score(d, a, b):
        if d == "h-":
            d, a, b = "h", b, a
        elif d == "v-":
            d, a, b = "v", b, a
        return math.log((pairs[(d, a, b)] + 0.1) / (total + 1))

    def outdoor(s):
        t = fl[s] & 0xFFF
        return t < len(tiles) and family(tiles[t]) in OUTDOOR
    # patches
    seen, patches = set(), []
    for s in sorted(todo):
        if s in seen:
            continue
        comp, stack = [], [s]
        seen.add(s)
        while stack:
            a = stack.pop()
            comp.append(a)
            for n, _ in neighbours(a):
                if n in todo and n not in seen:
                    seen.add(n)
                    stack.append(n)
        patches.append(comp)
    fills = {}
    report = []
    for comp in patches:
        cset = set(comp)
        seam = [(a, n, d) for a in comp for n, d in neighbours(a) if n not in cset and painted(n)]
        best = None
        for dr in range(-MAX_SHIFT, MAX_SHIFT + 1):
            for dc in range(-MAX_SHIFT, MAX_SHIFT + 1):
                if dc == 0 and dr == 0:
                    continue
                ok = True
                for a in comp:
                    c, r = a % 100 - dc, a // 100 - dr
                    if not (0 <= c < 100 and 0 <= r < 100):
                        ok = False
                        break
                    src = r * 100 + c
                    if src in todo or not painted(src) or not outdoor(src):
                        ok = False
                        break
                if not ok:
                    continue
                sc = 0.0
                for a, n, d in seam:
                    src = (a // 100 - dr) * 100 + a % 100 - dc
                    sc += pair_score(d, fl[src] & 0xFFF, fl[n] & 0xFFF)
                sc -= 0.02 * (abs(dc) + abs(dr))
                if best is None or sc > best[0]:
                    best = (sc, dc, dr)
        if best is None:
            # too big to copy in one piece: desert a chunk at a time (shift_map.fill_desert)
            import shift_map
            res = shift_map.fill_desert(fl, set(comp), tiles)
            if res is None:
                report.append(f"   {len(comp):4d} squares: no ground to copy - left as void")
                continue
            got, sh = res
            fills.update(got)
            for a2, v in got.items():
                fl[a2] = v
            report.append(f"   {len(comp):4d} squares filled with desert a chunk at a time")
            continue
        _, dc, dr = best
        for a in comp:
            src = (a // 100 - dr) * 100 + a % 100 - dc
            fills[a] = fl[src]
        cs = [a % 100 for a in comp]
        rs = [a // 100 for a in comp]
        report.append(f"   {len(comp):4d} squares, col {min(cs)}-{max(cs)} row {min(rs)}-{max(rs)}: "
                      f"copied from {dc:+d},{dr:+d} squares away (seam score {best[0] / max(1, len(seam)):.2f} a pair)")
    out = bytearray(raw)
    for s, v in fills.items():
        struct.pack_into(">H", out, m.tiles_offset + 4 * s + 2, v)
    check = MapFile.parse(bytes(out), gi.proto)
    assert all(check.floors[0][s] == v for s, v in fills.items())
    return bytes(out), report, (x0 + GROW, y0 + GROW, x1 - GROW, y1 - GROW)


def render(path, out, box=None, crop=None):
    D = []
    order = (ROOT / "base" / "mods" / "mods_order.txt").read_text().split()
    for d in ["base/master.dat", "base/critter.dat"] + [f"base/mods/{n}" for n in order] + ["base/mods/foc"]:
        if (ROOT / d).exists():
            D += ["--data", str(ROOT / d)]
    r = subprocess.run([str(_config.gecko_cli()), "map", "render",
                        "--map", str(path), "--elevation", "0", "--out", str(out), "--max-dim", "2000", "--full"] + D,
                       capture_output=True, text=True)
    import re
    from PIL import Image, ImageDraw
    m = re.search(r"origin=(-?\d+),(-?\d+) scale=([\d.]+)", r.stdout + r.stderr)
    ox, oy, s = int(m.group(1)), int(m.group(2)), float(m.group(3))
    im = Image.open(out).convert("RGB")
    if box:
        d = ImageDraw.Draw(im)
        bx0, by0 = (box[0] + 32 - ox) * s, (box[1] + 1212 - oy) * s
        bx1, by1 = (box[2] + 32 - ox) * s, (box[3] + 1212 - oy) * s
        for k in range(2):
            d.rectangle([bx0 - k, by0 - k, bx1 + k, by1 + k], outline=(255, 0, 255))
    bbox = im.convert("L").point(lambda v: 255 if v > 12 else 0).getbbox()
    if bbox:
        im = im.crop((max(0, bbox[0] - 20), max(0, bbox[1] - 20), min(im.width, bbox[2] + 20), min(im.height, bbox[3] + 20)))
    im.save(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("maps", nargs="+")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--preview", metavar="DIR")
    g.add_argument("--write", action="store_true")
    args = ap.parse_args()
    if args.write:
        running = subprocess.run(["tasklist"], capture_output=True, text=True).stdout.lower()
        if "gecko" in running or "fallout2" in running:
            sys.exit("close gecko and the game first")
    gi = GameIndex(pristine=False)
    tiles = parse_lst(gi.vfs.read("art\\tiles\\tiles.lst"))
    for name in map(bare_name, args.maps):
        if (DEPL / name).read_bytes() != (AUTH / name).read_bytes():
            sys.exit(f"{name}: the deployed copy differs from mod/data/maps; run tools/sync_maps.py first")
        data, report, box = plan(name, gi, tiles)
        print(name)
        for r in report:
            print(r)
        if args.preview:
            d = inside(args.preview)
            d.mkdir(parents=True, exist_ok=True)
            (d / f"new_{name}").write_bytes(data)
            render(AUTH / name, d / f"{Path(name).stem}_before.png", box)
            render(d / f"new_{name}", d / f"{Path(name).stem}_after.png", box)
        else:
            BACKUP.mkdir(parents=True, exist_ok=True)
            stamp = time.strftime("%Y%m%d_%H%M%S")
            shutil.copy2(AUTH / name, BACKUP / f"{name}.{stamp}")
            (AUTH / name).write_bytes(data)
            (DEPL / name).write_bytes(data)
            print(f"   wrote {name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
