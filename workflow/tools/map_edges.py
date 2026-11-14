#!/usr/bin/env python3
"""
map_edges.py - give FOC's maps Hi-Res Patch edge files (maps/<name>.edg), so the
camera stops at the edge of the painted ground and the void beyond is never seen.

    python tools/map_edges.py --preview build/edges       # renders with the boxes drawn on, nothing written
    python tools/map_edges.py                             # write mod/data/maps/*.edg (+ the deployed copies)
    python tools/map_edges.py Clairemont.map CLCAMP.MAP   # only these maps

Why: at the original 640x480 a ring of scroll blockers (misc proto 12) keeps the
camera in, but we run 1920x1080 at 2x through Mash's Hi-Res Patch, whose view is
far bigger, and this HRP build ignores scroll blockers anyway (f2_res.ini note,
2026-09-08). The HRP reads maps\\<name>.edg instead (f2_res.dll: "maps\\%s" ".edg"):
with EDGE_CLIPPING_ON=1 it stops scrolling at that box and draws everything outside
it black, a clean straight cut-off. RPU ships one for every vanilla map; FOC's maps
had none, so the camera wandered off into the void.

The format (v1, big-endian int32s, as RPU's files and gecko's MapEdgeReader):
'EDGE', 1, 0, then per elevation one zone {left, top, right, bottom}, each a hex
whose screen position gives that side (left/right by X, top/bottom by Y), with the
next elevation's number between zones. An elevation with nothing on it gets the
whole grid {199, 0, 39800, 39999}, as vanilla does.

Where the box goes, as on the vanilla maps (Arroyo, the vaults, the caves): over
the painted ground and around everything the player can walk to. Two gecko renders
of each elevation are read on a CELL-px grid: the floor-only one (--schematic: black
is where no floor is painted, the void) and the normal one (black there but floor
in the other is ground under a black frame drawn with gecko's Black Edge tool).
Every box is scored, and the best one wins (a maximum-sum search over the grid):
  + 1 for each walkable hex on visible ground, + EXIT_WEIGHT for each exit hex
  + GROUND_GAIN for each cell of visible ground, so the box reaches the ground's edge
  - FRAME_COST for a cell under a black frame (it looks the same as the cut-off)
  - VOID_COST for a cell of void, so the box stays on painted ground unless covering
    more of the town is worth it; cells within MARGIN px of the void count as void,
    so the cut-off never lands on the ground's ragged last row
On the floors above and below ground (elevation 1 and 2), a building stands in the
void the way it does in the originals' interiors, so the void costs little there
(INSIDE_VOID_COST) and the box takes in the whole building. A map in UNDERGROUND (the
Facility Below) is rooms in the void on every floor, as in the originals' vaults and
caves, so its box is simply round everything walkable, UNDERGROUND_PAD cells out, and
tools/rock_field.py then lays the originals' rock field under it (which does not
count as walkable ground here, so a re-run gives the same box). The report says how
much of the visible walkable ground and how many visible exit hexes are left
outside the box, and how much void is inside it; the preview tints that void red.
"""

from __future__ import annotations

