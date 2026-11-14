#!/usr/bin/env python3
"""
fonline2_source.py - make the FOnline 2 client usable as an asset source, like
the TC installs, without repeating anything we already have.

    python tools/fonline2_source.py report            classify everything, write nothing
    python tools/fonline2_source.py build             (re)generate build/fonline2_src/
    python tools/fonline2_source.py build --variants  also convert same-name/different-art pieces (as <name>_f2)
    python tools/fonline2_source.py sheets            contact-print the converted art (build/fonline2/sheets/)

WHAT THE CLIENT IS
------------------
FOnline 2 (Season 4) keeps its art in zips under Data\\. The files named .png are
NOT PNGs: int32 width, int32 height (little endian), then width*height RGBA
bytes. A .fofrm text file ties frames into an animation (fps, offs_x/offs_y =
the FRM header offsets, [dir_N] frm_K= paths, next_x_K/next_y_K = per-frame
shift). Everything else (UI, fonts, 3D critters, sound, music) has no Fallout 2
equivalent and is listed in the report, not converted. There are no maps or
protos: FOnline keeps those on the server.

WHAT build DOES
---------------
1. Classifies every art asset against tools/art_index.py (the pixel-hash index of
   everything we own: FOC, Fallout 1/2, every TC):
     REPEAT   every frame already exists somewhere          -> skipped
     NEAR     same name, same outline, colours within a hair -> skipped
     RESCALED an inventory icon that is a bigger copy of ours -> skipped
     VARIANT  same name, different picture                   -> skipped (--variants converts them)
     NEW      nothing like it anywhere                       -> converted
   Static duplicates inside the client itself are converted once.
2. Converts NEW art to real FRMs (palette = color.pal; nearest colour, no
   dithering, alpha thresholded; exact palette colours, including the animated
   ones, keep their index) and copies the new critters' FRMs verbatim (their
   names are cut to the engine's six-character base).
3. Writes build/fonline2_src/data/ laid out like a game's data folder, as an
   OVERLAY on the FOC base: every list holds the base's entries first and the
   new ones after, so mounted over the base (the gecko launcher does this) the
   new pieces sit beside the vanilla ones. Protos are generated from a typical
   vanilla proto of each kind (the client has none); names come from the art
   names in a merged pro_*.msg.
4. Writes build/fonline2/catalog.csv (every asset, its verdict, who has it) and
   report.txt.

The result is `fonline2` in tools/sources.json ("derived": the tools never
index it as something we already own, "overlay": tools/port_stamp.py keeps
vanilla ids as they are). port.py list/art/proto, findart.py and the gecko
launcher "Fallout FOnline 2" read it like any TC.

LIMITS, ON PURPOSE
------------------
* Static walls/scenery/items carry NO placement offsets in the client (vanilla
  FRMs nearly always do). They are estimated from vanilla pieces of the same
  family and size (about 80% right for walls); catalog.csv says which were
  guessed. Animated pieces keep the offsets their .fofrm records.
* Tiles are browse-only: the engine addresses 4096 tile slots and the base
  already fills 4077 of them (CLAUDE.md 4d). port_stamp swaps a new tile for the
  nearest-looking FOC one.
* Colour: roughly half the new pictures use palette colours exactly; the rest
  are quantized.
"""

from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
import os
import re
import shutil
import struct
import sys
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import numpy as np  # noqa: E402

import art_index  # noqa: E402
import port  # noqa: E402
from foclib import GameIndex, TYPE_NAME, parse_lst, parse_msg  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
SRC_KEY = "fonline2"
TREE = ROOT / "build" / "fonline2_src"
DATA = TREE / "data"
REPORT_DIR = ROOT / "build" / "fonline2"
MARK = TREE / ".generated_by_fonline2_source"

# zip -> [(top folder inside it, FOC art dir)]
ART_ZIPS = {
    "ArtScenery": [("Scenery", "scenery")],
    "ArtWall": [("Walls", "walls")],
    "ArtTiles": [("Tiles", "tiles")],
    "ArtItem": [("Items", "items"), ("Inventory", "inven")],
    "Main": [("Misc", "misc")],
}
# What is in the client but has no Fallout 2 equivalent (counted in the report).
NOT_CONVERTED = {
    "ArtCars": "vehicle paint sprites (FOnline's drivable cars)",
    "ArtUi": "FOnline interface art", "InterfaceArt": "FOnline interface art",
    "InterfaceArt01": "FOnline interface art", "InterfaceArt02": "FOnline interface art",
    "FactionIcons": "FOnline faction icons", "Avatars": "FOnline portraits",
    "ArtSplash": "loading/splash pictures", "ArtWeather": "weather particles",
    "SpriteSheets": "FOnline sprite sheets", "Mapper": "FOnline mapper UI",
    "Seasonal": "seasonal 3D critter parts",
    "3DAnims": "3D critter animations (FBX)", "3DConfig": "3D critter config", "3DModels": "3D critter models (FBX)",
    "3DTextures": "3D critter textures",
    "AudioAmbient": "ambient sound (wav/ogg)", "AudioCritters": "critter sound (wav)", "AudioSfx": "sound effects (wav/ogg)",
    "AudioWeapons": "weapon sound (wav)", "AlbumNevada": "music (ogg)", "AlbumOther": "music (ogg)",
    "AlbumReninJohnson": "music (ogg)", "AlbumVaultArchives": "music (ogg)", "Video": "intro video (ogg)",
    "Shaders": "GLSL shaders",
}

