#!/usr/bin/env python3
"""
exit_restyle.py - rebuild every exit-grid patch on FOC maps the way Fallout,
Fallout 2 and the TCs lay them out.

    python tools/exit_restyle.py --preview build/exits      # report + before/after renders, nothing written
    python tools/exit_restyle.py                            # write (backups kept)
    python tools/exit_restyle.py RWCORE.MAP Clairemont.map  # only these maps

Why: our exit patches were thin strips, two or three hexes deep, filled with
one exit-grid variant whatever the edge, so in gecko they show as flat bars
with stair-stepped ends (owner, 2026-10-17: "they look weird").

What the originals do (tools/exit_survey.py, gecko renders of FO2's maps):
each exit-grid object draws a large translucent patch (art/misc/exitgrd1-8,
ext2grd1-8 in red for the world map), and the eight variants are the four
directions of a map edge, each in two halves that overhang to opposite sides:
  1 / 2  flat band   (top and bottom edges)       pid 0x05000010 / 0x05000011
  3 / 4  tall band   (left and right edges)       pid 0x05000012 / 0x05000013
  5 / 6  shallow /   (edges along a hex row y)    pid 0x05000014 / 0x05000015
  7 / 8  steep \\     (edges along a hex column x) pid 0x05000016 / 0x05000017
An exit zone is a broad band with straight edges, several hexes deep, from
the line the player crosses out to the edge of the walkable ground. The art
is only seen in the editor; the engine itself only asks whether the hex the
player steps on holds an exit grid (object.cc isExitGridAt).

How: for each patch (exit hexes touching, same destination), the direction
of its long axis on screen picks the variant pair. The new zone is a
straight-sided box in screen rows/columns (flat and tall bands) or in hex
x/y (the two diagonals), spanning the old patch's full width, from its inner
edge out to the last walkable hex (at least MIN_DEPTH deep). Every walkable
hex inside gets an exit grid with the old destination; the half of the band
nearer each end uses the variant that overhangs inward, so the art stays
inside the zone. World-map exits store the red art, as the originals do.

Checked before writing, per map: walking from the start (and from every
arrival tile) without stepping on an exit reaches nothing that was beyond the
old exits; every arrival tile stays off and away from exit grids; every
patch is still reachable. Writes mod/data/maps/<name> and base/mods/foc/maps/<name>
after a backup in _safety_backups/exit_restyle/, and refuses if the two
copies differ (a gecko save not yet synced) or gecko or the game is running.
"""

from __future__ import annotations

import argparse
import math
import shutil
import struct
import subprocess
import sys
import time
from collections import deque, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib.paths import bare_name, inside  # noqa: E402
from foclib import GameIndex  # noqa: E402
from foclib.formats import MapFile  # noqa: E402
from mapbuild import MapBuilder, MapObjectSpec  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
from foclib import config as _config  # noqa: E402
AUTH = ROOT / "mod" / "data" / "maps"
DEPL = ROOT / "base" / "mods" / "foc" / "maps"
BACKUP = ROOT / "_safety_backups" / "exit_restyle"
W = 200
DIRS = {0: (-1, W - 1, W, W + 1, 1, -W), 1: (-W - 1, -1, W, 1, 1 - W, -W)}
FIRST, LAST = 0x05000010, 0x05000017
MIN_DEPTH = {"flat": 4, "tall": 3, "shallow": 3, "steep": 3}   # rows / columns / hex lines
MAX_DEPTH = {"flat": 8, "tall": 5, "shallow": 6, "steep": 6}
ARRIVAL_GAP = 2                                                  # hexes kept clear round arrivals
# variant pair per direction: (first half along the band, second half)
PAIRS = {"flat": (0x11, 0x10), "tall": (0x12, 0x13), "shallow": (0x15, 0x14), "steep": (0x16, 0x17)}


def is_exit(pid):
    return FIRST <= pid <= LAST