import argparse
import re
import struct
import subprocess
import sys
from collections import deque
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib.paths import bare_name, inside  # noqa: E402
from foclib import GameIndex  # noqa: E402
from foclib.formats import MapFile  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
from foclib import config as _config  # noqa: E402
AUTH = ROOT / "mod" / "data" / "maps"
DEPL = ROOT / "base" / "mods" / "foc" / "maps"
GECKO = _config.gecko_cli()
W = 200
DIRS = {0: (-1, W - 1, W, W + 1, 1, -W), 1: (-W - 1, -1, W, 1, 1 - W, -W)}
FULL_GRID = (199, 0, 39800, 39999)
GECKO_OFFSET = (32, 1212)      # gecko world = engine tileToScreenXY (relative to column 0) + this
CELL = 16                      # world px per grid cell
VOID_LEVEL = 12                # a render pixel this dark or darker is void (or black)
MARGIN = 16                    # world px the box is kept in from the void
EXIT_WEIGHT = 40               # an exit hex counts as this many walkable hexes: exits stay in view
GROUND_GAIN = 0.15             # per cell of visible ground
FRAME_COST = 0.05              # per cell under a black frame
VOID_COST = 2.0                # per cell of void, at ground level
INSIDE_VOID_COST = 0.1         # per cell of void, on the floors above and below
# maps underground on every floor (the vaults and caves in the originals): the
# rooms stand in the void at ground level too, so it costs INSIDE_VOID_COST there
UNDERGROUND = {"RWFAC1.MAP", "RWFAC2.MAP", "RWFAC3.MAP"}
UNDERGROUND_PAD = (8, 4)       # cells kept round the walkable ground there: above (walls rise), elsewhere
ROCK_FIELD = ("cav4009.frm", "cav4010.frm")   # the rock round underground rooms (tools/rock_field.py)
VIEW = (960, 540)              # what one screen shows: 1920x1080 at SCALE_2X
FEATURE_REACH = 384            # px round an object or paved square where walkable ground counts fully
OPEN_GROUND_WEIGHT = 0.03      # a walkable hex out in the open, far from anything
# invisible helpers that are not something to see: blocking hexes (scenery and wall),
# the exit-grid map marker and light sources
BLOCKER_PIDS = (0x02000031, 0x02000043, 0x02000080, 0x0200008D, 0x02000158, 0x0300026D, 0x0300026E)
PAVED = ("road", "curb", "crbm", "cmt", "brick", "tp", "line", "stop", "gar")


TILE_NAMES = []