# FRM palette slots that cycle in game; a nearest-colour match never lands here
# (an exact match to one keeps it - that is how the art was made).
ANIMATED = set(range(229, 255))
CRITTER_BASE_LEN = 6            # engine / gecko limit, see port.CRITTER_NAME_LEN
MSG_START = 6_000_000           # message ids for generated protos (name = even, description = +1)
MSG_FILE = {"items": "pro_item.msg", "critters": "pro_crit.msg", "scenery": "pro_scen.msg", "walls": "pro_wall.msg"}
PROTO_DIRS = ("items", "critters", "scenery", "walls")


# ================================================================== reading

def load_cfg() -> dict:
    return port.load_sources().get(SRC_KEY) or {}


def client_data_dir(override: str | None) -> Path:
    cfg = load_cfg()
    base = Path(override or cfg.get("client") or "")
    for cand in (base / "Data", base):
        if (cand / "ArtScenery.zip").is_file():
            return cand
    sys.exit(f"FOnline 2 client not found at {base} - set 'client' in tools/sources.json or pass --client")


class Zips:
    """Zip files by name with case-insensitive member lookup."""

    def __init__(self, folder: Path):
        self.folder = folder
        self._zf: dict[str, zipfile.ZipFile] = {}
        self._low: dict[str, dict[str, str]] = {}

    def open(self, name: str) -> zipfile.ZipFile:
        if name not in self._zf:
            self._zf[name] = zipfile.ZipFile(self.folder / f"{name}.zip")
            self._low[name] = {n.lower(): n for n in self._zf[name].namelist() if not n.endswith("/")}
        return self._zf[name]

    def members(self, name: str) -> dict[str, str]:
        self.open(name)
        return self._low[name]

    def read(self, name: str, member: str) -> bytes:
        zf = self.open(name)
        return zf.read(self._low[name].get(member.lower(), member))


def decode_image(b: bytes) -> np.ndarray:
    w, h = struct.unpack("<ii", b[:8])
    if w <= 0 or h <= 0 or len(b) < 8 + w * h * 4:
        raise ValueError("not a FOnline raw image")
    return np.frombuffer(b, dtype=np.uint8, offset=8, count=w * h * 4).reshape(h, w, 4)


def parse_fofrm(text: str) -> dict:
    """{'fps', 'count', 'ox', 'oy', 'dirs': {d: {'frames': {k: path}, 'nx': {k: v}, 'ny': {k: v}}}}"""
    out: dict = {"fps": None, "count": None, "ox": None, "oy": None, "dirs": {}}
    cur = None
    for line in text.splitlines():
        s = line.strip()
        if not s or s[0] in "#;":
            continue
        m = re.match(r"\[dir_(\d+)\]", s, re.I)
        if m:
            cur = int(m.group(1))
            out["dirs"].setdefault(cur, {"frames": {}, "nx": {}, "ny": {}})
            continue
        if "=" not in s:
            continue
        k, v = (x.strip() for x in s.split("=", 1))
        kl = k.lower()
        d = out["dirs"].setdefault(cur if cur is not None else 0, {"frames": {}, "nx": {}, "ny": {}}) \
            if (kl.startswith(("frm", "next_"))) else None
        if kl == "fps":
            out["fps"] = int(v)
        elif kl == "count":
            out["count"] = int(v)
        elif kl == "offs_x":
            out["ox"] = int(v)
        elif kl == "offs_y":
            out["oy"] = int(v)
        elif kl == "frm":
            d["frames"][0] = v
        elif (m := re.fullmatch(r"frm_(\d+)", kl)):
            d["frames"][int(m.group(1))] = v
        elif (m := re.fullmatch(r"next_([xy])_(\d+)", kl)):
            d["n" + m.group(1)][int(m.group(2))] = int(v)
    return out


@dataclass
class Asset:
    zipname: str
    kind: str                       # FOC art dir
    name: str                       # lowercase stem
    dirs: dict[int, list[str]]      # direction -> frame members
    ox: int | None = None
    oy: int | None = None
    fps: int = 10
    nxy: dict[tuple[int, int], tuple[int, int]] = field(default_factory=dict)
    source: str = ""
    # filled by classify()
    hashes: list[bytes] = field(default_factory=list)
    status: str = ""
    reason: str = ""
    matches: str = ""
    shape: tuple[int, int] = (0, 0)
    offset_how: str = ""
    out_name: str = ""
    alias_of: str = ""

    @property
    def frames_total(self) -> int:
        return sum(len(v) for v in self.dirs.values())

    @property
    def animated(self) -> bool:
        return self.frames_total > 1


