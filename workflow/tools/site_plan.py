#!/usr/bin/env python3
"""
site_plan.py - draw a map's site plan to scale, check it, and (later) build
from it, so a map is laid out on paper before anything is placed.

    python tools/site_plan.py mod/maps_src/plans/webb.plan.json
        -> docs/plans/webb.png, and the checks printed

A plan is JSON, in SQUARES (the 100 x 100 floor grid; a square is 2 x 2 hexes):

  name, title, elev (default 0)
  play_area   [x0, y0, x1, y1]     what the player can walk and see
  min_gap     squares between buildings (default 3; the Fallout towns' median is 4)
  zones       [{name, rect, color}]               districts, drawn as tinted areas
  roads       [{name, rect, kind}]                kind: road | runway | apron | yard | path
  buildings   [{id, name, stamp, at, purpose, rooms, shell}]
                stamp: a file in the Desktop 'Gecko Stamps' library; its walls
                give the true footprint and its doors are drawn. `at` is the
                footprint's top square. Or "size": [w, h] for a building with
                no stamp yet.
  features    [{name, rect or at}]                planes, wreckage, fires, fences
  exits       [{name, rect}]
  start       [x, y]
  routes      [{name, points: [[x, y], ...]}]     how the player is meant to walk
  underlay    {source, map, elev}                 an existing level to plan on top of:
                its walls and floor are drawn grey under the plan

The drawing uses the game's own projection (render.py), so it reads like the
map will on screen: the top of the drawing is the top of the screen.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))

ROOT = Path(__file__).resolve().parent.parent
from foclib.paths import inside  # noqa: E402
from foclib import config as _config  # noqa: E402
STAMPS = _config.stamps_dir()
OUT = ROOT / "docs" / "plans"

ROAD_COL = {"road": (92, 92, 96), "runway": (58, 58, 62), "apron": (110, 110, 114),
            "yard": (150, 132, 100), "path": (170, 150, 110)}
BLD_COL = (222, 196, 150)
BLD_EDGE = (60, 40, 20)


# ------------------------------------------------------------------ geometry

def proj(x: float, y: float) -> tuple[float, float]:
    """Continuous square coordinates -> screen pixels (render.square_xy)."""
    v5 = 99 - x
    return 48 * v5 + 32 * y, -12 * v5 + 24 * y


def rect_poly(r, s=1.0):
    x0, y0, x1, y1 = r
    pts = [(x0, y0), (x1 + 1, y0), (x1 + 1, y1 + 1), (x0, y1 + 1)]
    return [tuple(c * s for c in proj(*p)) for p in pts]


def rect_gap(a, b) -> int:
    """Squares of clear ground between two rects (0 = touching or overlapping)."""
    dx = max(b[0] - a[2] - 1, a[0] - b[2] - 1, 0)
    dy = max(b[1] - a[3] - 1, a[1] - b[3] - 1, 0)
    return max(dx, dy)


def overlaps(a, b) -> bool:
    return not (a[2] < b[0] or b[2] < a[0] or a[3] < b[1] or b[3] < a[1])


# -------------------------------------------------------------------- stamps

def stamp_footprint(path: str):
    """A stamp's wall squares and door squares, relative to its own origin
    with the hex x kept even (an odd x shift would break the hex walls)."""
    d = json.loads((STAMPS / path).read_text(encoding="utf-8"))
    v = d["variants"][0]
    from foclib import GameIndex
    gi = stamp_footprint.gi = getattr(stamp_footprint, "gi", None) or GameIndex(pristine=False)
    walls, doors, allsq = set(), set(), set()
    for o in v["objects"]:
        pid = o["proPid"]
        t = pid >> 24
        sq = (o["dxHex"] // 2, o["dyHex"] // 2)
        if t == 3:
            walls.add(sq)
        elif t == 2:
            pr = gi.proto(pid)
            if pr is not None and pr.subtype == 0:
                doors.add(sq)
        if t in (2, 3):
            allsq.add(sq)
    base = walls or allsq
    mx, my = min(s[0] for s in base), min(s[1] for s in base)
    Mx, My = max(s[0] for s in base), max(s[1] for s in base)
    return (mx, my, Mx, My), doors


def place_building(b):
    """Footprint rect in map squares, door squares, and the stamp's `at` hex."""
    if b.get("stamp"):
        (mx, my, Mx, My), doors = stamp_footprint(b["stamp"])
        sx, sy = b["at"]
        ox, oy = sx - mx, sy - my               # square offset of the stamp origin
        rect = [sx, sy, sx + (Mx - mx), sy + (My - my)]
        b["_at_hex"] = [2 * ox, 2 * oy]
        b["_doors"] = [(ox + x, oy + y) for x, y in doors]
    else:
        w, h = b["size"]
        sx, sy = b["at"]
        rect = [sx, sy, sx + w - 1, sy + h - 1]
        b["_doors"] = [tuple(d) for d in b.get("doors", [])]
    b["_rect"] = rect
    return rect


