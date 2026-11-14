#!/usr/bin/env python3
"""
render.py - draw a .map to a PNG, so a map can be looked at without launching
the game.

    python tools/render.py mod/data/maps/FOCTEST.MAP -o build/foctest.png
    python tools/render.py --art art/scenery/well1.frm -o build/well.png
    python tools/render.py --sheet art/walls/tent -o build/tents.png

WHY THIS EXISTS
---------------
Every map bug this project has hit cost a play-test cycle to find, because the
only way to see a map was to ask a human to launch the game and describe it.
A black floor took three rounds of that. Rendering locally turns a
twenty-minute round trip into a second, and would have caught the elevation
mistake instantly - the tiles simply would not have appeared.

It also answers "what does this art actually look like", which name-searching
cannot: `--sheet` contact-prints every FRM matching a prefix.

GEOMETRY, from tile.cc
----------------------
Both grids mirror x, and squares step twice as far as hexes:

    square: v5 = 99 - (sq % 100); v6 = sq // 100
            x = 48*v5 + 32*v6 ;  y = -12*v5 + 24*v6

    hex:    v3 = 199 - (t % 200); v4 = t // 200
            x = 48*(v3//2) + 16*v4 ;  y = 12*(-(v3//2)) + 12*v4
            plus, when v3 is odd, a half-step of (+32, 0) or (-16, +12)

This is a flat draw with no lighting: the goal is to see what is placed and
where, not to reproduce the engine's output.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from PIL import Image  # noqa: E402

from foclib.paths import inside  # noqa: E402
from foclib import (  # noqa: E402
    OBJ_TYPE_CRITTER,
    OBJ_TYPE_SCENERY,
    OBJ_TYPE_WALL,
    GameIndex,
    MapFile,
    TYPE_NAME,
    parse_lst,
    pid_type,
)

ROOT = Path(__file__).resolve().parent.parent

TILE_W, TILE_H = 80, 36


# --------------------------------------------------------------------- palette

def load_palette(gi: GameIndex) -> list[tuple[int, int, int]]:
    """
    color.pal is 256 RGB triples of 6-bit values, then lookup tables we ignore.
    Index 0 is transparent.
    """
    raw = gi.vfs.read("color.pal")
    pal = []
    for i in range(256):
        r, g, b = raw[i * 3], raw[i * 3 + 1], raw[i * 3 + 2]
        # 6-bit -> 8-bit; *4 alone leaves the top end at 252
        pal.append(((r << 2) | (r >> 4), (g << 2) | (g >> 4), (b << 2) | (b >> 4)))
    return pal


# ------------------------------------------------------------------------- FRM

class Frm:
    """
    One FRM frame, decoded to an RGBA image.

    Header is big-endian:
        u32 version, u16 fps, u16 actionFrame, u16 framesPerDirection,
        i16 shiftX[6], i16 shiftY[6], u32 frameOffset[6], u32 frameAreaSize
    then frames start at 0x3E. Each frame:
        u16 width, u16 height, u32 size, i16 offsetX, i16 offsetY, u8 data[size]

    Verified empirically: tile FRMs decode to exactly 80x36, which is Fallout's
    tile size, and every frame's size field equals width*height.
    """

    __slots__ = ("width", "height", "pixels", "x_offsets", "y_offsets")

    def __init__(self, width, height, pixels, x_offsets=None, y_offsets=None):
        self.width = width
        self.height = height
        self.pixels = pixels
        # Six (x, y) pairs from the FRM HEADER at 0x0A / 0x16, indexed by
        # ROTATION. object.cc::_obj_render_object does:
        #     x = tileScreenX + 16 + art->xOffsets[rotation]
        #     y = tileScreenY +  8 + art->yOffsets[rotation]
        #     left = x - width/2 ;  top = y - (height - 1)
        # These are what make walls sit flush. Not the per-frame shift.
        self.x_offsets = x_offsets or [0] * 6
        self.y_offsets = y_offsets or [0] * 6

    @classmethod
    def parse(cls, data: bytes, direction: int = 0) -> "Frm":
        if len(data) < 0x3E:
            raise ValueError("too short for an FRM header")
        xo = list(struct.unpack_from(">6h", data, 0x0A))
        yo = list(struct.unpack_from(">6h", data, 0x16))
        offsets = struct.unpack_from(">6I", data, 0x22)
        base = 0x3E + offsets[direction if direction < 6 else 0]
        if base + 12 > len(data):
            base = 0x3E
        w, h, size, _fx, _fy = struct.unpack_from(">HHIhh", data, base)
        px = data[base + 12: base + 12 + w * h]
        if len(px) < w * h:
            raise ValueError(f"frame truncated: {len(px)} < {w*h}")
        return cls(w, h, px, xo, yo)

    def to_image(self, palette) -> Image.Image:
        img = Image.new("RGBA", (self.width, self.height))
        out = bytearray(self.width * self.height * 4)
        for i, idx in enumerate(self.pixels):
            if idx == 0:                      # index 0 is transparent
                continue
            r, g, b = palette[idx]
            o = i * 4
            out[o] = r
            out[o + 1] = g
            out[o + 2] = b
            out[o + 3] = 255
        img.frombytes(bytes(out))
        return img


class ArtCache:
    def __init__(self, gi: GameIndex, palette):
        self.gi = gi
        self.palette = palette
        self._imgs: dict[str, Image.Image | None] = {}
        self._lists: dict[str, list[str]] = {}

    def art_list(self, d: str) -> list[str]:
        if d not in self._lists:
            try:
                self._lists[d] = parse_lst(self.gi.vfs.read(f"art/{d}/{d}.lst"))
            except FileNotFoundError:
                self._lists[d] = []
        return self._lists[d]

    def by_path(self, path: str, rotation: int = 0):
        """(image, offset_x, offset_y) for this rotation."""
        key = (path, rotation)
        if key in self._imgs:
            return self._imgs[key]
        result = (None, 0, 0)
        try:
            frm = Frm.parse(self.gi.vfs.read(path), rotation)
            result = (frm.to_image(self.palette),
                      frm.x_offsets[rotation % 6], frm.y_offsets[rotation % 6])
        except Exception:
            result = (None, 0, 0)
        self._imgs[key] = result
        return result

    def tile(self, tile_id: int) -> Image.Image | None:
        lst = self.art_list("tiles")
        i = tile_id          # art lists are 0-based; see CLAUDE.md 4f
        if not (0 <= i < len(lst)) or not lst[i]:
            return None
        return self.by_path(f"art/tiles/{lst[i]}")[0]

    def obj(self, obj_type: int, fid: int, rotation: int = 0):
        d = TYPE_NAME.get(obj_type)
        if d is None:
            return (None, 0, 0)
        lst = self.art_list(d)
        i = fid & 0xFFF
        if not (0 <= i < len(lst)) or not lst[i]:
            return (None, 0, 0)
        name = lst[i].split(",")[0]          # critters.lst rows are "name,fps,..."
        if obj_type == OBJ_TYPE_CRITTER and "." not in name:
            # Critter art is <6-char base><2-char animation code>. "aa" is the
            # idle stand, which is what a map object should show. Some anims
            # only exist as per-direction .fr0-.fr5, so try both.
            for suffix in ("aa.frm", "aa.fr0", "ab.frm", ".frm"):
                cand = f"art/{d}/{name}{suffix}"
                if self.gi.vfs.exists(cand):
                    return self.by_path(cand, rotation)
            return (None, 0, 0)
        return self.by_path(f"art/{d}/{name}", rotation)


# ---------------------------------------------------------------------- screen

def square_xy(sq: int) -> tuple[int, int]:
    v5 = 99 - (sq % 100)
    v6 = sq // 100
    return 48 * v5 + 32 * v6, -12 * v5 + 24 * v6


def hex_xy_screen(tile: int) -> tuple[int, int]:
    v3 = 199 - (tile % 200)
    v4 = tile // 200
    x = 48 * (v3 // 2) + 16 * v4
    y = 12 * -(v3 // 2) + 12 * v4
    if v3 & 1:
        x += 32
    return x, y


# ---------------------------------------------------------------------- render

def render_map(gi: GameIndex, m: MapFile, elevation_index: int = 0,
               draw_objects: bool = True, roofs: bool = False) -> Image.Image:
    palette = load_palette(gi)
    art = ArtCache(gi, palette)

    floors = m.floors[elevation_index]
    elev = m.elevations[elevation_index]

    # bounds from everything we intend to draw
    pts = []
    for sq, tid in enumerate(floors):
        if tid > 1:
            x, y = square_xy(sq)
            pts.append((x, y, x + TILE_W, y + TILE_H))
    objs = [o for o in m.objects if o.elevation == elev and 0 <= o.tile < 40000]
    for o in objs:
        x, y = hex_xy_screen(o.tile)
        pts.append((x - 100, y - 200, x + 100, y + 60))
    if not pts:
        raise SystemExit("nothing to draw on this elevation")

    minx = min(p[0] for p in pts)
    miny = min(p[1] for p in pts)
    maxx = max(p[2] for p in pts)
    maxy = max(p[3] for p in pts)
    W, H = maxx - minx + 8, maxy - miny + 8
    canvas = Image.new("RGBA", (W, H), (10, 10, 12, 255))

    drawn_tiles = 0
    for sq, tid in enumerate(floors):
        if tid <= 1:
            continue
        img = art.tile(tid)
        if img is None:
            continue
        x, y = square_xy(sq)
        canvas.alpha_composite(img, (x - minx, y - miny))
        drawn_tiles += 1

    drawn_objs = 0
    if draw_objects:
        # painter's order: back to front by screen y, then x
        def key(o):
            x, y = hex_xy_screen(o.tile)
            return (y, x)
        for o in sorted(objs, key=key):
            img, ox, oy = art.obj(pid_type(o.pid), o.fid, o.rotation)
            if img is None:
                continue
            x, y = hex_xy_screen(o.tile)
            # object.cc::_obj_render_object, exactly
            sx = x + 16 + ox
            sy = y + 8 + oy
            canvas.alpha_composite(
                img,
                (sx - img.width // 2 - minx, sy - (img.height - 1) - miny))
            drawn_objs += 1

    print(f"  rendered elevation {elev}: {drawn_tiles} tiles, {drawn_objs} objects, "
          f"{W}x{H}px")
    return canvas


def render_sheet(gi: GameIndex, prefix: str, limit: int = 120) -> Image.Image:
    """Contact sheet of every FRM whose path starts with `prefix`."""
    palette = load_palette(gi)
    art = ArtCache(gi, palette)
    d = prefix.replace("\\", "/").split("/")[1] if "/" in prefix else "scenery"
    lst = art.art_list(d)
    want = prefix.replace("\\", "/").split("/")[-1].lower()
    names = [n for n in lst if n and n.lower().startswith(want)][:limit]
    if not names:
        sys.exit(f"no art under {prefix}")

    imgs = []
    for n in names:
        img = art.by_path(f"art/{d}/{n}")[0]
        if img is not None:
            imgs.append((n, img))
    if not imgs:
        sys.exit("nothing decodable")

    cols = 8
    cw = max(i.width for _, i in imgs) + 8
    ch = max(i.height for _, i in imgs) + 18
    rows = (len(imgs) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * cw, rows * ch), (24, 24, 28, 255))
    from PIL import ImageDraw
    dr = ImageDraw.Draw(sheet)
    for k, (n, img) in enumerate(imgs):
        cx, cy = (k % cols) * cw, (k // cols) * ch
        sheet.alpha_composite(img, (cx + 4, cy + 4))
        dr.text((cx + 4, cy + ch - 13), n[:16], fill=(190, 190, 190, 255))
    print(f"  sheet: {len(imgs)} images from art/{d}/{want}*")
    return sheet


def render_fragment(frag: dict, out: Path) -> None:
    """
    Draw a corpus fragment using the art of the install it came FROM.

    Fragments store art by name, so this resolves names against that source's
    own lists - the point being to see what a fragment actually looks like
    before deciding to place it. Placing it here is a separate problem.

    Both tiles and objects are laid into one absolute 100x100 frame at a fixed
    origin, so the square and hex mirroring stay consistent with each other.
    """
    from port import load_sources
    srcs = load_sources()
    src = frag["source"]
    if src not in srcs:
        sys.exit(f"fragment source {src} is not in sources.json")
    gi = GameIndex(Path(srcs[src]["path"]), pristine=False)
    palette = load_palette(gi)
    art = ArtCache(gi, palette)

    OX, OY = 30, 30
    items = []          # (sort_y, sort_x, image, screen_x, screen_y)

    for ry, row in enumerate(frag["floor"]):
        for rx, c in enumerate(row):
            if c < 0:
                continue
            name = frag["palette"][c]
            img = art.by_path(f"art/tiles/{name}")[0]
            if img is None:
                continue
            sq = (OY + ry) * 100 + (OX + rx)
            x, y = square_xy(sq)
            items.append((-1, 0, img, x, y))

    for o in frag["objects"]:
        d = TYPE_NAME.get(o["t"])
        if not d or not o["art"]:
            continue
        name = o["art"].split(",")[0]
        if o["t"] == OBJ_TYPE_CRITTER and not name.lower().endswith(".frm"):
            name += ".fr0"
        rot = o.get("rot") or 0
        img, ox, oy = art.by_path(f"art/{d}/{name}", rot)
        if img is None:
            continue
        hx, hy = OX * 2 + o["dx"], OY * 2 + o["dy"]
        if not (0 <= hx < 200 and 0 <= hy < 200):
            continue
        x, y = hex_xy_screen(hy * 200 + hx)
        sx, sy = x + 16 + ox, y + 8 + oy
        items.append((y, x, img, sx - img.width // 2, sy - (img.height - 1)))

    if not items:
        sys.exit("nothing drawable in this fragment")

    minx = min(i[3] for i in items)
    miny = min(i[4] for i in items)
    maxx = max(i[3] + i[2].width for i in items)
    maxy = max(i[4] + i[2].height for i in items)
    canvas = Image.new("RGBA", (maxx - minx + 8, maxy - miny + 8), (10, 10, 12, 255))

    for _sy, _sx, img, x, y in sorted(items, key=lambda i: (i[0], i[1])):
        canvas.alpha_composite(img, (x - minx, y - miny))

    canvas.save(out)
    w, h = frag["size_sq"]
    print(f"  {frag['id']}: {w}x{h} squares, {frag['object_count']} objects, "
          f"{len(items)} drawn -> {canvas.width}x{canvas.height}px")


def load_fragment(frag_id: str) -> dict:
    import json
    path = ROOT / "corpus" / "fragments.jsonl"
    if not path.is_file():
        sys.exit("no corpus - run: python tools/corpus.py index")
    with path.open(encoding="utf-8") as f:
        for line in f:
            if frag_id in line:
                fr = json.loads(line)
                if fr["id"] == frag_id:
                    return fr
    sys.exit(f"no fragment {frag_id}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("map", nargs="?", help="path to a .map file")
    ap.add_argument("-o", "--out", default="build/render.png")
    ap.add_argument("--elevation", type=int, default=0, help="index, not level")
    ap.add_argument("--no-objects", action="store_true")
    ap.add_argument("--art", help="render a single FRM by game path")
    ap.add_argument("--sheet", help="contact sheet, e.g. art/walls/tent")
    ap.add_argument("--fragment", help="render a corpus fragment by id")
    ap.add_argument("--base", type=Path, default=None)
    ap.add_argument("--source", help="render using another install's art")
    args = ap.parse_args()

    if args.source:
        from port import load_sources
        srcs = load_sources()
        if args.source not in srcs:
            sys.exit(f"unknown source {args.source}")
        gi = GameIndex(Path(srcs[args.source]["path"]), pristine=False)
    else:
        gi = GameIndex(inside(args.base) if args.base else None)

    out = inside(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)

    if args.fragment:
        render_fragment(load_fragment(args.fragment), out)
    elif args.sheet:
        render_sheet(gi, args.sheet).save(out)
    elif args.art:
        img = Frm.parse(gi.vfs.read(args.art)).to_image(load_palette(gi))
        img.resize((img.width * 2, img.height * 2), Image.NEAREST).save(out)
        print(f"  {args.art}: {img.width}x{img.height}")
    else:
        if not args.map:
            sys.exit("give a .map, or --art / --sheet")
        data = inside(args.map).read_bytes()  # NOSONAR - path checked by foclib.paths.inside()
        m = MapFile.parse(data, gi.proto)
        render_map(gi, m, args.elevation, not args.no_objects).save(out)

    print(f"  wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