def enumerate_assets(zips: Zips) -> list[Asset]:
    assets: list[Asset] = []
    for zname, tops in ART_ZIPS.items():
        members = zips.members(zname)
        for top, kind in tops:
            pre = top.lower() + "/"
            mine = {low: real for low, real in members.items() if low.startswith(pre)}
            referenced: set[str] = set()
            for low, real in sorted(mine.items()):
                if not low.endswith(".fofrm"):
                    continue
                f = parse_fofrm(zips.read(zname, real).decode("utf8", "replace"))
                folder = real.rsplit("/", 1)[0]
                dirs: dict[int, list[str]] = {}
                nxy = {}
                for d, info in sorted(f["dirs"].items()):
                    row = []
                    for k in sorted(info["frames"]):
                        rel = info["frames"][k].replace("\\", "/")
                        mem = mine.get((folder + "/" + rel).lower())
                        if mem is None:
                            continue
                        referenced.add(mem.lower())
                        row.append(mem)
                        if k in info["nx"] or k in info["ny"]:
                            nxy[(d, len(row) - 1)] = (info["nx"].get(k, 0), info["ny"].get(k, 0))
                    if row:
                        dirs[d] = row
                if not dirs:
                    continue
                assets.append(Asset(zname, kind, Path(real).stem.lower(), dirs, f["ox"], f["oy"],
                                    f["fps"] or 10, nxy, real))
            for low, real in sorted(mine.items()):
                if low.endswith(".png") and low not in referenced:
                    assets.append(Asset(zname, kind, Path(real).stem.lower(), {0: [real]}, source=real))
    return assets


# ============================================================ classification

class Known:
    """Same-name art we already own, for judging a same-name FOnline 2 picture."""

    def __init__(self, idx: art_index.ArtIndex):
        self.idx = idx
        self._gi: dict[str, GameIndex] = {}
        self._pal: dict[str, np.ndarray] = {}

    def _open(self, key: str):
        if key not in self._gi:
            if key == art_index.FOC_KEY:
                gi = GameIndex(extra_dirs=[ROOT / "mod" / "data"], pristine=False)
            else:
                gi = port.open_source(key)
            self._gi[key] = gi
            try:
                self._pal[key] = art_index.palette6(gi.vfs.read("color.pal"))
            except FileNotFoundError:
                self._pal[key] = art_index.palette6(GameIndex().vfs.read("color.pal"))
        return self._gi[key], self._pal[key]

    def frame0(self, adir: str, stem: str):
        """[(source, rgb8, opaque)] for every source that has art\\adir\\stem.frm."""
        out = []
        for key in sorted(self.idx.name_sources(adir, stem), key=lambda k: (k != art_index.FOC_KEY, k)):
            try:
                gi, pal = self._open(key)
                data = gi.vfs.read(f"art\\{adir}\\{stem}.frm")
                _d, _f, w, h, px = next(art_index.frm_frames(data))
            except Exception:  # noqa: BLE001
                continue
            a = np.frombuffer(px, dtype=np.uint8).reshape(h, w)
            out.append((key, pal[a].astype(np.int32) << 2, a != 0))
        return out


def judge_same_name(kind: str, img: np.ndarray, cands) -> tuple[str, str, str]:
    """(status, reason, source) for a picture whose name we own but whose pixels differ."""
    rgb = img[:, :, :3].astype(np.int32)
    op = img[:, :, 3] >= 128
    best = None
    for src, k_rgb, k_op in cands:
        if k_rgb.shape != rgb.shape:
            v = ("size", 0.0, 99.0, src, k_rgb.shape)
        else:
            both, union = op & k_op, op | k_op
            iou = float(both.sum()) / max(1, int(union.sum()))
            diff = float(np.abs(rgb[both] - k_rgb[both]).mean()) if both.any() else 99.0
            v = ("same", iou, diff, src, k_rgb.shape)
        if best is None or (v[0] == "same" and (best[0] != "same" or v[2] < best[2])):
            best = v
    if best is None:
        return "VARIANT", "same name elsewhere but unreadable", ""
    if best[0] == "size":
        if kind == "inven":
            return "RESCALED", f"our icon is {best[4][1]}x{best[4][0]}, this is {img.shape[1]}x{img.shape[0]}", best[3]
        return "VARIANT", f"{best[3]} has this name at {best[4][1]}x{best[4][0]}", best[3]
    if best[1] > 0.99 and best[2] < 8:
        return "NEAR", f"same picture as {best[3]} (colour off by {best[2]:.1f}/255)", best[3]
    return "VARIANT", f"{best[3]} has this name, outline match {best[1]:.2f}", best[3]


def classify(assets: list[Asset], zips: Zips, idx: art_index.ArtIndex) -> None:
    known = Known(idx)
    seen_static: dict[tuple, Asset] = {}
    for a in assets:
        imgs_h = []
        first = None
        for d in sorted(a.dirs):
            for m in a.dirs[d]:
                try:
                    img = decode_image(zips.read(a.zipname, m))
                except Exception as e:  # noqa: BLE001
                    a.status, a.reason = "SKIP", f"unreadable image {m}: {e}"
                    break
                if first is None:
                    first = img
                    a.shape = (img.shape[1], img.shape[0])
                imgs_h.append(art_index.raw_rgba_hash(img.shape[1], img.shape[0], img))
            if a.status:
                break
        if a.status:
            continue
        a.hashes = imgs_h
        have = [idx.has_frame(h) for h in imgs_h]
        owners = collections.Counter(s for h in imgs_h for s in idx.sources_of(h))
        a.matches = ",".join(f"{k}" for k, _ in owners.most_common(3))
        if all(have):
            a.status, a.reason = "REPEAT", "every frame already exists"
            continue
        name_owners = idx.name_sources(a.kind, a.name)
        if name_owners:
            cands = known.frame0(a.kind, a.name)
            a.status, a.reason, src = judge_same_name(a.kind, first, cands)
            a.matches = a.matches or src
            continue
        a.status = "NEW"
        a.reason = "some frames exist elsewhere" if any(have) else "nothing like it anywhere"
        if not a.animated:
            key = (a.kind, a.hashes[0], a.ox, a.oy)
            if key in seen_static:
                a.status, a.reason, a.alias_of = "REPEAT", f"same picture as {seen_static[key].name} in this client", seen_static[key].name
            else:
                seen_static[key] = a


