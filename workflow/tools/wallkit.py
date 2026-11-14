#!/usr/bin/env python3
"""
wallkit.py - learn how the original games and the total conversions build
walls, so FOC's maps can draw their own buildings instead of copying them.

    python tools/wallkit.py learn                  # every source but Olympus
    python tools/wallkit.py show nec               # what was learned for a family
    python tools/wallkit.py families --min 400     # the families worth drawing with

A Fallout wall is one piece per hex. A run along the hex x axis keeps its hex
y; a run along the hex y axis keeps its hex x. Corners, T-joints, crossings
and ends use their own pieces. Every wall object in every map is given its
neighbour signature - which of the four in-line neighbours (x-1, x+1, y-1,
y+1) are walls of the same family - and the pieces seen with each signature
are counted. Invisible blockers (block.frm) sit beside the runs; their
offsets from the run are learned too. Doors are learned the same way: the
door piece and the wall family on both sides of it.

The result, build/wallkit/kit.json, maps family -> signature -> pieces
(source, source pid, art, count). draw() in this module places a family's
pieces round any outline, choosing among the pieces seen with each signature
by their counts, and converts source pids to FOC pids with
port_stamp.Converter, as compose_map.py's lift does.

Olympus is never learned from: its assets are the owner's only.
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

import port  # noqa: E402
from foclib import MapFile, parse_lst  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "wallkit"
SOURCES = ["fo2", "fo1", "sonora", "nevada", "resurrection", "sotj", "yesterday"]
FAMILY_RE = re.compile(r"^([a-z_]+?)(\d+)\.frm$", re.I)


def family(art: str) -> str | None:
    m = FAMILY_RE.match(art.lower())
    if not m:
        return None
    f = m.group(1)
    return None if f in ("block", "blk", "wallblk", "scrblk") else f


def list_maps(src):
    names = set()
    for prov in src.vfs.providers:
        if prov.kind == "dat":
            for n in prov.dat.names():
                if n.lower().startswith("maps\\") and n.lower().endswith(".map"):
                    names.add(n.split("\\")[-1].lower())
        else:
            d = prov.root / "maps"
            if d.is_dir():
                names |= {f.lower() for f in os.listdir(d) if f.lower().endswith(".map")}
    return sorted(names)


def sig_of(x, y, cells):
    return "".join(c if (x + dx, y + dy) in cells else "." for c, dx, dy in
                   (("W", -1, 0), ("E", 1, 0), ("N", 0, -1), ("S", 0, 1)))


def per_source(c: Counter, n: int, idx: int = 0):
    """The top n entries of each source game (keys are 'source|...', or the
    source is field `idx`)."""
    by = defaultdict(list)
    for e, k in c.most_common():
        src = e.split("|")[idx]
        if len(by[src]) < n:
            by[src].append((e, k))
    return [x for v in by.values() for x in v]


def styles_of(adj: Counter, min_pair=4):
    """Sub-styles of a family: groups of pieces the games put side by side
    (union of adjacency pairs seen at least min_pair times), per source.
    Returns {source: [[art, ...], ...]} largest first."""
    out = {}
    by = defaultdict(list)
    for e, n in adj.items():
        src, a, b = e.split("|")
        if n >= min_pair:
            by[src].append((a, b))
    for src, pairs in by.items():
        par = {}

        def f(x):
            par.setdefault(x, x)
            while par[x] != x:
                par[x] = par[par[x]]
                x = par[x]
            return x
        for a, b in pairs:
            par[f(a)] = f(b)
        groups = defaultdict(set)
        for x in list(par):
            groups[f(x)].add(x)
        out[src] = [sorted(g) for g in sorted(groups.values(), key=len, reverse=True)]
    return out


def learn(sources):
    kit = defaultdict(lambda: {"sigs": defaultdict(Counter), "blockers": Counter(), "doors": defaultdict(Counter),
                               "floors": Counter(), "roofs": Counter(), "count": 0, "sources": Counter(),
                               "adj": Counter()})
    for sname in sources:
        try:
            src = port.open_source(sname)
        except Exception as e:
            print(sname, e)
            continue
        wl = [x.split(",")[0] for x in parse_lst(src.vfs.read("art\\walls\\walls.lst"))]
        sl = [x.split(",")[0] for x in parse_lst(src.vfs.read("art\\scenery\\scenery.lst"))]
        tl = [x.split(",")[0] for x in parse_lst(src.vfs.read("art\\tiles\\tiles.lst"))]
        n_maps = 0
        for name in list_maps(src):
            try:
                m = MapFile.parse(src.vfs.read(f"maps\\{name}"), src.proto)
            except Exception:
                continue
            n_maps += 1
            for ei, elev in enumerate(m.elevations):
                walls = {}
                blocks = set()
                doors = []
                for o in m.objects:
                    if o.elevation != elev or not (0 <= o.tile < 40000):
                        continue
                    t = o.pid >> 24
                    x, y = o.tile % 200, o.tile // 200
                    if t == 3:
                        art = wl[o.fid & 0xFFF] if (o.fid & 0xFFF) < len(wl) else ""
                        f = family(art)
                        if f:
                            walls[(x, y)] = (f, o.pid, art.lower())
                        elif art.lower().startswith("block"):
                            blocks.add((x, y, o.pid))
                    elif t == 2:
                        pr = src.proto(o.pid)
                        if pr is not None and pr.subtype == 0:
                            art = sl[o.fid & 0xFFF] if (o.fid & 0xFFF) < len(sl) else ""
                            doors.append((x, y, o.pid, art.lower(), o.rotation))
                byfam = defaultdict(set)
                for (x, y), (f, _, _) in walls.items():
                    byfam[f].add((x, y))
                for (x, y), (f, pid, art) in walls.items():
                    for dx, dy, d in ((1, 0, "x"), (0, 1, "y")):
                        nb = walls.get((x + dx, y + dy))
                        if nb and nb[0] == f:
                            kit[f]["adj"][f"{sname}|{d}|{art}|{nb[2]}"] += 1
                    s = sig_of(x, y, byfam[f]) + str(x & 1)
                    k = kit[f]
                    k["sigs"][s][f"{sname}|{pid}|{art}"] += 1
                    k["count"] += 1
                    k["sources"][sname] += 1
                    for (bx, by, bpid) in blocks:
                        if abs(bx - x) <= 1 and abs(by - y) <= 1 and (bx, by) != (x, y):
                            k["blockers"][f"{s}|{bx - x},{by - y}|{sname}|{bpid}"] += 1
                    # floor and roof squares just inside the wall
                    sq = (y // 2) * 100 + x // 2
                    for d in (101, 100, 1):
                        q = sq + d
                        if 0 <= q < 10000:
                            fv = m.floors[ei][q] & 0xFFF
                            rv = m.roofs[ei][q] & 0xFFF
                            if fv > 1 and fv < len(tl):
                                k["floors"][f"{sname}|{fv}|{tl[fv]}"] += 1
                            if rv > 1 and rv < len(tl):
                                k["roofs"][f"{sname}|{rv}|{tl[rv]}"] += 1
                for (x, y, pid, art, rot) in doors:
                    for axis, (a, b) in (("x", ((x - 1, y), (x + 1, y))), ("y", ((x, y - 1), (x, y + 1)))):
                        if a in walls and b in walls and walls[a][0] == walls[b][0]:
                            kit[walls[a][0]]["doors"][axis][f"{sname}|{pid}|{art}|{rot}"] += 1
        print(f"{sname}: {n_maps} maps")
    out = {}
    for f, k in kit.items():
        out[f] = {"count": k["count"], "sources": dict(k["sources"]),
                  "sigs": {s: per_source(c, 10) for s, c in k["sigs"].items()},
                  "blockers": per_source(k["blockers"], 40, idx=2),
                  "doors": {a: per_source(c, 8) for a, c in k["doors"].items()},
                  "floors": k["floors"].most_common(8), "roofs": k["roofs"].most_common(6),
                  "next": {e: n for e, n in k["adj"].items() if n >= 2}}
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "kit.json").write_text(json.dumps(out, indent=1), encoding="utf-8")
    print(f"wrote {OUT / 'kit.json'}: {len(out)} families")


# ---------------------------------------------------------------------- draw

_KIT = None


def kit():
    global _KIT
    if _KIT is None:
        _KIT = json.loads((OUT / "kit.json").read_text(encoding="utf-8"))
    return _KIT


def pick(entries, rng):
    tot = sum(n for _, n in entries)
    r = rng.random() * tot
    for e, n in entries:
        r -= n
        if r <= 0:
            return e
    return entries[-1][0]


def fallback_sig(sigs, s):
    """The nearest signature seen (same column parity), same axis first."""
    if s in sigs:
        return s
    par = s[4:] if len(s) > 4 else ""
    s = s[:4]
    same = {k[:4]: k for k in sigs if k[4:] == par}
    if s in same:
        return same[s]
    sub = {k: sigs[same[k]] for k in same}
    if sub:
        return same[_fallback4(sub, s)]
    return _fallback4({k: v for k, v in sigs.items()}, s)


def _fallback4(sigs, s):
    if s in sigs:
        return s
    horiz = s[0] == "W" or s[1] == "E"
    vert = s[2] == "N" or s[3] == "S"
    order = []
    if horiz and not vert:
        order = ["WE..", "W...", ".E.."]
    elif vert and not horiz:
        order = ["..NS", "..N.", "...S"]
    else:
        order = [x for x in ("WEN.", "WE.S", "W.NS", ".ENS", "WENS", "W..S", ".E.S", "W.N.", ".EN.")]
        order += ["WE..", "..NS"]
    for o in order:
        if o in sigs:
            return o
    return max(sigs, key=lambda k: sum(n for _, n in sigs[k]))


def draw(add, convert, fam, cells, elev, rng, doors=(), door_pref=None, source="fo2", style=0):
    """Place family `fam` on every hex in `cells` (a set of (hx, hy)).
    `doors`: hexes in `cells` that get a door instead of a wall piece.
    `add(pid, fid, tile, elev, rot)`, `convert(source, pid) -> (pid, fid) or None`."""
    k = kit()[fam]
    # one game's pieces only: the same family name is different art in different games
    keep = None
    st = k.get("styles", {}).get(source, [])
    if style is not None and st:
        keep = set(st[min(style, len(st) - 1)])
    sigs = {s: [(e, n) for e, n in v if e.startswith(source + "|") and (keep is None or e.split("|")[2] in keep)]
            for s, v in k["sigs"].items()}
    sigs = {s: v for s, v in sigs.items() if v}
    if not sigs:
        raise SystemExit(f"wall family {fam} has no pieces from {source}")
    placed = 0
    door_set = set(doors)
    nxt = defaultdict(Counter)
    for e, n in k.get("next", {}).items():
        src, d, a, b = e.split("|")
        if src == source:
            nxt[(d, a)][b] += n
    chosen = {}
    placed_blk = set()
    for (x, y) in sorted(cells, key=lambda c: (c[1], c[0])):
        if (x, y) in door_set:
            axis = "x" if ((x - 1, y) in cells or (x + 1, y) in cells) else "y"
            cand = [c for c in (k["doors"].get(axis) or []) if c[0].startswith(source + "|")]
            if door_pref:
                cand = [c for c in cand if door_pref in c[0]] or cand
            if cand:
                src, pid, art, rot = pick(cand, rng).split("|")
                r = convert(src, int(pid))
                if r:
                    add(r[0], r[1], y * 200 + x, elev, int(rot))
            continue
        s = sig_of(x, y, cells) + str(x & 1)
        pool = sigs[fallback_sig(sigs, s)]
        left, up = chosen.get((x - 1, y)), chosen.get((x, y - 1))
        if left or up:
            w = []
            for e, n in pool:
                art = e.split("|")[2]
                sc = 1.0
                if left:
                    sc *= nxt[("x", left)].get(art, 0)
                if up:
                    sc *= nxt[("y", up)].get(art, 0)
                if sc > 0:
                    w.append((e, sc))
            if w:
                pool = w
        e = pick(pool, rng)
        chosen[(x, y)] = e.split("|")[2]
        src, pid, art = e.split("|")
        r = convert(src, int(pid))
        if r is None:
            continue
        add(r[0], r[1], y * 200 + x, elev, 0)
        placed += 1
        # blockers seen beside this signature
        for b, n in k["blockers"]:
            bs, off, bsrc, bpid = b.split("|")
            if bs != s or n < 3 or bsrc != source:
                continue
            dx, dy = map(int, off.split(","))
            rb = convert(bsrc, int(bpid))
            bx, by = x + dx, y + dy
            if any(abs(bx - ddx) <= 2 and abs(by - ddy) <= 2 for ddx, ddy in door_set):
                continue
            if (bx, by) in placed_blk:
                continue
            placed_blk.add((bx, by))
            if rb and (x + dx, y + dy) not in cells:
                add(rb[0], rb[1], (y + dy) * 200 + x + dx, elev, 0)
    return placed


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("learn")
    s = sub.add_parser("show")
    s.add_argument("family")
    f = sub.add_parser("families")
    f.add_argument("--min", type=int, default=300)
    args = ap.parse_args()
    if args.cmd == "learn":
        learn(SOURCES)
    elif args.cmd == "show":
        k = kit()[args.family]
        print(json.dumps({key: k[key] for key in ("count", "sources", "doors", "floors", "roofs")}, indent=1))
        for sg, v in sorted(k["sigs"].items(), key=lambda kv: -sum(n for _, n in kv[1])):
            print(sg, sum(n for _, n in v), [e.split("|")[2] for e, _ in v[:6]])
    else:
        for fam, k in sorted(kit().items(), key=lambda kv: -kv[1]["count"]):
            if k["count"] >= args.min:
                full = all(any(s.startswith(p) for s in k["sigs"]) for p in ("WE", ".."))
                print(f"{fam:10} {k['count']:6}  sigs {len(k['sigs']):2}  doors {sum(len(v) for v in k['doors'].values()):2}  {dict(k['sources'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