def screen(t):
    """Engine tileToScreenXY relative to tile 0's column (tile.cc), in gecko world px."""
    x, y = t % W, t // W
    v3 = W - 1 - x
    return (48 * (v3 // 2) + 32 * (v3 & 1) + 16 * y + GECKO_OFFSET[0],
            -12 * (v3 // 2) + 12 * y + GECKO_OFFSET[1])


def data_args():
    D = []
    order = (ROOT / "base" / "mods" / "mods_order.txt").read_text().split()
    for d in ["base/master.dat", "base/critter.dat"] + [f"base/mods/{n}" for n in order] + ["base/mods/foc"]:
        if (ROOT / d).exists():
            D += ["--data", str(ROOT / d)]
    return D


def render(path, elev, out, maxdim, style=()):
    r = subprocess.run([str(GECKO), "map", "render", "--map", str(path), "--elevation", str(elev),
                        "--out", str(out), "--max-dim", str(maxdim), "--full"] + list(style) + data_args(),
                       capture_output=True, text=True)
    m = re.search(r"origin=(-?\d+),(-?\d+) scale=([\d.]+)", r.stdout + r.stderr)
    if not m:
        sys.exit(f"gecko-cli render failed for {path} e{elev}: {r.stdout}{r.stderr}")
    return int(m.group(1)), int(m.group(2)), float(m.group(3))


def walkable(raw, m, gi, elev, seeds, skip=()):
    """Hexes the player can stand on at this elevation (flood from seeds, or every open
    floored hex when there are no seeds), and its exit-grid hexes. With skip (the rock
    field's tiles round underground rooms) the flood starts from the seeds and every floor
    hex that isn't the rock field, and goes on through any floor: a cave floored with the
    rock field's own tiles counts where the rooms reach it, the field beyond the walls
    doesn't."""
    blocked, exits = set(), set()
    for o in m.objects:
        if o.elevation != elev or not (0 <= o.tile < W * W):
            continue
        t = o.pid >> 24
        flags = struct.unpack_from(">I", raw, o.off + 0x24)[0]
        if t == 5 and 0x05000010 <= o.pid <= 0x05000017:
            exits.add(o.tile)
        elif t in (2, 3) and not flags & 0x10:
            pr = gi.proto(o.pid) if t == 2 else None
            if pr is not None and pr.subtype == 0:
                continue
            blocked.add(o.tile)
    fl = m.floors[m.elevations.index(elev)]

    def floored(t):
        v = fl[(t // W // 2) * 100 + (t % W) // 2]
        return v > 1 and t not in blocked

    def ok(t):
        return floored(t) and (fl[(t // W // 2) * 100 + (t % W) // 2] & 0xFFF) not in skip
    if skip:
        seen = {t for t in range(W * W) if ok(t)} | {s for s in seeds if floored(s)}
        step = floored
    elif not seeds:
        return {t for t in range(W * W) if ok(t)}, exits
    else:
        seen = {s for s in seeds if ok(s)}
        step = ok
    q = deque(seen)
    while q:
        t = q.popleft()
        for d in DIRS[(t % W) & 1]:
            n = t + d
            if 0 <= n < W * W and abs(n % W - t % W) <= 1 and n not in seen and step(n):
                seen.add(n)
                q.append(n)
    return seen, exits


def best_box(V):
    """The box (r0, c0, r1, c1) with the largest sum of V: for every pair of columns,
    a running maximum-sum run over the rows (Kadane), all columns at once."""
    import numpy as np
    H, Wd = V.shape
    best = (-np.inf, None)
    for c0 in range(Wd):
        rows = np.cumsum(V[:, c0:], axis=1)            # rows[r, k] = sum of V[r, c0..c0+k]
        run = np.zeros(rows.shape[1])
        run_start = np.zeros(rows.shape[1], dtype=int)
        for r in range(H):
            extend = run + rows[r]
            restart = rows[r]
            take = extend >= restart
            run_start = np.where(take, run_start, r)
            run = np.where(take, extend, restart)
            k = int(np.argmax(run))
            if run[k] > best[0]:
                best = (run[k], (int(run_start[k]), c0, r, c0 + k))
    return best[1]


def hex_for(target, axis, toward, hexes):
    """A hex whose screen coordinate on `axis` (0 X, 1 Y) is the nearest to target on
    the `toward` side (+1: >= target, -1: <= target)."""
    best = None
    for t in hexes:
        v = screen(t)[axis]
        if (v - target) * toward < 0:
            continue
        d = abs(v - target)
        if best is None or d < best[0]:
            best = (d, t)
    return best[1]


def elevation_box(name, path, raw, m, gi, elev, seeds, work):
    import numpy as np
    stem = Path(name).stem
    png = work / f"{stem}_e{elev}.png"
    fpng = work / f"{stem}_e{elev}_floor.png"
    proj = render(path, elev, png, 2400)
    if render(path, elev, fpng, 2400, ("--schematic",)) != proj:
        sys.exit(f"{name} e{elev}: the two renders don't line up")
    ox, oy, s = proj
    nat = Image.open(png).convert("L")
    flo = Image.open(fpng).convert("L")
    cw, ch = int(nat.width / s / CELL) + 1, int(nat.height / s / CELL) + 1

    def grid(im):
        g = im.resize((cw, ch), Image.BOX).point(lambda v: 255 if v > VOID_LEVEL else 0)
        return g.filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.MinFilter(3))
    floor_g = grid(flo)
    k = 2 * (MARGIN // CELL) + 1
    if k > 1:
        floor_g = floor_g.filter(ImageFilter.MinFilter(k))
    floor = np.array(floor_g) > 0
    visible = (np.array(grid(nat)) > 0) & floor
    skip = ()
    if name.upper() in UNDERGROUND:
        skip = {i for i, n in enumerate(TILE_NAMES) if n.lower() in ROCK_FIELD}
    walk, exits = walkable(raw, m, gi, elev, seeds, skip)
    content = np.zeros((ch, cw))
    seen_walk = set()
    seen_exit = set()

    def cell_of(t):
        x, y = screen(t)
        return int((y - oy) / CELL), int((x - ox) / CELL)
    # what there is to see: objects (not exit grids or invisible blockers) and paved floor;
    # ground counts fully only within FEATURE_REACH px of one
    feat = Image.new("L", (cw, ch), 0)
    fpx = feat.load()
    fl = m.floors[m.elevations.index(elev)]
    for o in m.objects:
        if o.elevation != elev or not (0 <= o.tile < W * W) or (o.pid >> 24) not in (0, 1, 2, 3):
            continue
        if o.pid in BLOCKER_PIDS:
            continue
        r, c = cell_of(o.tile)
        if 0 <= r < ch and 0 <= c < cw:
            fpx[c, r] = 255
    for sq in range(10000):
        t = fl[sq] & 0xFFF
        if t > 1 and t < len(TILE_NAMES) and TILE_NAMES[t].lower().startswith(PAVED):
            r, c = cell_of((sq // 100) * 2 * W + (sq % 100) * 2)
            if 0 <= r < ch and 0 <= c < cw:
                fpx[c, r] = 255
    reach = 2 * (FEATURE_REACH // CELL) + 1
    near = np.array(feat.filter(ImageFilter.MaxFilter(reach if reach % 2 else reach + 1))) > 0
    for t in walk | exits:
        r, c = cell_of(t)
        if 0 <= r < ch and 0 <= c < cw and visible[r, c]:
            if t in exits:
                content[r, c] += EXIT_WEIGHT
                seen_exit.add(t)
            else:
                content[r, c] += 1 if near[r, c] else OPEN_GROUND_WEIGHT
                seen_walk.add(t)
    void_cost = VOID_COST if (elev == 0 and name.upper() not in UNDERGROUND) else INSIDE_VOID_COST
    V = content + np.where(visible, GROUND_GAIN, np.where(floor, -FRAME_COST, -void_cost))
    for t in seeds:
        r, c = cell_of(t)
        if 0 <= r < ch and 0 <= c < cw:
            V[r, c] += 1e6
    # search only around what there is
    rows = np.where((floor | (content > 0)).any(axis=1))[0]
    cols = np.where((floor | (content > 0)).any(axis=0))[0]
    if len(rows) == 0:
        return None, png, proj, "no ground"
    pad = 4
    r_lo, r_hi = max(0, rows[0] - pad), min(ch - 1, rows[-1] + pad)
    c_lo, c_hi = max(0, cols[0] - pad), min(cw - 1, cols[-1] + pad)
    if name.upper() in UNDERGROUND:
        # underground every room stands in the void, so the box is simply round
        # everything the player can walk to, as in the originals' vaults and caves
        cells = [cell_of(t) for t in seen_walk | seen_exit]
        if not cells:
            return None, png, proj, "nothing walkable"
        up, side = UNDERGROUND_PAD
        r0 = max(0, min(r for r, _ in cells) - up)
        r1 = min(ch - 1, max(r for r, _ in cells) + side)
        c0 = max(0, min(c for _, c in cells) - side)
        c1 = min(cw - 1, max(c for _, c in cells) + side)
    else:
        box = best_box(V[r_lo:r_hi + 1, c_lo:c_hi + 1])
        if box is None:
            return None, png, proj, "no box"
        r0, c0, r1, c1 = box[0] + r_lo, box[1] + c_lo, box[2] + r_lo, box[3] + c_lo
    X0, X1 = ox + c0 * CELL, ox + (c1 + 1) * CELL
    Y0, Y1 = oy + r0 * CELL, oy + (r1 + 1) * CELL
    grid_hexes = range(W * W)
    zone = (hex_for(X0, 0, +1, grid_hexes), hex_for(Y0, 1, +1, grid_hexes),
            hex_for(X1, 0, -1, grid_hexes), hex_for(Y1, 1, -1, grid_hexes))
    xs = (screen(zone[0])[0], screen(zone[2])[0])
    ys = (screen(zone[1])[1], screen(zone[3])[1])

    def inside(t):
        x, y = screen(t)
        return xs[0] <= x <= xs[1] and ys[0] <= y <= ys[1]
    out_walk = sum(1 for t in seen_walk if not inside(t))
    out_exit = sum(1 for t in seen_exit if not inside(t))
    raw_floor = np.array(flo.resize((cw, ch), Image.BOX).point(lambda v: 255 if v > VOID_LEVEL else 0)) > 0
    sub = raw_floor[r0:r1 + 1, c0:c1 + 1]
    void_pct = 100 * (1 - sub.mean()) if sub.size else 0
    note = (f"box {xs[1] - xs[0]}x{ys[1] - ys[0]} px (a screen is {VIEW[0]}x{VIEW[1]}); "
            f"visible walkable outside {out_walk}/{len(seen_walk)} ({100 * out_walk / max(1, len(seen_walk)):.1f}%), "
            f"visible exit hexes outside {out_exit}/{len(seen_exit)}, void inside {void_pct:.1f}%")
    return zone, png, proj, note


def edg_bytes(zones):
    out = struct.pack(">3i", 0x45444745, 1, 0)
    for e in (0, 1, 2):
        out += struct.pack(">4i", *zones[e])
        if e < 2:
            out += struct.pack(">i", e + 1)
    return out


def draw(png, proj, zone, out):
    ox, oy, s = proj
    im = Image.open(png).convert("RGB")
    d = ImageDraw.Draw(im)
    x0, x1 = (screen(zone[0])[0] - ox) * s, (screen(zone[2])[0] - ox) * s
    y0, y1 = (screen(zone[1])[1] - oy) * s, (screen(zone[3])[1] - oy) * s
    # what the player will see: black outside the box
    shade = Image.new("RGB", im.size, (0, 0, 0))
    m = Image.new("L", im.size, 150)
    ImageDraw.Draw(m).rectangle([x0, y0, x1, y1], fill=0)
    im = Image.composite(shade, im, m)
    fpng = Path(str(png).replace(".png", "_floor.png"))
    if fpng.exists():
        flo = Image.open(fpng).convert("L").point(lambda v: 0 if v > VOID_LEVEL else 255)
        boxmask = Image.new("L", im.size, 0)
        ImageDraw.Draw(boxmask).rectangle([x0, y0, x1, y1], fill=255)
        from PIL import ImageChops
        red = ImageChops.multiply(flo, boxmask).point(lambda v: 110 if v else 0)
        im = Image.composite(Image.new("RGB", im.size, (200, 0, 0)), im, red)
    d = ImageDraw.Draw(im)
    for k in range(2):
        d.rectangle([x0 - k, y0 - k, x1 + k, y1 + k], outline=(255, 0, 255))
    d.rectangle([x0, y0, x0 + VIEW[0] * s, y0 + VIEW[1] * s], outline=(0, 255, 255))
    # crop to the ground plus a border
    bbox = Image.open(png).convert("L").point(lambda v: 255 if v > VOID_LEVEL else 0).getbbox()
    if bbox:
        pad = 40
        im = im.crop((max(0, bbox[0] - pad), max(0, bbox[1] - pad),
                      min(im.width, bbox[2] + pad), min(im.height, bbox[3] + pad)))
    im.save(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("maps", nargs="*")
    ap.add_argument("--preview", metavar="DIR")
    ap.add_argument("--dir", type=Path, help="work on the maps in this folder (a layout being built), "
                                             "not mod/data/maps and the deployed copies")
    args = ap.parse_args()
    global AUTH, DEPL
    if args.dir:
        AUTH = inside(args.dir)
        DEPL = AUTH / "_no_deployed_copy"
    if not args.preview and not args.dir:
        running = subprocess.run(["tasklist"], capture_output=True, text=True).stdout.lower()
        if "gecko" in running or "fallout2" in running:
            sys.exit("close gecko and the game first")
    gi = GameIndex(pristine=False)
    from foclib import parse_lst
    TILE_NAMES.extend(parse_lst(gi.vfs.read(r"art\tiles\tiles.lst")))
    names = [bare_name(n) for n in args.maps] or sorted(p.name for p in AUTH.iterdir()
                                if p.suffix.lower() == ".map" and (DEPL / p.name).exists())
    work = inside(args.preview) if args.preview else ROOT / "build" / "edges"
    work.mkdir(parents=True, exist_ok=True)
    results = {}
    for name in names:
        path = AUTH / name
        raw = path.read_bytes()
        if (DEPL / name).exists() and (DEPL / name).read_bytes() != raw:
            sys.exit(f"{name}: the deployed copy differs from mod/data/maps; run tools/sync_maps.py first")
        m = MapFile.parse(raw, gi.proto)
        zones = {}
        print(name)
        for e in (0, 1, 2):
            fl = m.floors[m.elevations.index(e)] if e in m.elevations else None
            if fl is None or not any(v > 1 for v in fl):
                zones[e] = FULL_GRID
                continue
            seeds = [m.default_tile] if e == m.default_elevation else []
            zone, png, proj, note = elevation_box(name, path, raw, m, gi, e, seeds, work)
            if zone is None:
                zones[e] = FULL_GRID
                print(f"   e{e}: {note}; whole grid")
                continue
            zones[e] = zone
            print(f"   e{e}: zone {zone}  {note}")
            if args.preview:
                draw(png, proj, zone, work / f"{Path(name).stem}_e{e}_edge.png")
        results[name] = edg_bytes(zones)
    if args.preview:
        print(f"--preview: renders with the boxes drawn on in {work}; nothing written")
        return 0
    for name, data in results.items():
        stem = Path(name).stem
        for d in (AUTH, DEPL):
            if d.is_dir():
                (d / f"{stem}.edg").write_bytes(data)
        print(f"wrote {stem}.edg")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