# ============================================================ offsets / FRM

def family(stem: str) -> str:
    return re.sub(r"[\d_].*$", "", stem)


class OffsetModel:
    """Placement offsets for static art, learned from the base's own FRMs:
    mode of (xo, yo) among single-frame pieces of the same family and size, else
    of the same size, else (0, 0)."""

    def __init__(self):
        gi = GameIndex()
        self.fam: dict[tuple, collections.Counter] = {}
        self.size: dict[tuple, collections.Counter] = {}
        for kind in ("walls", "scenery", "items"):
            self.fam[kind] = collections.defaultdict(collections.Counter)
            self.size[kind] = collections.defaultdict(collections.Counter)
            try:
                lst = parse_lst(gi.vfs.read(f"art\\{kind}\\{kind}.lst"))
            except FileNotFoundError:
                continue
            for nm in lst[:4096]:
                if not nm:
                    continue
                try:
                    data = gi.vfs.read(f"art\\{kind}\\{nm.split(',')[0]}")
                except FileNotFoundError:
                    continue
                if len(data) < 0x46 or struct.unpack_from(">H", data, 8)[0] != 1:
                    continue
                xo = struct.unpack_from(">h", data, 0x0A)[0]
                yo = struct.unpack_from(">h", data, 0x16)[0]
                w, h = struct.unpack_from(">HH", data, 0x3E)
                stem = Path(nm.split(",")[0]).stem.lower()
                self.fam[kind][(family(stem), w, h)][(xo, yo)] += 1
                self.size[kind][(w, h)][(xo, yo)] += 1

    def guess(self, kind: str, stem: str, w: int, h: int) -> tuple[int, int, str]:
        if kind == "tiles" or kind == "inven" or kind == "misc":
            return 0, 0, "none needed"
        c = self.fam.get(kind, {}).get((family(stem), w, h))
        if c and sum(c.values()) >= 2:
            return (*c.most_common(1)[0][0], "family+size")
        c = self.size.get(kind, {}).get((w, h))
        if c and sum(c.values()) >= 3:
            return (*c.most_common(1)[0][0], "size")
        return 0, 0, "none (set by eye)"


class Quantizer:
    def __init__(self, pal6: np.ndarray):
        self.pal = pal6.astype(np.int32)
        self.exact: dict[tuple, int] = {}
        for i in range(255, 0, -1):          # lowest index wins a duplicate colour
            self.exact[tuple(self.pal[i])] = i
        self.near_idx = np.array([i for i in range(1, 256) if i not in ANIMATED])
        self.near_pal = self.pal[self.near_idx]
        self.cache: dict[tuple, int] = {}

    def to_indices(self, img: np.ndarray) -> tuple[bytes, float]:
        """RGBA -> palette indices (0 = transparent). Returns (bytes, fraction of exact matches)."""
        h, w = img.shape[:2]
        op = img[:, :, 3] >= 128
        rgb6 = (img[:, :, :3] >> 2).astype(np.int32)
        out = np.zeros((h, w), dtype=np.uint8)
        px = rgb6[op]
        if len(px) == 0:
            return out.tobytes(), 1.0
        keys = px[:, 0] * 4096 + px[:, 1] * 64 + px[:, 2]
        uniq, inv = np.unique(keys, return_inverse=True)
        counts = np.bincount(inv, minlength=len(uniq))
        lut = np.zeros(len(uniq), dtype=np.uint8)
        is_exact = np.zeros(len(uniq), dtype=bool)
        miss: list[int] = []
        for j, k in enumerate(uniq.tolist()):
            tup = (k >> 12, (k >> 6) & 63, k & 63)
            hit = self.exact.get(tup)
            if hit is not None:
                lut[j], is_exact[j] = hit, True
                continue
            hit = self.cache.get(tup)
            if hit is not None:
                lut[j] = hit
            else:
                miss.append(j)
        if miss:
            m = np.array([((uniq[j] >> 12), (uniq[j] >> 6) & 63, uniq[j] & 63) for j in miss], dtype=np.int32)
            d = m[:, None, :] - self.near_pal[None, :, :]
            dist = 2 * d[:, :, 0] ** 2 + 4 * d[:, :, 1] ** 2 + 3 * d[:, :, 2] ** 2
            best = self.near_idx[dist.argmin(axis=1)]
            for j, b, t in zip(miss, best.tolist(), m.tolist()):
                lut[j] = b
                self.cache[tuple(t)] = b
        out[op] = lut[inv]
        return out.tobytes(), float(counts[is_exact].sum()) / len(px)