# ------------------------------------------------------------------ underlay

class _FocSource:
    """FOC's own deployed maps (base/mods/foc/maps) as an underlay source."""

    def __init__(self):
        from foclib import GameIndex
        self.gi = GameIndex(pristine=False)
        self.vfs = self
        self.proto = self.gi.proto

    def read(self, path):
        p = path.replace("\\", "/")
        if p.lower().startswith("maps/"):
            d = ROOT / "base" / "mods" / "foc" / "maps"
            hit = next((q for q in d.glob("*") if q.name.lower() == p[5:].lower()), None)
            if hit:
                return hit.read_bytes()
        return self.gi.vfs.read(path)


def open_src(name):
    import port
    return _FocSource() if name == "foc" else port.open_source(name)


def underlay_squares(u):
    from foclib import MapFile
    src = open_src(u["source"])
    name = u["map"] if u["map"].lower().endswith(".map") else u["map"] + ".map"
    m = MapFile.parse(src.vfs.read(f"maps\\{name}"), src.proto)
    ei = m.elevations.index(u.get("elev", 0))
    from collections import Counter
    cnt = Counter(v & 0xFFF for v in m.floors[ei] if (v & 0xFFF) > 1)
    filler = cnt.most_common(1)[0][0] if cnt and cnt.most_common(1)[0][1] > 0.4 * sum(cnt.values()) else None
    from foclib import parse_lst
    tl = parse_lst(src.vfs.read("art\\tiles\\tiles.lst"))
    PAVE = ("road", "curb", "cmt", "swlk", "side", "asph", "conc", "street", "park")
    INSIDE = ("brick", "tp", "flr", "floor", "tile", "wood", "vlt", "vault", "carpt", "mtl", "metal", "bfl")

    def kind(v):
        n = tl[v].lower() if v < len(tl) else ""
        if n.startswith(PAVE):
            return "pave"
        if n.startswith(INSIDE):
            return "inside"
        return "ground"
    floor = {(k % 100, k // 100): kind(m.floors[ei][k] & 0xFFF) for k in range(10000)
             if (m.floors[ei][k] & 0xFFF) > 1 and (m.floors[ei][k] & 0xFFF) != filler}
    walls = set()
    for o in m.objects:
        if o.elevation == u.get("elev", 0) and (o.pid >> 24) == 3 and 0 <= o.tile < 40000:
            walls.add(((o.tile % 200) // 2, (o.tile // 200) // 2))
    sx, sy = u.get("shift", [0, 0])
    return {(x + sx, y + sy): k for (x, y), k in floor.items()}, {(x + sx, y + sy) for x, y in walls}


def detect_rooms(u, min_hexes=24):
    """The underlay's rooms, the way find_rooms.py finds them: connected
    walkable hexes between walls, blocking scenery and doors, the level's
    background filler not counting as floor. Largest first; each room is
    returned as its set of squares."""
    from collections import Counter, deque
    from foclib import MapFile
    W = 200
    dirs = {0: (-1, W - 1, W, W + 1, 1, -W), 1: (-W - 1, -1, W, 1, 1 - W, -W)}
    src = open_src(u["source"])
    name = u["map"] if u["map"].lower().endswith(".map") else u["map"] + ".map"
    m = MapFile.parse(src.vfs.read(f"maps\\{name}"), src.proto)
    el = u.get("elev", 0)
    floors = m.floors[m.elevations.index(el)]
    cnt = Counter(v & 0xFFF for v in floors if (v & 0xFFF) > 1)
    filler = None
    if cnt:
        tid, c = cnt.most_common(1)[0]
        if c > 0.4 * sum(cnt.values()):
            filler = tid
    blocked = set()
    for o in m.objects:
        if o.elevation != el or not (0 <= o.tile < 40000):
            continue
        t = o.pid >> 24
        pr = src.proto(o.pid)
        if t == 3 or (t == 2 and pr is not None and (pr.subtype == 0 or not (pr.flags & 0x10))):
            blocked.add(o.tile)

    def ok(t):
        v = floors[(t // W // 2) * 100 + (t % W) // 2] & 0xFFF
        return v > 1 and v != filler
    seen, rooms = set(), []
    for start in range(40000):
        if start in seen or start in blocked or not ok(start):
            continue
        comp, q = [], deque([start])
        seen.add(start)
        while q:
            t = q.popleft()
            comp.append(t)
            for d in dirs[(t % W) & 1]:
                n = t + d
                if 0 <= n < 40000 and abs(n % W - t % W) <= 1 and n not in seen and n not in blocked and ok(n):
                    seen.add(n)
                    q.append(n)
        if len(comp) >= min_hexes:
            rooms.append(comp)
    rooms.sort(key=lambda c: -len(c))
    sx, sy = u.get("shift", [0, 0])
    return [{((t % W) // 2 + sx, (t // W) // 2 + sy) for t in comp} for comp in rooms]


# ------------------------------------------------------------------- drawing

def font(n):
    for f in ("C:/Windows/Fonts/arialbd.ttf", "C:/Windows/Fonts/arial.ttf"):
        try:
            return ImageFont.truetype(f, n)
        except OSError:
            pass
    return ImageFont.load_default()


def draw_plan(plan: dict, out: Path) -> list[str]:
    S = 0.5                                     # half the game's pixel scale
    pa = plan["play_area"]
    view = plan.get("view", [pa[0] - 6, pa[1] - 6, pa[2] + 6, pa[3] + 6])
    # The floor grid is 100 x 100 squares; keeping the view near it also bounds the loops below.
    view = [min(max(int(v), -20), 120) for v in view]
    corners = [proj(x, y) for x in (view[0], view[2] + 1) for y in (view[1], view[3] + 1)]
    minx = min(c[0] for c in corners) * S
    miny = min(c[1] for c in corners) * S
    maxx = max(c[0] for c in corners) * S
    maxy = max(c[1] for c in corners) * S
    pad, legend_w = 40, 560
    W = int(maxx - minx + 2 * pad + legend_w)
    H = int(max(maxy - miny + 2 * pad + 60, 900))
    im = Image.new("RGB", (W, H), (236, 230, 214))
    dr = ImageDraw.Draw(im, "RGBA")

    def P(pts):
        return [(x - minx + pad, y - miny + pad + 50) for x, y in pts]

    def R(rect):
        return P(rect_poly(rect, S))

    def C(x, y):
        px, py = proj(x, y)
        return px * S - minx + pad, py * S - miny + pad + 50

    f_t, f_l, f_s, f_xs = font(26), font(15), font(13), font(11)
    dr.text((pad, 12), plan.get("title", plan["name"]), fill=(30, 20, 10), font=f_t)

    # ground of the view, grid
    dr.polygon(R(view), fill=(214, 200, 168))
    # underlay: an existing level's floor and walls
    if plan.get("underlay"):
        floor, walls = underlay_squares(plan["underlay"])
        shade = {"pave": (150, 150, 152), "inside": (226, 210, 170), "ground": (196, 186, 160)}
        for (x, y), k in floor.items():
            dr.polygon(R([x, y, x, y]), fill=shade[k])
        for (x, y) in walls:
            dr.polygon(R([x, y, x, y]), fill=(90, 80, 70))

    for x in range(view[0], view[2] + 2):
        c = (150, 140, 116) if x % 10 else (110, 100, 80)
        dr.line([C(x, view[1]), C(x, view[3] + 1)], fill=c, width=1)
    for y in range(view[1], view[3] + 2):
        c = (150, 140, 116) if y % 10 else (110, 100, 80)
        dr.line([C(view[0], y), C(view[2] + 1, y)], fill=c, width=1)
    for x in range(view[0] - view[0] % 10 + 10, view[2] + 1, 10):
        dr.text(C(x, view[1] - 1.5), str(x), fill=(80, 70, 50), font=f_xs)
    for y in range(view[1] - view[1] % 10 + 10, view[3] + 1, 10):
        dr.text(C(view[2] + 1.5, y), str(y), fill=(80, 70, 50), font=f_xs)

    room_list = []
    if plan.get("underlay") and plan.get("rooms") is not None:
        rooms = detect_rooms(plan["underlay"], plan.get("room_min", 24))
        names = plan["rooms"]
        palette = [(80, 120, 200), (200, 120, 60), (60, 160, 90), (170, 70, 160), (200, 180, 40), (60, 170, 180),
                   (190, 70, 70), (120, 100, 200), (110, 150, 60), (160, 110, 80)]
        for ri, sqs in enumerate(rooms, 1):
            info = names.get(str(ri))
            col = palette[(ri - 1) % len(palette)] if info else (170, 170, 170)
            for (x, y) in sqs:
                dr.polygon(R([x, y, x, y]), fill=col + (110 if info else 50,))
            cx = sum(q[0] for q in sqs) / len(sqs)
            cy = sum(q[1] for q in sqs) / len(sqs)
            px, py = C(cx + 0.5, cy + 0.5)
            dr.ellipse((px - 12, py - 12, px + 12, py + 12), fill=(30, 20, 10) if info else (120, 120, 120))
            dr.text((px, py), str(ri), fill=(255, 240, 200), font=f_s, anchor="mm")
            room_list.append((ri, info, col))
    for zi, z in enumerate(plan.get("zones", [])):
        col = tuple(int(z.get("color", "#8080ff")[i:i + 2], 16) for i in (1, 3, 5))
        dr.polygon(R(z["rect"]), fill=col + (60,), outline=col + (220,), width=2)
        x0, y0, x1, y1 = z["rect"]
        cx, cy = C((x0 + x1 + 1) / 2, (y0 + y1 + 1) / 2)
        letter = chr(ord("A") + zi)
        dr.rectangle((cx - 12, cy - 12, cx + 12, cy + 12), fill=col + (235,), outline=(30, 20, 10))
        dr.text((cx, cy), letter, fill=(255, 255, 255), font=f_l, anchor="mm")

    dr.polygon(R(pa), outline=(40, 120, 40), width=3)

    for rd in plan.get("roads", []):
        dr.polygon(R(rd["rect"]), fill=ROAD_COL.get(rd.get("kind", "road")))
        if rd.get("label", True) and rd.get("name"):
            x0, y0, x1, y1 = rd["rect"]
            dr.text(C((x0 + x1) / 2, (y0 + y1) / 2), rd["name"], fill=(245, 245, 245), font=f_s, anchor="mm")

    for ft in plan.get("features", []):
        r = ft.get("rect") or [ft["at"][0], ft["at"][1], ft["at"][0], ft["at"][1]]
        dr.polygon(R(r), fill=(120, 90, 60, 170), outline=(60, 40, 20))
        x0, y0, x1, y1 = r
        dr.text(C((x0 + x1 + 1) / 2, (y0 + y1 + 1) / 2), ft["name"], fill=(30, 20, 10), font=f_xs, anchor="mm")

    for ex in plan.get("exits", []):
        dr.polygon(R(ex["rect"]), fill=(60, 170, 60, 170), outline=(20, 90, 20))
        x0, y0, x1, y1 = ex["rect"]
        dr.text(C((x0 + x1 + 1) / 2, (y0 + y1 + 1) / 2), ex.get("name", "exit"), fill=(10, 50, 10), font=f_s, anchor="mm")

    for i, b in enumerate(plan.get("buildings", []), 1):
        r = b["_rect"]
        dr.polygon(R(r), fill=BLD_COL, outline=BLD_EDGE, width=2)
        for (x, y) in b["_doors"]:
            dr.polygon(R([x, y, x, y]), fill=(200, 40, 30))
        for (x, y) in b.get("new_doors", []):           # doorways to be knocked through
            dr.polygon(R([x, y, x, y]), fill=(240, 140, 20))
        x0, y0, x1, y1 = r
        cx, cy = C((x0 + x1 + 1) / 2, (y0 + y1 + 1) / 2)
        dr.ellipse((cx - 13, cy - 13, cx + 13, cy + 13), fill=(40, 30, 20))
        dr.text((cx, cy), str(i), fill=(255, 240, 200), font=f_l, anchor="mm")

    # gaps between neighbouring buildings
    bl = plan.get("buildings", [])
    for i in range(len(bl)):
        for j in range(i + 1, len(bl)):
            a, b = bl[i]["_rect"], bl[j]["_rect"]
            g = rect_gap(a, b)
            if g <= 8:
                ax, ay = (a[0] + a[2] + 1) / 2, (a[1] + a[3] + 1) / 2
                bx, by = (b[0] + b[2] + 1) / 2, (b[1] + b[3] + 1) / 2
                p, q = C(ax, ay), C(bx, by)
                m = ((p[0] + q[0]) / 2, (p[1] + q[1]) / 2)
                col = (180, 30, 30) if g < plan.get("min_gap", 3) else (40, 40, 160)
                dr.text(m, f"{g}", fill=col, font=f_s, anchor="mm")

    for rt in plan.get("routes", []):
        pts = [C(x + 0.5, y + 0.5) for x, y in rt["points"]]
        for a, b in zip(pts, pts[1:]):
            dr.line([a, b], fill=(30, 60, 200, 220), width=4)
        if len(pts) >= 2:
            ax, ay = pts[-2]
            bx, by = pts[-1]
            import math
            ang = math.atan2(by - ay, bx - ax)
            for da in (2.6, -2.6):
                dr.line([(bx, by), (bx + 14 * math.cos(ang + da), by + 14 * math.sin(ang + da))],
                        fill=(30, 60, 200), width=4)

    if plan.get("start"):
        sx, sy = plan["start"]
        cx, cy = C(sx + 0.5, sy + 0.5)
        dr.regular_polygon((cx, cy, 12), 5, fill=(240, 200, 20), outline=(60, 40, 0))
        dr.text((cx + 16, cy), "start", fill=(60, 40, 0), font=f_s, anchor="lm")

    # vignettes (docs/MAP_STORYTELLING_GUIDE.md): numbered markers
    for si, st in enumerate(plan.get("stories", []), 1):
        sx, sy = st["at"]
        cx, cy = C(sx + 0.5, sy + 0.5)
        dr.regular_polygon((cx, cy, 13), 4, rotation=45, fill=(150, 20, 110), outline=(255, 255, 255))
        dr.text((cx, cy), f"S{si}", fill=(255, 255, 255), font=f_xs, anchor="mm")

    # legend
    lx = int(maxx - minx + 2 * pad) + 10
    y = 60
    if plan.get("kind"):
        for line in wrap("Kind of place: " + plan["kind"], 66):
            dr.text((lx, y - 40), line, fill=(110, 30, 80), font=f_s)
            y += 16
        for line in wrap("Materials: " + plan.get("materials", ""), 70) if plan.get("materials") else []:
            dr.text((lx, y - 40), line, fill=(60, 50, 40), font=f_s)
            y += 16
        y += 10
    stl = plan.get("stories", [])
    if stl:
        dr.text((lx, y - 40), "Stories on the map (vignettes)", fill=(30, 20, 10), font=f_l)
        y -= 18
        for si, st in enumerate(stl, 1):
            for k, line in enumerate(wrap(f"S{si}. {st['name']}: {st['what']}", 66)):
                dr.text((lx, y + 2), line, fill=(110, 30, 80) if k == 0 else (60, 50, 40), font=f_s)
                y += 16
            y += 3
        y += 50
    zl = plan.get("zones", [])
    if zl:
        dr.text((lx, y - 40), "Zones and rooms", fill=(30, 20, 10), font=f_l)
        y -= 18
        for zi, z in enumerate(zl):
            col = tuple(int(z.get("color", "#8080ff")[i:i + 2], 16) for i in (1, 3, 5))
            dr.rectangle((lx, y + 2, lx + 16, y + 16), fill=col)
            dr.text((lx + 4, y + 2), chr(ord("A") + zi), fill=(255, 255, 255), font=f_s)
            for k, line in enumerate(wrap(z["name"], 64)):
                dr.text((lx + 24, y + 2), line, fill=(30, 20, 10), font=f_s)
                y += 16
            y += 4
        y += 14
    if room_list:
        dr.text((lx, y), "Rooms (numbered as found on the level)", fill=(30, 20, 10), font=f_l)
        y += 22
        for ri, info, col in room_list:
            if not info:
                continue
            dr.rectangle((lx, y + 2, lx + 16, y + 16), fill=col)
            for line in wrap(f"{ri}. {info}", 66):
                dr.text((lx + 24, y + 2), line, fill=(30, 20, 10), font=f_s)
                y += 16
            y += 4
        rest = [str(ri) for ri, info, _ in room_list if not info]
        if rest:
            for line in wrap("unassigned (grey): " + ", ".join(rest), 70):
                dr.text((lx, y), line, fill=(90, 90, 90), font=f_s)
                y += 16
        y += 14
    if bl:
        dr.text((lx, y), "Buildings (size in squares)", fill=(30, 20, 10), font=f_l)
        y += 24
    for i, b in enumerate(bl, 1):
        r = b["_rect"]
        head = f"{i}. {b['name']}  ({r[2] - r[0] + 1} x {r[3] - r[1] + 1})"
        dr.text((lx, y), head, fill=(30, 20, 10), font=f_l)
        y += 20
        for line in wrap(b.get("purpose", ""), 64):
            dr.text((lx + 18, y), line, fill=(60, 50, 40), font=f_s)
            y += 16
        for room in b.get("rooms", []):
            for k, line in enumerate(wrap(room, 60)):
                dr.text((lx + 18, y), ("- " if k == 0 else "  ") + line, fill=(70, 60, 50), font=f_s)
                y += 16
        y += 8
    y += 10
    dr.text((lx, y), "Key", fill=(30, 20, 10), font=f_l)
    y += 22
    for label, col in (("building (red = door, orange = new doorway)", BLD_COL), ("road", ROAD_COL["road"]), ("runway", ROAD_COL["runway"]),
                       ("exit grid", (60, 170, 60)), ("walking route", (30, 60, 200))):
        dr.rectangle((lx, y, lx + 22, y + 12), fill=col, outline=(40, 30, 20))
        dr.text((lx + 30, y - 2), label, fill=(30, 20, 10), font=f_s)
        y += 18
    dr.text((lx, y + 4), "green outline = play area;  numbers between buildings = gap in squares", fill=(30, 20, 10), font=f_s)
    dr.text((lx, y + 22), "grid: 1 square = 2 hexes; heavy lines every 10 squares", fill=(30, 20, 10), font=f_s)

    if y + 60 > H:
        im2 = Image.new("RGB", (W, y + 80), (236, 230, 214))
        im2.paste(im, (0, 0))
        im = im2
    out.parent.mkdir(parents=True, exist_ok=True)
    im.save(out)
    return []


def wrap(text: str, n: int) -> list[str]:
    words, lines, cur = text.split(), [], ""
    for w in words:
        if len(cur) + len(w) + 1 > n:
            lines.append(cur)
            cur = w
        else:
            cur = (cur + " " + w).strip()
    if cur:
        lines.append(cur)
    return lines


# -------------------------------------------------------------------- checks

def check(plan: dict) -> list[str]:
    msgs = []
    pa = plan["play_area"]
    w, h = pa[2] - pa[0] + 1, pa[3] - pa[1] + 1
    msgs.append(f"play area {w} x {h} squares (Fallout towns: about 39 x 48)")
    bl = plan.get("buildings", [])
    gap = plan.get("min_gap", 3)
    # Olympus assets are the owner's alone (memory: foc-no-olympus-tiles)
    for b in bl:
        if "olympus" in (b.get("stamp") or "").lower():
            msgs.append(f"PROBLEM {b['name']} uses an Olympus stamp; Olympus assets are the owner's only")
    if (plan.get("underlay") or {}).get("source") == "olympus":
        msgs.append("PROBLEM the underlay is an Olympus map; Olympus assets are the owner's only")
    for i in range(len(bl)):
        a = bl[i]["_rect"]
        if not (pa[0] <= a[0] and a[2] <= pa[2] and pa[1] <= a[1] and a[3] <= pa[3]):
            msgs.append(f"PROBLEM {bl[i]['name']} is not inside the play area")
        for j in range(i + 1, len(bl)):
            b = bl[j]["_rect"]
            if overlaps(a, b):
                msgs.append(f"PROBLEM {bl[i]['name']} overlaps {bl[j]['name']}")
            elif rect_gap(a, b) < gap and not plan.get("allow_close", {}).get(f"{bl[i]['id']}+{bl[j]['id']}"):
                msgs.append(f"PROBLEM {bl[i]['name']} and {bl[j]['name']} are {rect_gap(a, b)} squares apart (min {gap})")
        for rd in plan.get("roads", []):
            if overlaps(a, rd["rect"]) and rd.get("kind") not in ("yard",):
                msgs.append(f"PROBLEM {bl[i]['name']} sits on {rd.get('name', 'a road')}")
        near = min((rect_gap(a, rd["rect"]) for rd in plan.get("roads", [])), default=99)
        if near > 4:
            msgs.append(f"note: {bl[i]['name']} is {near} squares from the nearest road or yard")
    if plan.get("start") and plan.get("exits"):
        sx, sy = plan["start"]
        d = min(rect_gap([sx, sy, sx, sy], e["rect"]) for e in plan["exits"])
        msgs.append(f"start is {d} squares from the nearest exit (towns: 10-15)")
    return msgs


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("plan")
    args = ap.parse_args()
    p = inside(args.plan)
    plan = json.loads(p.read_text(encoding="utf-8"))
    for b in plan.get("buildings", []):
        place_building(b)
    out = OUT / (p.name.split(".")[0] + ".png")
    draw_plan(plan, out)
    for m in check(plan):
        print(m)
    print(f"drew {out.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
