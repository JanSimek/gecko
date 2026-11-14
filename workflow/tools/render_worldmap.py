#!/usr/bin/env python3
"""
render_worldmap.py - draw the worldmap the way the engine draws it.

    python tools/render_worldmap.py -o build/worldmap/foc_map.png

Not a stylised mock-up: the circles are the game's own WRLDSPR*.FRM sprites and
the labels are set in the game's own font1.aaf, following the exact rules in
worldmap.cc.

WHAT THE ENGINE ACTUALLY DOES (worldmap.cc, font_manager.cc)
-----------------------------------------------------------
  * circle art is intrface.lst index 336 + city size -> WRLDSPR0/1/2.FRM for
    small/medium/large.
  * world_pos is the circle's TOP-LEFT, not its centre. The bounds check is
    `cityX <= 472 - width`, so the sprite hangs down and right of the point.
  * the circle is composited with _dark_translucent_trans_buf_to_buf using a
    blend table built from _colorTable[992] - 15-bit RGB 0,31,0, pure green.
  * the label sits at y + circleHeight + 3, centred on the circle:
    `x + width/2 - textWidth/2`, drawn in that same green with FONT_SHADOW.
  * the worldmap calls fontSetCurrent(101), i.e. font1.aaf.
  * a name only appears once wmAreaIsKnown is true, which needs visitedState
    AND state == CITY_STATE_KNOWN. start_state=on gives KNOWN but not visited,
    so every location reads "Unknown" until the player goes there.

AAF format: 'AAFF', maxHeight, letterSpacing, wordSpacing, lineSpacing (all
big-endian int16), then 256 glyphs of (width, height, offset) = 2060 bytes of
header. Glyph data is one byte per pixel holding a blend level 0..7 (NOT 0..255),
bottom-aligned within maxHeight; the pen advances by width + letterSpacing.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from foclib import GameIndex, parse_lst   # noqa: E402
import worldmap                            # noqa: E402

DEPLOY = ROOT / "base" / "mods" / "foc"
GREEN = np.array([0, 255, 0], float)      # _colorTable[992]
DARKEN = 0.82                             # a touch darker than the raw tiles


class Font:
    def __init__(self, blob: bytes):
        if blob[:4] != b"AAFF":
            raise ValueError("not an AAF font")
        (self.max_h, self.letter_sp, self.word_sp,
         self.line_sp) = struct.unpack_from(">4H", blob, 4)
        self.glyphs = []
        for i in range(256):
            w, h, off = struct.unpack_from(">HHI", blob, 12 + i * 8)
            self.glyphs.append((w, h, off))
        self.data = blob[2060:]

    def width(self, s: str) -> int:
        return sum(self.glyphs[ord(c) & 0xFF][0] + self.letter_sp for c in s)

    def draw(self, img: np.ndarray, s: str, x: int, y: int, colour) -> None:
        """Blend text into an HxWx3 float image, matching the engine's rules."""
        pen = x
        H, W, _ = img.shape
        for ch in s:
            w, h, off = self.glyphs[ord(ch) & 0xFF]
            top = y + (self.max_h - h)          # glyphs are bottom-aligned
            for gy in range(h):
                ty = top + gy
                if ty < 0 or ty >= H:
                    continue
                row = self.data[off + gy * w: off + (gy + 1) * w]
                for gx, a in enumerate(row):
                    tx = pen + gx
                    if a and 0 <= tx < W:
                        # AAF glyph bytes are a blend LEVEL, 0..7 - not 0..255.
                        # The engine indexes a colour blend table with
                        # (byte << 8) + background, so 7 is fully opaque.
                        # Dividing by 255 rendered the text at 3% and it
                        # disappeared entirely.
                        f = min(1.0, a / 7.0)
                        img[ty, tx] = img[ty, tx] * (1 - f) + np.asarray(colour) * f
            pen += w + self.letter_sp