def make_frm(dirs: dict[int, list[tuple[int, int, bytes, int, int]]], fps: int, xo: int, yo: int) -> bytes:
    """dirs[d] = [(w, h, indices, shiftx, shifty)]. A single-direction FRM repeats
    direction 0's offset in all six slots, as vanilla does."""
    nd = max(dirs) + 1 if dirs else 1
    fpd = max(len(v) for v in dirs.values())
    header = struct.pack(">IHHH", 4, fps, 0, fpd) + struct.pack(">6h", *([xo] * 6)) + struct.pack(">6h", *([yo] * 6))
    offs, body = [], bytearray()
    for d in range(6):
        if d >= nd and nd == 1:
            offs.append(0)
            continue
        offs.append(len(body))
        for (w, h, px, sx, sy) in dirs.get(d, dirs[0]):
            body += struct.pack(">HHIhh", w, h, w * h, sx, sy) + px
    return header + struct.pack(">6I", *offs) + struct.pack(">I", len(body)) + bytes(body)


# =========================================================== critter files

CRIT_EXT = re.compile(r"\.(frm|fr[0-5])$", re.I)


def plan_critters(zips: Zips, idx: art_index.ArtIndex, base_lst: list[str]):
    """Group OldCritters.zip by full base name and decide what to copy.

    Returns (files, new_bases, rows) where files = [(member, dest_name)], new_bases
    = {dest_base: (full_base, humanoid_guess)} and rows are catalog rows.
    """
    members = zips.members("OldCritters")
    groups: dict[str, list[str]] = collections.defaultdict(list)
    for low, real in members.items():
        if low.startswith("art/critters/") and CRIT_EXT.search(low):
            stem = Path(real).stem.lower()
            groups[stem[:-2]].append(real)
    base_known = {e.split(",")[0].strip().lower() for e in base_lst if e}
    taken = set(base_known)
    rows, files = [], []
    new_bases: dict[str, tuple[str, bool]] = {}
    for full in sorted(groups):
        mem = sorted(groups[full])
        exact = [m for m in mem if hashlib.md5(zips.read("OldCritters", m), usedforsecurity=False).digest() in idx.crit]
        if len(exact) == len(mem):
            rows.append(("critters", full, "REPEAT", f"all {len(mem)} files already exist", "", len(mem), ""))
            continue
        base_is_known = full in base_known or any(
            idx.names.get(("critters", Path(m).stem.lower())) for m in mem)
        if base_is_known:
            # an animation we lack for a critter we have: keep only file names nobody owns
            fresh = [m for m in mem if not idx.name_sources("critters", Path(m).stem)]
            same = [m for m in mem if m not in exact and m not in fresh]
            for m in fresh:
                files.append((m, Path(m).name.lower()))
            rows.append(("critters", full, "NEW" if fresh else "VARIANT",
                         f"{len(fresh)} file(s) we lack, {len(exact)} identical, {len(same)} same-name-different (skipped)",
                         "", len(fresh), ""))
            continue
        d = full if len(full) <= CRITTER_BASE_LEN else full[:CRITTER_BASE_LEN]
        n = 2
        while d in taken:
            d = f"{full[:CRITTER_BASE_LEN - 1]}{n}"
            n += 1
        taken.add(d)
        suffixes = [Path(m).stem.lower()[-2:] for m in mem]
        humanoid = len({s[1] for s in suffixes if s[0] == "a"}) >= 8 and len(mem) >= 40
        new_bases[d] = (full, humanoid)
        for m in mem:
            files.append((m, d + Path(m).stem.lower()[-2:] + Path(m).suffix.lower()))
        rows.append(("critters", full, "NEW", f"new critter, {len(mem)} files, base '{d}'", "", len(mem), d))
    return files, new_bases, rows


# ================================================================== build

def prettify(stem: str) -> str:
    return stem


def collect(assets: list[Asset], variants: bool) -> list[Asset]:
    pick = [a for a in assets if a.status == "NEW" or (variants and a.status == "VARIANT")]
    # unique output names per kind
    used: dict[str, set[str]] = collections.defaultdict(set)
    for a in pick:
        base = a.name + ("_f2" if a.status == "VARIANT" else "")
        nm, n = base, 2
        while nm in used[a.kind]:
            nm = f"{base}{n}"
            n += 1
        used[a.kind].add(nm)
        a.out_name = nm
    return pick