def screen(t):
    """Engine tileToScreenXY relative to tile 0's column (tile.cc)."""
    x, y = t % W, t // W
    v3 = W - 1 - x
    return 48 * (v3 // 2) + 32 * (v3 & 1) + 16 * y, -12 * (v3 // 2) + 12 * y


def row_of(t):
    return (t // W) - (W - 1 - t % W) // 2       # screen row: Y = 12 * row


def col_of(t):
    """Screen column, one hex (32 px) wide."""
    return screen(t)[0] // 32


def neighbours(t):
    for d in DIRS[(t % W) & 1]:
        n = t + d
        if 0 <= n < W * W and abs(n % W - t % W) <= 1:
            yield n


class MapInfo:
    def __init__(self, name, gi):
        self.name = name
        self.raw = (AUTH / name).read_bytes()
        self.m = MapFile.parse(self.raw, gi.proto)
        self.ends = {}
        self.exits = {}                      # tile -> (pid, obj, dest tuple)
        blocked = set()
        for o in self.m.objects:
            _, end = MapFile._read_object(self.raw, o.off, gi.proto)
            self.ends[o.off] = end
            if o.elevation != 0 or not (0 <= o.tile < W * W):
                continue
            t = o.pid >> 24
            flags = struct.unpack_from(">I", self.raw, o.off + 0x24)[0]
            if is_exit(o.pid):
                dest = struct.unpack_from(">4i", self.raw, end - 16)
                self.exits[o.tile] = (o.pid, o, dest)
            elif t in (2, 3) and not flags & 0x10:
                pr = gi.proto(o.pid) if t == 2 else None
                if pr is not None and pr.subtype == 0:      # a door
                    continue
                blocked.add(o.tile)
        fl = self.m.floors[self.m.elevations.index(0)]
        self.floor = lambda t: fl[(t // W // 2) * 100 + (t % W) // 2] > 1
        self.blocked = blocked

    def walkable(self, t):
        return self.floor(t) and t not in self.blocked


def flood(info, seeds, stop):
    seen = set(s for s in seeds if info.walkable(s))
    q = deque(seen)
    while q:
        t = q.popleft()
        if t in stop:
            continue
        for n in neighbours(t):
            if n not in seen and info.walkable(n):
                seen.add(n)
                q.append(n)
    return seen


def patches(exits):
    seen, out = set(), []
    for t in sorted(exits):
        if t in seen:
            continue
        dest = exits[t][2]
        comp, stack = [], [t]
        seen.add(t)
        while stack:
            a = stack.pop()
            comp.append(a)
            ax, ay = a % W, a // W
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    b = (ay + dy) * W + ax + dx
                    if b in exits and b not in seen and exits[b][2] == dest:
                        seen.add(b)
                        stack.append(b)
        out.append((dest, comp))
    return out


def direction(tiles):
    """flat / tall / shallow / steep from the long axis of the patch on screen."""
    pts = [screen(t) for t in tiles]
    mx = sum(p[0] for p in pts) / len(pts)
    my = sum(p[1] for p in pts) / len(pts)
    sxx = sum((p[0] - mx) ** 2 for p in pts)
    syy = sum((p[1] - my) ** 2 for p in pts)
    sxy = sum((p[0] - mx) * (p[1] - my) for p in pts)
    ang = math.degrees(0.5 * math.atan2(2 * sxy, sxx - syy)) % 180
    cands = {"flat": 0, "tall": 90, "shallow": math.degrees(math.atan2(-12, 48)) % 180,
             "steep": math.degrees(math.atan2(12, 16))}
    return min(cands, key=lambda k: min(abs(ang - cands[k]), 180 - abs(ang - cands[k])))


def axes(kind):
    """(lateral, depth) coordinate functions for a band of this kind."""
    if kind == "flat":
        return col_of, row_of
    if kind == "tall":
        return row_of, col_of
    if kind == "shallow":                 # lines of constant hex y
        return (lambda t: t % W), (lambda t: t // W)
    return (lambda t: t // W), (lambda t: t % W)      # steep: lines of constant hex x


def rebuild_zone(info, tiles, walk, centre, arrivals):
    """Per slice across the band: from where the old patch began (the line the
    player crosses stays put) out to the last walkable hex, MIN_DEPTH to
    MAX_DEPTH deep; the band's two ends are straight."""
    kind = direction(tiles)
    lat, dep = axes(kind)
    lo_l, hi_l = min(lat(t) for t in tiles), max(lat(t) for t in tiles)
    lo_d, hi_d = min(dep(t) for t in tiles), max(dep(t) for t in tiles)
    sign = 1 if (lo_d + hi_d) / 2 >= dep(centre) else -1     # +1: outward is larger dep
    old = defaultdict(list)
    for t in tiles:
        old[lat(t)].append(dep(t))
    ground = defaultdict(list)
    for t in walk:
        if lo_l <= lat(t) <= hi_l:
            ground[lat(t)].append(t)
    keep_clear = set()
    for a in arrivals:
        ring = {a}
        for _ in range(ARRIVAL_GAP):
            ring |= {n for r in ring for n in neighbours(r)}
        keep_clear |= ring
    # the old patch's edge nearest the play area, slice by slice (signed: larger is further out),
    # straightened: a least-squares line through it, so the zone's inner edge is a straight line
    pts = [(L, min(sign * d for d in ds)) for L, ds in old.items()]
    n = len(pts)
    mL = sum(p[0] for p in pts) / n
    mI = sum(p[1] for p in pts) / n
    var = sum((p[0] - mL) ** 2 for p in pts)
    slope = sum((p[0] - mL) * (p[1] - mI) for p in pts) / var if var else 0.0
    raw = dict(pts)
    # shift the line in until it is nowhere outside the old edge (nothing behind the old
    # exits opens up); a patch whose edge is too crooked for that keeps its own edge
    shift = max(0, max(round(mI + slope * (L - mL)) - raw[L] for L in raw))
    straight = shift <= 3
    known = sorted(raw)
    zone = []
    for L in range(lo_l, hi_l + 1):
        if straight:
            inner = round(mI + slope * (L - mL)) - shift
        else:
            inner = raw[min(known, key=lambda k: abs(k - L))]
        outward = [sign * dep(t) for t in ground[L] if sign * dep(t) >= inner]
        if not outward:
            continue
        outer = min(max(outward), inner + MAX_DEPTH[kind] - 1)
        start = min(inner, outer - MIN_DEPTH[kind] + 1)
        zone += [t for t in ground[L] if start <= sign * dep(t) <= outer and t not in keep_clear]
    mid = (lo_l + hi_l) / 2
    first, second = PAIRS[kind]
    pids = {t: FIRST - 0x10 + (first if lat(t) <= mid else second) for t in zone}
    return kind, zone, pids


def build(name, gi, arrivals_into):
    info = MapInfo(name, gi)
    if (DEPL / name).read_bytes() != info.raw:
        sys.exit(f"{name}: the deployed copy differs from mod/data/maps; run tools/sync_maps.py first")
    if not info.exits:
        return None, []
    start = info.m.default_tile
    arrivals = {start} | set(arrivals_into.get(name.upper(), ()))
    walk = flood(info, arrivals, set())                 # all ground the player can stand on
    old_inside = flood(info, arrivals, set(info.exits))
    beyond = walk - old_inside - set(info.exits)
    new = {}
    report = []
    for dest, tiles in patches(info.exits):
        kind, zone, pids = rebuild_zone(info, tiles, walk, start, arrivals)
        for t in zone:
            if t in new and new[t][1] != dest:
                continue
            new[t] = (pids[t], dest)
        report.append(f"   {len(tiles):4d} -> {len(zone):4d} hexes  {kind:7s} -> map {dest[0]} tile {dest[1]}")
    # the new exits must still stop the player before anything that was beyond the old ones:
    # a hex behind the old exits that the player could now walk to becomes part of the nearest zone
    absorbed = 0
    for _ in range(50):
        new_inside = flood(info, arrivals, set(new))
        leak = (new_inside & beyond) - set(new)
        if not leak:
            break
        for t in leak:
            near = min(new, key=lambda z: abs(z % W - t % W) + abs(z // W - t // W))
            new[t] = new[near]
            absorbed += 1
    if absorbed:
        report.append(f"   {absorbed} hexes behind the old exits added to the zones")
    new_inside = flood(info, arrivals, set(new))
    leak = (new_inside & beyond) - set(new)
    problems = []
    if leak:
        problems.append(f"{len(leak)} hexes beyond the old exits can now be walked to, e.g. "
                        + ", ".join(f"({t % W},{t // W})" for t in sorted(leak)[:5]))
    for a in arrivals:
        if a in new or any(n in new for n in neighbours(a)):
            problems.append(f"arrival ({a % W},{a // W}) is on or next to an exit grid")
    for dest in {d for _, d in new.values()}:
        if not any(t in new_inside for t, (_, d) in new.items() if d == dest):
            problems.append(f"exits to map {dest[0]} can't be reached")
    # write: drop the old exit objects, add the new ones to elevation 0
    builder = MapBuilder(name, elevations=tuple(info.m.elevations), map_id=0)
    groups = {e: [] for e in (0, 1, 2)}
    for o in info.m.objects:
        if is_exit(o.pid) and o.elevation == 0:
            continue
        groups[o.elevation].append(info.raw[o.off:info.ends[o.off]])
    pr = {p: gi.proto(p) for p in range(FIRST, LAST + 1)}
    for t in sorted(new, key=lambda t: (row_of(t), col_of(t))):
        pid, dest = new[t]
        # misc art: exitgrd1-8 at 17-24, the world map's red ext2grd1-8 at 33-40
        art = (pid & 0xFF) + 1 + (16 if dest[0] < 0 else 0)
        flags = pr[pid].flags - (1 << 32) if pr[pid].flags >= 1 << 31 else pr[pid].flags
        buf = bytearray()
        builder._write_object(buf, MapObjectSpec(pid=pid, fid=0x05000000 | art, tile=t, elevation=0,
                                                 flags=flags, extras=tuple(dest)))
        groups[0].append(bytes(buf))
    first = info.m.objects[0].off
    if info.m.objects[0].elevation != 0:
        sys.exit(f"{name}: no objects on elevation 0")
    out = bytearray(info.raw[:first - 8])          # up to the total object count
    out += struct.pack(">i", sum(len(g) for g in groups.values()))
    for e in (0, 1, 2):
        out += struct.pack(">i", len(groups[e]))
        for d in groups[e]:
            out += d
    out += info.raw[max(info.ends.values()):]
    MapFile.parse(bytes(out), gi.proto)
    return bytes(out), report + [f"   PROBLEM: {p}" for p in problems]


def arrivals_by_map(gi, names):
    ids = {}
    for line in (ROOT / "mod" / "ids" / "maps.txt").read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if parts and parts[0].isdigit():
            ids[int(parts[0])] = parts[1].upper()
    out = defaultdict(set)
    for p in DEPL.iterdir():
        if p.suffix.lower() != ".map":
            continue
        try:
            info = MapInfo(p.name, gi)
        except Exception:
            continue
        for _pid, _o, dest in info.exits.values():
            if dest[0] in ids and dest[1] >= 0 and dest[2] == 0:
                out[ids[dest[0]]].add(dest[1])
    return out


def render(path, out, crops):
    D = []
    order = (ROOT / "base" / "mods" / "mods_order.txt").read_text().split()
    for d in ["base/master.dat", "base/critter.dat"] + [f"base/mods/{n}" for n in order] + ["base/mods/foc"]:
        if (ROOT / d).exists():
            D += ["--data", str(ROOT / d)]
    g = str(_config.gecko_cli())
    for i, c in enumerate(crops):
        subprocess.run([g, "map", "render", "--map", str(path), "--elevation", "0", "--out", f"{out}_{i}.png",
                        "--max-dim", "900", "--crop-hex", str(c), "--crop-extent", "420"] + D,
                       capture_output=True, check=False)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("maps", nargs="*")
    ap.add_argument("--preview", metavar="DIR")
    args = ap.parse_args()
    running = subprocess.run(["tasklist"], capture_output=True, text=True).stdout.lower()
    if not args.preview and ("gecko" in running or "fallout2" in running):
        sys.exit("close gecko and the game first")
    gi = GameIndex(pristine=False)
    names = [bare_name(n) for n in args.maps] or sorted(p.name for p in AUTH.iterdir() if p.suffix.lower() == ".map" and (DEPL / p.name).exists())
    arr = arrivals_by_map(gi, names)
    results, bad = {}, False
    for name in names:
        data, report = build(name, gi, arr)
        if data is None:
            continue
        print(name)
        for r in report:
            print(r)
        if any("PROBLEM" in r for r in report):
            bad = True
        results[name] = data
    if args.preview:
        d = inside(args.preview)
        d.mkdir(parents=True, exist_ok=True)
        for name, data in results.items():
            info = MapInfo(name, gi)
            crops = [tiles[len(tiles) // 2] for _, tiles in patches(info.exits)]
            (d / f"new_{name}").write_bytes(data)
            render(AUTH / name, d / f"{Path(name).stem}_before", crops)
            render(d / f"new_{name}", d / f"{Path(name).stem}_after", crops)
        print(f"--preview: test copies and before/after renders in {d}; nothing written")
        return 1 if bad else 0
    if bad:
        sys.exit("not writing: fix the problems above first")
    BACKUP.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    for name, data in results.items():
        shutil.copy2(AUTH / name, BACKUP / f"{name}.{stamp}")
        (AUTH / name).write_bytes(data)
        (DEPL / name).write_bytes(data)
        print(f"wrote {name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