def frm_first_frame(blob: bytes):
    w, h = struct.unpack_from(">HH", blob, 0x3E)
    px = np.frombuffer(blob[0x3E + 12: 0x3E + 12 + w * h], np.uint8)
    return w, h, px.reshape(h, w)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", default="build/worldmap/foc_map.png")
    ap.add_argument("--unknown", action="store_true",
                    help="label every location \"Unknown\", as the game does "
                         "before an area has been visited")
    ap.add_argument("--include-hidden", action="store_true",
                    help="also draw FOC areas that start hidden (e.g. the "
                         "Ruins, revealed by a quest); base areas stay hidden")
    args = ap.parse_args()

    gi = GameIndex(ROOT / "base", pristine=True)
    raw = gi.vfs.read("color.pal")
    pal = np.array([[min(255, raw[i * 3 + j] * 4) for j in range(3)]
                    for i in range(256)], float)

    # the deployed tiles, i.e. what the engine will load
    img = np.zeros((1500, 1400, 3), float)
    for t in range(20):
        blob = (DEPLOY / "art" / "intrface" / f"WRLDMP{t:02d}.FRM").read_bytes()
        w, h, px = frm_first_frame(blob)
        img[(t // 4) * 300:(t // 4) * 300 + h,
            (t % 4) * 350:(t % 4) * 350 + w] = pal[px]
    img *= DARKEN

    lst = parse_lst(gi.vfs.read("art/intrface/intrface.lst"))
    sphere = {}
    for i, size in enumerate(("small", "medium", "large")):
        sphere[size] = frm_first_frame(gi.vfs.read(f"art/intrface/{lst[336 + i]}"))
    font = Font(gi.vfs.read("font1.aaf"))

    city = (DEPLOY / "data" / "city.txt").read_text(errors="replace")
    blocks = re.split(r"^\[Area (\d+)\]", city, flags=re.M)
    drawn = 0
    for i in range(1, len(blocks), 2):
        b = blocks[i + 1]
        st = re.search(r"^start_state=(\w+)", b, re.M)
        nm = re.search(r"^area_name=(.+)$", b, re.M)
        wp = re.search(r"^world_pos=(\d+),(\d+)", b, re.M)
        sz = re.search(r"^size=(\w+)", b, re.M)
        shown = st and st.group(1).lower() == "on"
        if args.include_hidden and int(blocks[i]) >= worldmap.base_area_count(gi):
            shown = True
        if not (shown and nm and wp):
            continue
        x, y = int(wp.group(1)), int(wp.group(2))
        w, h, px = sphere[(sz.group(1) if sz else "Small").lower()]
        # translucent green, exactly where the engine puts it: top-left at world_pos
        for gy in range(h):
            for gx in range(w):
                v = px[gy, gx]
                if not v:
                    continue
                ty, tx = y + gy, x + gx
                if 0 <= ty < 1500 and 0 <= tx < 1400:
                    f = min(1.0, pal[v].mean() / 255.0)
                    img[ty, tx] = img[ty, tx] * (1 - f) + GREEN * f
        # Draw the real name. Note this is the ONE place this render knowingly
        # differs from the running game: the engine prints "Unknown" until
        # wmAreaIsKnown is true, which needs visitedState as well as
        # CITY_STATE_KNOWN. Naming them here makes the map readable as a
        # reference sheet; --unknown reproduces what the player first sees.
        label = "Unknown" if args.unknown else nm.group(1).strip()
        tw = font.width(label)
        font.draw(img, label, x + w // 2 - tw // 2 + 1, y + h + 3 + 1, (0, 0, 0))
        font.draw(img, label, x + w // 2 - tw // 2, y + h + 3, GREEN)
        drawn += 1

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).save(out)
    print(f"{drawn} locations drawn with the game's own sprites and font")
    print(f"circle sizes: " + ", ".join(f"{k}={v[0]}x{v[1]}" for k, v in sphere.items()))
    print(f"font1.aaf: max height {font.max_h}, letter spacing {font.letter_sp}")
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