def write_lines(path: Path, lines: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(("\r\n".join(lines) + "\r\n").encode("cp1252", "replace"))


def pick_template(gi: GameIndex, t: int, size: int | None = None, subtype: int | None = None,
                  art_stem: str | None = None) -> bytes:
    """The most typical vanilla proto of a kind (mode of everything past the fid), or
    the one drawing a named art when art_stem is given."""
    name = TYPE_NAME[t]
    lst = gi.proto_lst(t)
    art = [n.lower() for n in parse_lst(gi.vfs.read(f"art\\{name}\\{name}.lst"))]
    shapes: collections.Counter = collections.Counter()
    by_shape: dict[bytes, bytes] = {}
    for fn in lst:
        if not fn:
            continue
        try:
            b = gi.vfs.read(f"proto\\{name}\\{fn}")
        except FileNotFoundError:
            continue
        if size is not None and len(b) != size:
            continue
        if subtype is not None and (t in (0, 2)) and struct.unpack_from(">I", b, 32)[0] != subtype:
            continue
        if art_stem is not None:
            fid = struct.unpack_from(">I", b, 8)[0] & 0xFFF
            if fid < len(art) and art[fid].startswith(art_stem):
                return b
            continue
        key = b[12:52] + b[56:]          # skip pid/msg/fid and an item's inventory fid
        shapes[key] += 1
        by_shape.setdefault(key, b)
    if art_stem is not None:
        raise LookupError(f"no {name} proto draws {art_stem}")
    return by_shape[shapes.most_common(1)[0][0]]


def build(args) -> int:
    zdir = client_data_dir(args.client)
    zips = Zips(zdir)
    print(f"client: {zdir}")
    print("indexing what we already own (cached after the first run)...")
    idx = art_index.load_index(refresh=args.refresh_index)

    assets = enumerate_assets(zips)
    print(f"{len(assets)} art assets in the client; classifying...")
    classify(assets, zips, idx)

    base = GameIndex()                         # FOC base: vanilla + RPU, no FOC layer
    base_crit = parse_lst(base.vfs.read("art\\critters\\critters.lst"))
    crit_files, new_bases, crit_rows = plan_critters(zips, idx, base_crit)

    tally = collections.Counter((a.kind, a.status) for a in assets)
    report = ["FOnline 2 -> Fallout source: what the client holds, against everything we own", ""]
    report.append(f"{'art dir':10s}{'REPEAT':>8s}{'NEAR':>7s}{'RESCALED':>10s}{'VARIANT':>9s}{'NEW':>7s}{'(total)':>9s}")
    for kind in ("scenery", "walls", "tiles", "items", "inven", "misc"):
        row = [tally[(kind, s)] for s in ("REPEAT", "NEAR", "RESCALED", "VARIANT", "NEW")]
        report.append(f"{kind:10s}" + "".join(f"{v:>{w}d}" for v, w in zip(row, (8, 7, 10, 9, 7))) + f"{sum(row):>9d}")
    ct = collections.Counter(r[2] for r in crit_rows)
    report.append(f"critters   {ct['REPEAT']} critter bases already ours, {sum(1 for r in crit_rows if r[2] == 'NEW')} with new files "
                  f"({len(new_bases)} are new critters: {', '.join(sorted(new_bases))})")
    report.append("")
    report.append("not converted (no Fallout 2 equivalent):")
    for z, why in NOT_CONVERTED.items():
        p = zdir / f"{z}.zip"
        if p.is_file():
            report.append(f"  {z:20s} {len(zips.members(z)):6d} files  {why}")
    print("\n".join(report))

    REPORT_DIR.mkdir(parents=True, exist_ok=True)
    with (REPORT_DIR / "catalog.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["kind", "name", "status", "reason", "matches", "frames", "size", "offsets", "converted_as"])
        for a in assets:
            w.writerow([a.kind, a.name, a.status, a.reason, a.matches, a.frames_total,
                        f"{a.shape[0]}x{a.shape[1]}", a.offset_how, ""])
        for r in crit_rows:
            w.writerow([r[0], r[1], r[2], r[3], r[4], r[5], "", "", r[6]])
    if args.command == "report":
        (REPORT_DIR / "report.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
        print(f"\ncatalog: {REPORT_DIR / 'catalog.csv'} (nothing else written)")
        return 0

    # ------------------------------------------------------------ tree
    pick = collect(assets, args.variants)
    if TREE.exists():
        if not MARK.is_file():
            sys.exit(f"{TREE} exists and was not generated by this tool - not touching it")
        shutil.rmtree(TREE)
    DATA.mkdir(parents=True)
    MARK.write_text("generated by tools/fonline2_source.py; safe to delete and rebuild\n")

    pal_raw = base.vfs.read("color.pal")
    (DATA / "color.pal").write_bytes(pal_raw)
    quant = Quantizer(art_index.palette6(pal_raw))
    offs = OffsetModel()

    # --- art lists start as the base's own
    art_lists: dict[str, list[str]] = {}
    for kind in ("scenery", "walls", "tiles", "items", "inven", "misc", "critters"):
        art_lists[kind] = list(parse_lst(base.vfs.read(f"art\\{kind}\\{kind}.lst")))
    base_len = {k: len(v) for k, v in art_lists.items()}
    new_art: dict[str, dict[str, int]] = collections.defaultdict(dict)   # kind -> out_name -> 0-based list index

    exact_frac: dict[str, list[float]] = collections.defaultdict(list)
    n_guess = collections.Counter()
    for a in pick:
        dirs_out: dict[int, list] = {}
        w0, h0 = a.shape
        for d in sorted(a.dirs):
            row = []
            for k, m in enumerate(a.dirs[d]):
                img = decode_image(zips.read(a.zipname, m))
                px, frac = quant.to_indices(img)
                exact_frac[a.kind].append(frac)
                sx, sy = a.nxy.get((d, k), (0, 0))
                row.append((img.shape[1], img.shape[0], px, sx, sy))
            dirs_out[d] = row
        if a.ox is not None or a.oy is not None or a.animated:
            xo, yo, how = a.ox or 0, a.oy or 0, "fofrm"
        else:
            xo, yo, how = offs.guess(a.kind, a.name, w0, h0)
        a.offset_how = how
        n_guess[(a.kind, how)] += 1
        fn = f"{a.out_name}.frm"
        out = DATA / "art" / a.kind / fn
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(make_frm(dirs_out, a.fps, xo, yo))
        idx0 = len(art_lists[a.kind])
        art_lists[a.kind].append(fn)
        new_art[a.kind][a.out_name] = idx0

    # --- critters
    for member, dest in crit_files:
        out = DATA / "art" / "critters" / dest
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(zips.read("OldCritters", member))
    crit_index: dict[str, int] = {}
    for d, (full, humanoid) in sorted(new_bases.items()):
        i = len(art_lists["critters"])
        art_lists["critters"].append(f"{d},11,1" if humanoid else f"{d},{i}")
        crit_index[d] = i

    for kind, lst in art_lists.items():
        if len(lst) > base_len[kind]:
            write_lines(DATA / "art" / kind / f"{kind}.lst", lst)

    # --- protos (a template per kind, patched)
    proto_lists: dict[str, list[str]] = {}
    new_names: dict[str, list[str]] = collections.defaultdict(list)   # art dir -> display names (msg order)
    for pdir in PROTO_DIRS:
        t = [k for k, v in TYPE_NAME.items() if v == pdir][0]
        proto_lists[pdir] = list(base.proto_lst(t))
    base_plen = {k: len(v) for k, v in proto_lists.items()}

    def add_proto(pdir: str, template: bytes, fid_index: int, name: str, patch=None) -> int:
        t = [k for k, v in TYPE_NAME.items() if v == pdir][0]
        index = len(proto_lists[pdir]) + 1                       # proto lists are 1-based
        names = new_names[pdir]
        msg_id = MSG_START + 2 * len(names)
        names.append(name)
        b = bytearray(template)
        struct.pack_into(">III", b, 0, (t << 24) | index, msg_id, (t << 24) | fid_index)
        if patch:
            patch(b)
        fn = f"{index:08d}.pro"
        out = DATA / "proto" / pdir / fn
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(bytes(b))
        proto_lists[pdir].append(fn)
        return index

    tmpl_scen = pick_template(base, 2, size=45, subtype=5)
    tmpl_wall = pick_template(base, 3, size=36)
    tmpl_item = pick_template(base, 0, size=69, subtype=5)
    try:
        tmpl_crit = pick_template(base, 1, art_stem="mamrat")
    except LookupError:
        tmpl_crit = pick_template(base, 1)
    for nm, i in sorted(new_art["scenery"].items(), key=lambda kv: kv[1]):
        add_proto("scenery", tmpl_scen, i, nm)
    for nm, i in sorted(new_art["walls"].items(), key=lambda kv: kv[1]):
        add_proto("walls", tmpl_wall, i, nm)

    # items: ground art (items.lst) + inventory icon (inven.lst)
    def base_index(kind: str, stem: str) -> int | None:
        for j, e in enumerate(art_lists[kind][:base_len[kind]]):
            if e and Path(e.split(",")[0]).stem.lower() == stem:
                return j
        return None

    item_stems = sorted(set(new_art["items"]) | set(new_art["inven"]))
    item_skipped = []
    no_icon: list[str] = []
    for stem in item_stems:
        g = new_art["items"].get(stem, base_index("items", stem.removesuffix("_f2")))
        iv = new_art["inven"].get(stem, base_index("inven", stem.removesuffix("_f2")))
        if g is None:
            item_skipped.append((stem, "icon only: no ground art to place"))
            continue
        if iv is None:
            no_icon.append(stem)      # keeps the template's own icon: browse it, do not ship it
        add_proto("items", tmpl_item, g, stem,
                  (lambda b, iv=iv: struct.pack_into(">I", b, 52, 0x07000000 | iv)) if iv is not None else None)
    for d, i in sorted(crit_index.items()):
        add_proto("critters", tmpl_crit, i, d)

    for pdir, lst in proto_lists.items():
        if len(lst) > base_plen[pdir]:
            write_lines(DATA / "proto" / pdir / f"{pdir}.lst", lst)

    # --- messages: the base's names first, ours after
    for pdir, names in new_names.items():
        if not names:
            continue
        try:
            msg = parse_msg(base.vfs.read(f"text\\english\\game\\{MSG_FILE[pdir]}"))
        except FileNotFoundError:
            msg = {}
        lines = [f"{{{mid}}}{{}}{{{text}}}" for mid, text in sorted(msg.items())]
        lines += [f"{{{MSG_START + 2 * j}}}{{}}{{{n}}}" for j, n in enumerate(names)]
        write_lines(DATA / "text" / "english" / "game" / MSG_FILE[pdir], lines)

    (TREE / "README.txt").write_text(
        "Generated by tools/fonline2_source.py from the FOnline 2 client - do not edit.\n"
        "An overlay on the FOC base: lists = base entries + the pieces FOnline 2 has that we do not.\n"
        "Read it with tools/port.py (source 'fonline2'); mount data/ over the base to browse it in gecko.\n",
        encoding="utf-8")

    # --- numbers back into the catalog / report
    conv = collections.Counter(a.kind for a in pick)
    with (REPORT_DIR / "catalog.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["kind", "name", "status", "reason", "matches", "frames", "size", "offsets", "converted_as"])
        for a in assets:
            w.writerow([a.kind, a.name, a.status, a.reason, a.matches, a.frames_total,
                        f"{a.shape[0]}x{a.shape[1]}", a.offset_how,
                        f"art/{a.kind}/{a.out_name}.frm" if a.out_name else ""])
        for r in crit_rows:
            w.writerow([r[0], r[1], r[2], r[3], r[4], r[5], "", "", r[6]])
    report.append("")
    report.append("converted into build/fonline2_src/data:")
    for kind in ("scenery", "walls", "tiles", "items", "inven", "misc"):
        fr = exact_frac.get(kind, [])
        report.append(f"  {kind:10s}{conv[kind]:6d} pieces"
                      + (f"   exact palette colours in {100 * float(np.mean(fr)):.0f}% of pixels" if fr else "")
                      + (f"   offsets: " + ", ".join(f"{h} {n}" for (k, h), n in sorted(n_guess.items()) if k == kind)
                         if any(k == kind for (k, _h) in n_guess) else ""))
    report.append(f"  critters  {len(crit_files)} files, {len(new_bases)} new critter bases")
    report.append(f"  protos    " + ", ".join(f"{p} +{len(proto_lists[p]) - base_plen[p]}" for p in PROTO_DIRS))
    if no_icon:
        report.append(f"  items drawn on the ground but with no inventory icon of their own: {len(no_icon)} "
                      f"(their protos borrow the template's icon; e.g. {', '.join(no_icon[:6])})")
    if item_skipped:
        report.append(f"  inventory icons whose item has no ground art (icon kept in art/inven, no proto): "
                      f"{len(item_skipped)} (e.g. {', '.join(s for s, _ in item_skipped[:6])})")
    tiles_total = len(art_lists["tiles"])
    report.append(f"  tiles.lst now {tiles_total} entries; only the first 4096 can be used in a map (browse-only)")
    (REPORT_DIR / "report.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
    print("\n".join(report[-12:]))
    print(f"\ntree: {TREE}\ncatalog: {REPORT_DIR / 'catalog.csv'}")
    return 0


def sheets(args) -> int:
    """Contact sheets of what build converted, so it can be looked at without gecko."""
    from PIL import Image, ImageDraw
    if not DATA.is_dir():
        sys.exit("nothing built yet - run: python tools/fonline2_source.py build")
    pal = art_index.palette6((DATA / "color.pal").read_bytes()).astype(np.int32) << 2
    out_dir = REPORT_DIR / "sheets"
    out_dir.mkdir(parents=True, exist_ok=True)
    for old in out_dir.glob("*.png"):
        old.unlink()
    cell, cols, rows = 150, 8, 6
    kinds = [args.kind] if args.kind else ["scenery", "walls", "tiles", "items", "inven", "misc"]
    for kind in kinds:
        files = sorted((DATA / "art" / kind).glob("*.frm")) if (DATA / "art" / kind).is_dir() else []
        for page in range(0, len(files), cols * rows):
            chunk = files[page:page + cols * rows]
            sheet = Image.new("RGB", (cols * cell, rows * (cell + 14)), (70, 70, 80))
            dr = ImageDraw.Draw(sheet)
            for k, f in enumerate(chunk):
                try:
                    _d, _f, w, h, px = next(art_index.frm_frames(f.read_bytes()))
                except StopIteration:
                    continue
                a = np.frombuffer(px, dtype=np.uint8).reshape(h, w)
                rgba = np.dstack([pal[a].astype(np.uint8), np.where(a == 0, 0, 255).astype(np.uint8)])
                im = Image.fromarray(rgba, "RGBA")
                sc = min((cell - 8) / w, (cell - 8) / h, 2)
                im = im.resize((max(1, int(w * sc)), max(1, int(h * sc))), Image.NEAREST)
                bg = Image.new("RGB", im.size, (110, 110, 120))
                bg.paste(im, mask=im.split()[3])
                x, y = (k % cols) * cell, (k // cols) * (cell + 14)
                sheet.paste(bg, (x + 4, y + 14))
                dr.text((x + 4, y + 1), f.stem[:22], fill=(255, 255, 255))
            sheet.save(out_dir / f"{kind}_{page // (cols * rows) + 1:02d}.png")
        print(f"  {kind}: {len(files)} pieces")
    print(f"sheets in {out_dir}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("command", choices=("report", "build", "sheets"))
    ap.add_argument("--kind", help="sheets: one art dir only (scenery, walls, tiles, items, inven, misc)")
    ap.add_argument("--client", help="FOnline 2 client folder (default: 'client' in tools/sources.json)")
    ap.add_argument("--variants", action="store_true", help="also convert same-name/different-picture pieces as <name>_f2")
    ap.add_argument("--refresh-index", action="store_true", help="rebuild the art index from scratch")
    args = ap.parse_args()
    return sheets(args) if args.command == "sheets" else build(args)


if __name__ == "__main__":
    raise SystemExit(main())
