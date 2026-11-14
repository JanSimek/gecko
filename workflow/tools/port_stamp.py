#!/usr/bin/env python3
"""
port_stamp.py - convert a gecko stamp saved while browsing another game (via
tools/gecko_browse) into FOC's ID space, so it stamps the same thing in the
FOC editing profile.

    python tools/port_stamp.py --all --dry-run          # detect + report only
    python tools/port_stamp.py --all                    # convert every TC stamp
    python tools/port_stamp.py --all --redo             # reconvert from the saved originals
    python tools/port_stamp.py "bar" --source sonora    # one stamp, source forced
    python tools/port_stamp.py --all --since 1790000000 --source sonora
                                                        # what the TC launcher runs

WHY THIS EXISTS
---------------
A stamp (gecko's pattern JSON) stores raw ids: proPid, frmPid and tileId. Those
are positions in the game's own proto and art .lst files (see port.py's
docstring), so a stamp saved in a Sonora session and stamped in the FOC profile
draws whatever FOC keeps at those positions - a random mess. Every gecko session
also saves into the same %LOCALAPPDATA%\\gecko\\gecko\\patterns folder, with no
record of which game a stamp came from.

WHICH GAME A STAMP CAME FROM
----------------------------
Every object carries both its proPid and its frmPid, and in the game it was
captured from the proto at proPid has exactly that FID. That agreement is
checked against every source in tools/sources.json and against FOC itself; the
game that agrees best wins. A stamp FOC already agrees with as well as any TC
is left alone (Nevada and Resurrection reuse most vanilla numbering, so many of
their stamps are already correct in FOC).

HOW EACH ID IS MAPPED
---------------------
Objects, in order:
  1. already ported by port.py / port_bulk.py (the ledger) -> that FOC pid
  2. the same pid in FOC has the identical .pro and identical art -> kept
  3. a FOC proto of the same type draws byte-identical art -> that pid
     (port_bulk skipped art FOC already had, so this is common)
  4. otherwise port.py's cmd_proto ports it (fresh pid + art, ledgered)
  frmPid follows the chosen proto, or - for an object drawn with art other than
  its proto's - is matched to FOC art by bytes, porting the art if FOC lacks it.

Tiles: a map square stores a 12-bit tile id and base tiles.lst already fills
4077 of the 4096 lines (CLAUDE.md 4d), so a tile is matched to FOC's
byte-identical tile when there is one, and otherwise ported into a spare line
of tiles.lst (tools/tile_slots.py: lines that repeat an earlier tile's picture
or have no art, and that nothing of FOC's uses). Only when the spare lines run
out is it swapped for the closest-looking FOC tile (coarse colour/texture/outline
distance - see TileMatcher - each side decoded with its own game's palette). Every swap is contact-printed to
build/stamp_port/<stamp>_tiles.png, source on the left, chosen tile on the right.
A stamp with no objects (a roof or a floor) can't be told by its protos, so
with --source it counts as the source's when any of its tile ids draws
something else in FOC than in the source.

The original stamp is copied to _safety_backups/stamp_originals/<source>/ on F:,
never under %LOCALAPPDATA%: a process started from the Claude desktop app (an
MSIX package) has NEW files it creates under AppData silently redirected into
AppData/Local/Packages/Claude_*/LocalCache, where gecko and the TC launchers
can't see them (edits to existing files do reach the real folder) - and the
converted one written under the original name, tagged with a "focConverted"
block (gecko ignores unknown keys). Ported art lands at the end of the art
lists, above the engine's 4096 cap - run tools/repack_art.py afterwards to move
everything a map or stamp uses below it.
"""

from __future__ import annotations

import argparse
import contextlib
import datetime as _dt
import hashlib
import io
import json
import os
import shutil
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402

import lists  # noqa: E402
import port  # noqa: E402
import tile_slots  # noqa: E402
from foclib.paths import inside  # noqa: E402
from foclib import GameIndex, TYPE_NAME, parse_lst, parse_pro, pid_index, pid_type  # noqa: E402
from render import Frm, load_palette  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
from foclib import config as _config  # noqa: E402
PATTERNS = _config.patterns_dir()
# gecko saves the owner's own stamps here (PatternLibrary::customDir); older ones were loose in
# PATTERNS, which the stamp browser can't show.
CUSTOM = PATTERNS / "Custom"
ORIGINALS = ROOT / "_safety_backups" / "stamp_originals"


def own_stamps() -> list[Path]:
    """The owner's stamps: the Custom folder, plus any still loose in the patterns root."""
    return sorted(CUSTOM.glob("*.json")) + sorted(PATTERNS.glob("*.json"))
REPORT_DIR = ROOT / "build" / "stamp_port"
MOD_DATA = ROOT / "mod" / "data"

# Sources that cannot be a stamp's origin: fo2 IS the base FOC already stands
# on, and fonline3 is an art-only pack with no maps or protos to stamp from.
NOT_STAMP_SOURCES = {"fo2", "fonline3"}
TILE_W, TILE_H = 80, 36
EMPTY_TILE = 1  # CLAUDE.md 4c: an empty square is tile id 1


def sha(b: bytes) -> str:
    return hashlib.sha1(b, usedforsecurity=False).hexdigest()


def art_mask(art_dir: str) -> int:
    # Critter FIDs keep animation bits above the 12-bit index; other types
    # have nothing there (port.py cmd_proto has the long version).
    return 0xFFF if art_dir == "critters" else 0xFFFFFF


def art_file(art_dir: str, entry: str) -> str:
    return port.critter_art_filename(entry) if art_dir == "critters" else entry


# ------------------------------------------------------------------ FOC view

class Foc:
    """FOC as the next deploy will ship it: base plus mod/ids + mod/data,
    read straight from the authored tree so freshly ported protos and art are
    visible before deploy.py has run."""

    def __init__(self):
        self.gi = GameIndex()               # pristine base
        self._art: dict[str, list[str]] = {}
        self._local: dict[str, dict[str, Path]] = {}
        self._hash: dict[str, dict[str, int]] = {}
        self._protos_by_art: dict[int, dict[int, list[int]]] = {}
        self._proto_lsts: dict[int, list[str]] = {}
        self.palette = load_palette(self.gi)

    def reset(self):
        """Forget everything derived from mod/ids - call after porting."""
        self._art.clear()
        self._local.clear()
        self._protos_by_art.clear()
        self._proto_lsts.clear()
        for d in list(self._hash):
            del self._hash[d]

    def art_list(self, d: str) -> list[str]:
        if d not in self._art:
            self._art[d] = [n.lower() for n in lists.art_lst(self.gi, d)]
        return self._art[d]

    def _local_files(self, sub: str) -> dict[str, Path]:
        if sub not in self._local:
            p = MOD_DATA / sub
            self._local[sub] = ({q.name.lower(): q for q in p.iterdir()}
                                if p.is_dir() else {})
        return self._local[sub]

    def read(self, sub: str, name: str) -> bytes | None:
        q = self._local_files(sub).get(name.lower())
        if q is not None:
            return q.read_bytes()
        try:
            return self.gi.vfs.read(f"{sub.replace('/', chr(92))}\\{name}")
        except FileNotFoundError:
            return None

    def art_bytes(self, d: str, idx: int) -> bytes | None:
        lst = self.art_list(d)
        if not (0 <= idx < len(lst)) or not lst[idx]:
            return None
        return self.read(f"art/{d}", art_file(d, lst[idx]))

    def art_hash_index(self, d: str) -> dict[str, int]:
        """sha1(art bytes) -> first index drawing it (first wins: tiles.lst
        repeats 22 names, CLAUDE.md 4f)."""
        if d not in self._hash:
            out: dict[str, int] = {}
            for i in range(len(self.art_list(d))):
                b = self.art_bytes(d, i)
                if b is not None:
                    out.setdefault(sha(b), i)
            self._hash[d] = out
        return self._hash[d]

    def proto_lst(self, t: int) -> list[str]:
        if t not in self._proto_lsts:
            self._proto_lsts[t] = lists.proto_lst(self.gi, t)
        return self._proto_lsts[t]

    def proto_bytes(self, pid: int) -> bytes | None:
        t = pid_type(pid)
        if t not in TYPE_NAME:
            return None
        lst = self.proto_lst(t)
        i = pid_index(pid) - 1          # proto lists are 1-based
        if not (0 <= i < len(lst)) or not lst[i]:
            return None
        return self.read(f"proto/{TYPE_NAME[t]}", lst[i])

    def proto(self, pid: int):
        b = self.proto_bytes(pid)
        try:
            return parse_pro(b) if b else None
        except ValueError:
            return None

    def protos_by_art(self, t: int) -> dict[int, list[int]]:
        """art index -> FOC pids of type t whose proto draws it, base first."""
        if t not in self._protos_by_art:
            d = TYPE_NAME[t]
            out: dict[int, list[int]] = {}
            for i in range(1, len(self.proto_lst(t)) + 1):
                pid = (t << 24) | i
                p = self.proto(pid)
                if p is not None and ((p.fid >> 24) & 0xF) == t:
                    out.setdefault(p.fid & art_mask(d), []).append(pid)
            self._protos_by_art[t] = out
        return self._protos_by_art[t]


# ------------------------------------------------------------ source detection

def stamp_pairs(d: dict) -> set[tuple[int, int]]:
    return {(o["proPid"], o["frmPid"]) for v in d.get("variants", []) for o in v.get("objects", [])}


def agreement(gi, pairs, proto_fn) -> float:
    if not pairs:
        return 0.0
    ok = 0
    for pro, frm in pairs:
        p = proto_fn(gi, pro)
        if p is not None and p.fid == frm:
            ok += 1
    return ok / len(pairs)


def detect(d: dict, foc: Foc, sources: dict[str, GameIndex],
           overlay: frozenset = frozenset()) -> tuple[str | None, dict[str, float]]:
    pairs = stamp_pairs(d)
    scores = {"foc": agreement(foc, pairs, lambda g, p: g.proto(p))}
    for name, gi in sources.items():
        if name in overlay:
            # An overlay source (see "overlay" in tools/sources.json) holds only the
            # pieces BEYOND the base, so judge it on those alone: a stamp of vanilla
            # pieces plus a few new ones must not be called plain FOC just because
            # FOC agrees with most of its objects.
            new = {(pro, frm) for pro, frm in pairs
                   if pid_index(pro) > len(foc.gi.proto_lst(pid_type(pro)))}
            scores[name] = agreement(gi, new, lambda g, p: g.proto(p)) if new else 0.0
        else:
            scores[name] = agreement(gi, pairs, lambda g, p: g.proto(p))
    best = max((s for s in scores if s != "foc"), key=lambda s: scores[s], default=None)
    if best is None or scores[best] <= scores["foc"]:
        return None, scores
    return best, scores


# ------------------------------------------------------------------- tiles

def tiles_differ(d: dict, foc: Foc, src: GameIndex) -> bool:
    """Does any tile id in the stamp draw something else in FOC than in the
    source? (A stamp of FOC's own tiles, or of tiles the two share, doesn't.)"""
    try:
        src_lst = parse_lst(src.vfs.read("art\\tiles\\tiles.lst"))
    except FileNotFoundError:
        return False
    for v in d.get("variants", []):
        for layer in ("floor", "roof"):
            for t in v.get(layer, []):
                tid = t["tileId"]
                if tid <= EMPTY_TILE:
                    continue
                if not (0 <= tid < len(src_lst)):
                    return True
                try:
                    sb = src.vfs.read("art\\tiles\\" + src_lst[tid])
                except FileNotFoundError:
                    continue
                if foc.art_bytes("tiles", tid) != sb:
                    return True
    return False


def tile_rgba(data: bytes, palette) -> np.ndarray:
    """80x36 RGBA uint8 array; tiles of another size are cropped/padded."""
    frm = Frm.parse(data)
    img = frm.to_image(palette)
    canvas = Image.new("RGBA", (TILE_W, TILE_H))
    canvas.paste(img, (0, 0))
    return np.asarray(canvas, dtype=np.uint8)


# Look-alike metric, tuned by eye on the 67 Sonora tiles FOC lacks (contact
# sheets in build/stamp_port/). Coarse on purpose: two dirt tiles never agree
# pixel for pixel, and a per-pixel distance prefers whatever shares the outline
# over whatever shares the colour and texture - which is what reads as "the same
# floor". Per 5x6-pixel cell it compares colour, coverage and edge energy (so
# pebbles match pebbles), plus the tile's overall contrast; a candidate whose
# total coverage differs by more than COVER_GATE is never picked.
GRID_X, GRID_Y = 16, 6
SHAPE_COST = 3 * 110 * 110     # a cell opaque on one side only ~ a 110/channel miss
EDGE_WEIGHT = 6.0
COVER_GATE = 0.12
LUMA = np.array([0.299, 0.587, 0.114], np.float32)


def tile_features(a: np.ndarray):
    """a: [..., 36, 80, 4] -> (cell colour, cell coverage, cell edge energy,
    global colour std, total coverage)."""
    alpha = (a[..., 3] > 0).astype(np.float32)
    rgb = a[..., :3].astype(np.float32)
    lum = rgb @ LUMA
    edge = (np.abs(np.diff(lum, axis=-1, append=lum[..., -1:]))
            + np.abs(np.diff(lum, axis=-2, append=lum[..., -1:, :]))) * alpha
    ch, cw = TILE_H // GRID_Y, TILE_W // GRID_X
    lead = a.shape[:-3]
    al = alpha[..., :ch * GRID_Y, :cw * GRID_X].reshape(*lead, GRID_Y, ch, GRID_X, cw)
    cl = rgb[..., :ch * GRID_Y, :cw * GRID_X, :].reshape(*lead, GRID_Y, ch, GRID_X, cw, 3)
    ed = edge[..., :ch * GRID_Y, :cw * GRID_X].reshape(*lead, GRID_Y, ch, GRID_X, cw)
    ws = al.sum(axis=(-3, -1))
    col = (cl * al[..., None]).sum(axis=(-4, -2)) / np.maximum(ws, 1)[..., None]
    cov = ws / (ch * cw)
    edg = ed.sum(axis=(-3, -1)) / np.maximum(ws, 1)
    n = alpha.sum(axis=(-2, -1))
    mean = (rgb * alpha[..., None]).sum(axis=(-3, -2)) / np.maximum(n, 1)[..., None]
    var = (((rgb - mean[..., None, None, :]) ** 2) * alpha[..., None]).sum(axis=(-3, -2))         / np.maximum(n, 1)[..., None]
    return col, cov, edg, np.sqrt(var), n / (TILE_W * TILE_H)


class TileMatcher:
    def __init__(self, foc: Foc):
        self.foc = foc
        lst = foc.art_list("tiles")
        ids, arrs = [], []
        for i in range(2, min(len(lst), 4096)):
            name = lst[i]
            if not name or name.startswith("reserved"):
                continue
            b = foc.art_bytes("tiles", i)
            if b is None:
                continue
            try:
                a = tile_rgba(b, foc.palette)
            except (ValueError, struct.error):
                continue
            if a[..., 3].max() == 0:
                continue
            ids.append(i)
            arrs.append(a)
        self.ids = np.array(ids)
        self.col, self.cov, self.edg, self.std, self.tot = tile_features(np.stack(arrs))

    def closest(self, a: np.ndarray) -> tuple[int, float]:
        col, cov, edg, std, tot = tile_features(a)
        both = np.minimum(self.cov, cov[None])
        colour = (((self.col - col[None]) ** 2).sum(-1) * both).sum((1, 2))
        shape = np.abs(self.cov - cov[None]).sum((1, 2)) * SHAPE_COST
        edges = (((self.edg - edg[None]) ** 2) * both).sum((1, 2)) * EDGE_WEIGHT
        texture = ((self.std - std[None]) ** 2).sum(-1) * (GRID_X * GRID_Y)
        score = (colour + shape + edges + texture) / (GRID_X * GRID_Y)
        score = np.where(np.abs(self.tot - tot) > COVER_GATE, np.inf, score)
        k = int(np.argmin(score))
        return int(self.ids[k]), float(np.sqrt(score[k] / 3))


# ------------------------------------------------------------------ convert

class Converter:
    def __init__(self, foc: Foc, source: str, src: GameIndex, dry_run: bool, stamp_name: str,
                 overlay: bool = False):
        self.foc = foc
        self.source = source
        self.src = src
        # An overlay source's ids inside the base's lists ARE the base's (it holds
        # only what comes after), so those objects, art and tiles are kept as they are.
        self.overlay = overlay
        self._base_art_len: dict[str, int] = {}
        self.dry_run = dry_run
        self.stamp_name = stamp_name
        self.src_palette = load_palette(src)
        self.src_art: dict[str, list[str]] = {}
        self.pid_map: dict[int, int | None] = {}
        self.frm_map: dict[int, int | None] = {}
        self.tile_map: dict[int, int] = {}
        self.stats = {"kept": 0, "ledger": 0, "artmatch": 0, "ported": 0, "unresolved": 0}
        self.swaps: list[tuple[int, int, float]] = []
        self.exact_tiles = 0
        self.ported_tiles = 0
        self._spare: list[int] | None = None
        self.notes: list[str] = []

    # -- source helpers
    def src_art_list(self, d: str) -> list[str]:
        if d not in self.src_art:
            try:
                self.src_art[d] = parse_lst(self.src.vfs.read(f"art\\{d}\\{d}.lst"))
            except FileNotFoundError:
                self.src_art[d] = []
        return self.src_art[d]

    def src_art_bytes(self, d: str, idx: int) -> tuple[str | None, bytes | None]:
        lst = self.src_art_list(d)
        if not (0 <= idx < len(lst)) or not lst[idx]:
            return None, None
        f = art_file(d, lst[idx])
        try:
            return f, self.src.vfs.read(f"art\\{d}\\{f}")
        except FileNotFoundError:
            return f, None

    def ledger(self, kind: str, source_id: str) -> str | None:
        for r in port.read_ledger():
            if r["source"] == self.source and r["kind"] == kind and r["source_id"].lower() == source_id.lower():
                return r["dest_id"]
        return None

    def _quiet(self, fn, args):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            fn(args)
        self.foc.reset()
        return buf.getvalue()

    # -- objects
    def map_pid(self, pid: int) -> int | None:
        if pid in self.pid_map:
            return self.pid_map[pid]
        out = self._map_pid(pid)
        self.pid_map[pid] = out
        return out

    def base_art_len(self, d: str) -> int:
        if d not in self._base_art_len:
            try:
                self._base_art_len[d] = len(parse_lst(self.foc.gi.vfs.read(f"art\\{d}\\{d}.lst")))
            except FileNotFoundError:
                self._base_art_len[d] = 0
        return self._base_art_len[d]

    def _map_pid(self, pid: int) -> int | None:
        t = pid_type(pid)
        d = TYPE_NAME.get(t)
        dest = self.ledger("proto", f"0x{pid:08X}")
        if dest:
            self.stats["ledger"] += 1
            return int(dest, 16)
        if self.overlay and d is not None and pid_index(pid) <= len(self.foc.gi.proto_lst(t)):
            self.stats["kept"] += 1
            return pid
        sp_path = self.src.proto_path(pid)
        if sp_path is None or not self.src.vfs.exists(sp_path) or d is None:
            self.stats["unresolved"] += 1
            self.notes.append(f"proto 0x{pid:08X} not in {self.source}")
            return None
        sp_bytes = self.src.vfs.read(sp_path)
        sp = parse_pro(sp_bytes)
        _, s_art = self.src_art_bytes(d, sp.fid & art_mask(d))

        fp_bytes = self.foc.proto_bytes(pid)
        if fp_bytes == sp_bytes:
            fp = parse_pro(fp_bytes)
            if self.foc.art_bytes(d, fp.fid & art_mask(d)) == s_art:
                self.stats["kept"] += 1
                return pid

        if s_art is not None:
            idx = self.foc.art_hash_index(d).get(sha(s_art))
            if idx is not None:
                cands = self.foc.protos_by_art(t).get(idx, [])
                # same flags first (flat/no-block/light matter to how it sits),
                # then the lowest pid (base before ported)
                cands = sorted(cands, key=lambda c: (self.foc.proto(c).flags != sp.flags, c))
                if cands:
                    self.stats["artmatch"] += 1
                    return cands[0]

        if self.dry_run:
            self.stats["ported"] += 1
            self.notes.append(f"would port proto 0x{pid:08X}")
            return None
        out = self._quiet(port.cmd_proto, argparse.Namespace(
            source=self.source, pids=[f"0x{pid:08X}"], notes=f"gecko stamp '{self.stamp_name}'"))
        dest = self.ledger("proto", f"0x{pid:08X}")
        if not dest:
            self.stats["unresolved"] += 1
            self.notes.append(f"port of 0x{pid:08X} failed: {out.strip()[-200:]}")
            return None
        self.stats["ported"] += 1
        return int(dest, 16)

    def map_frm(self, pid: int, frm: int, new_pid: int | None) -> int | None:
        t = pid_type(pid)
        d = TYPE_NAME.get(t)
        sp = self.src.proto(pid)
        if new_pid is not None and sp is not None and sp.fid == frm:
            fp = self.foc.proto(new_pid)
            return fp.fid if fp is not None else None
        if frm in self.frm_map:
            return self.frm_map[frm]
        fd = TYPE_NAME.get((frm >> 24) & 0xF)
        mask = art_mask(fd) if fd else 0xFFFFFF
        if self.overlay and fd and (frm & mask) < self.base_art_len(fd):
            self.frm_map[frm] = frm            # the base's own art, same index
            return frm
        out = None
        if fd is not None:
            name, b = self.src_art_bytes(fd, frm & mask)
            if b is not None:
                idx = self.foc.art_hash_index(fd).get(sha(b))
                if idx is None and fd != "critters" and not self.dry_run:
                    self._quiet(port.cmd_art, argparse.Namespace(
                        source=self.source, paths=[f"art\\{fd}\\{name}"],
                        notes=f"gecko stamp '{self.stamp_name}'"))
                    idx = self.foc.art_hash_index(fd).get(sha(b))
                if idx is not None:
                    out = (frm & ~mask & 0xFFFFFFFF) | idx
        if out is None:
            self.notes.append(f"art 0x{frm:08X} for proto 0x{pid:08X}: no FOC match")
        self.frm_map[frm] = out
        return out

    # -- tiles
    def map_tile(self, tid: int, matcher_fn) -> int:
        if tid in self.tile_map:
            return self.tile_map[tid]
        if tid <= EMPTY_TILE:
            self.tile_map[tid] = tid
            return tid
        if self.overlay and tid < self.base_art_len("tiles"):
            self.tile_map[tid] = tid           # the base's own floor tile
            return tid
        name, b = self.src_art_bytes("tiles", tid)
        if b is None:
            self.notes.append(f"tile {tid} has no art in {self.source} - left empty")
            self.tile_map[tid] = EMPTY_TILE
            return EMPTY_TILE
        idx = self.foc.art_hash_index("tiles").get(sha(b))
        if idx is not None and idx < 4096:
            self.exact_tiles += 1
        else:
            idx = self.port_tile(name, b)
            if idx is None:
                idx, dist = matcher_fn().closest(tile_rgba(b, self.src_palette))
                self.swaps.append((tid, idx, dist))
            else:
                self.ported_tiles += 1
        self.tile_map[tid] = idx
        return idx

    def port_tile(self, name: str, b: bytes) -> int | None:
        """The source's tile in a spare line of tiles.lst (tile_slots.py), or
        None when there is no spare line left (or the name can't be used).
        A dry run only counts what would fit, and answers 1."""
        if self._spare is None:
            self._spare = tile_slots.spare_slots(self.foc.gi)
        if self.dry_run:
            if self._spare:
                self._spare.pop(0)
                return EMPTY_TILE
            return None
        try:
            tid = tile_slots.port_tile(self.source, self.src, name, self._spare,
                                       notes=f"tile in a spare tiles.lst line, gecko stamp '{self.stamp_name}'")
        except (RuntimeError, ValueError) as e:
            self.notes.append(f"tile {name}: {e}")
            return None
        # what the FOC view already read: the new line, and its picture
        lst = self.foc.art_list("tiles")
        if tid < len(lst):
            lst[tid] = name.lower()
        self.foc.art_hash_index("tiles")[sha(b)] = tid
        self.foc._local.pop("art/tiles", None)
        return tid

    def convert(self, d: dict, matcher_fn) -> dict:
        out = json.loads(json.dumps(d))
        for v in out.get("variants", []):
            kept = []
            for o in v.get("objects", []):
                new_pid = self.map_pid(o["proPid"])
                new_frm = self.map_frm(o["proPid"], o["frmPid"], new_pid)
                if new_pid is None or new_frm is None:
                    continue            # a loud drop beats a wrong sprite
                o["proPid"], o["frmPid"] = new_pid, new_frm
                kept.append(o)
            v["objects"] = kept
            for layer in ("floor", "roof"):
                for tile in v.get(layer, []):
                    tile["tileId"] = self.map_tile(tile["tileId"], matcher_fn)
        return out


def contact_sheet(conv: Converter, foc: Foc, out: Path) -> None:
    if not conv.swaps:
        return
    rows = sorted(conv.swaps, key=lambda s: -s[2])
    sheet = Image.new("RGBA", (2 * TILE_W + 12, len(rows) * (TILE_H + 4)), (24, 24, 28, 255))
    for r, (src_id, foc_id, _dist) in enumerate(rows):
        y = r * (TILE_H + 4)
        _, b = conv.src_art_bytes("tiles", src_id)
        sheet.alpha_composite(Image.fromarray(tile_rgba(b, conv.src_palette)), (0, y))
        fb = foc.art_bytes("tiles", foc_id)
        sheet.alpha_composite(Image.fromarray(tile_rgba(fb, foc.palette)), (TILE_W + 12, y))
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)


def original_path(meta: dict) -> Path:
    """Where a converted stamp's original lives ("original" is relative to
    ORIGINALS; a leading "_tc_originals/" is the older in-patterns layout)."""
    rel = meta["original"]
    if rel.startswith("_tc_originals/"):
        rel = rel[len("_tc_originals/"):]
    return ORIGINALS / rel


# --------------------------------------------------------------------- main

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("stamps", nargs="*", help="stamp names or paths (default with --all: every stamp)")
    ap.add_argument("--all", action="store_true", help="consider every stamp in the patterns folder")
    ap.add_argument("--source", help="force the source game (a tools/sources.json key)")
    ap.add_argument("--since", type=float, help="only stamps modified at/after this unix time")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--redo", action="store_true",
                    help="reconvert already-converted stamps from their saved originals")
    args = ap.parse_args()

    if args.stamps:
        paths = []
        for s in args.stamps:
            p = Path(s)
            name = s if s.lower().endswith(".json") else s + ".json"
            for folder in (CUSTOM, PATTERNS):
                if not p.is_file():
                    p = folder / name
            if not p.is_file():
                sys.exit(f"no such stamp: {s}")
            paths.append(inside(p))
    elif args.all:
        paths = sorted(own_stamps(), key=lambda p: p.stat().st_mtime)
    else:
        ap.error("name stamps, or pass --all")
    if args.since is not None:
        paths = [p for p in paths if p.stat().st_mtime >= args.since]
    if not paths:
        print("no stamps to consider")
        return 0

    foc = Foc()
    known = port.load_sources()
    if args.source:
        if args.source not in known:
            sys.exit(f"unknown source '{args.source}'. Known: {', '.join(sorted(known))}")
        names = [args.source]
    else:
        names = [n for n in known if n not in NOT_STAMP_SOURCES]
    overlay = frozenset(n for n in names if known.get(n, {}).get("overlay"))
    sources = {}
    for n in names:
        try:
            sources[n] = port.open_source(n)
        except SystemExit as e:
            print(f"  (skipping source {n}: {e})")

    matcher = []

    def matcher_fn():
        if not matcher:
            print("  indexing FOC floor tiles for look-alike matching...")
            matcher.append(TileMatcher(foc))
        return matcher[0]

    converted = 0
    for p in paths:
        d = json.loads(p.read_text(encoding="utf-8"))  # NOSONAR - path checked by foclib.paths.inside()
        prior = d.get("focConverted")
        if prior and not args.redo:
            print(f"{p.stem}: already converted from {prior.get('source')} - skipped")
            continue
        if prior:
            orig = original_path(prior)
            if not orig.is_file():
                print(f"{p.stem}: original {orig} is missing - cannot redo")
                continue
            d = json.loads(orig.read_text(encoding="utf-8"))
        src_name, scores = detect(d, foc, sources, overlay)
        if (args.source and src_name is None and not stamp_pairs(d) and args.source in sources
                and tiles_differ(d, foc, sources[args.source])):
            src_name = args.source      # tiles-only stamp: its ids draw other tiles in FOC
        if src_name is None and args.source in overlay and not stamp_pairs(d):
            # a floor-only stamp from an overlay launcher is the source's own if it
            # uses a tile beyond the base's list
            base_tiles = len(parse_lst(foc.gi.vfs.read("art\\tiles\\tiles.lst")))
            if any(t["tileId"] >= base_tiles for v in d.get("variants", [])
                   for layer in ("floor", "roof") for t in v.get(layer, [])):
                src_name = args.source
        top =", ".join(f"{k} {v:.2f}" for k, v in sorted(scores.items(), key=lambda kv: -kv[1])[:3])
        if src_name is None:
            print(f"{p.stem}: already FOC-compatible ({top}) - left alone")
            continue
        print(f"{p.stem}: from {src_name} ({top})")
        conv = Converter(foc, src_name, sources[src_name], args.dry_run, p.stem,
                         overlay=src_name in overlay)
        new = conv.convert(d, matcher_fn)
        s = conv.stats
        n_obj = sum(len(v.get("objects", [])) for v in d.get("variants", []))
        n_new = sum(len(v.get("objects", [])) for v in new.get("variants", []))
        print(f"    objects: {n_new}/{n_obj} placed | protos kept {s['kept']}, ledger {s['ledger']}, "
              f"same-art {s['artmatch']}, {'to port' if args.dry_run else 'ported'} {s['ported']}, "
              f"unresolved {s['unresolved']}")
        print(f"    tiles: {conv.exact_tiles} exact, {conv.ported_tiles} {'to port' if args.dry_run else 'ported'} "
              f"into spare tiles.lst lines, {len(conv.swaps)} swapped for look-alikes"
              + (f" (worst distance {max(x[2] for x in conv.swaps):.0f})" if conv.swaps else ""))
        for note in conv.notes[:8]:
            print(f"      - {note}")
        if len(conv.notes) > 8:
            print(f"      - ... {len(conv.notes) - 8} more")
        if args.dry_run:
            continue

        contact_sheet(conv, foc, REPORT_DIR / f"{p.stem}_tiles.png")
        if prior:
            backup = original_path(prior)     # keep pointing at the real original
        else:
            backup = ORIGINALS / src_name / p.name
            backup.parent.mkdir(parents=True, exist_ok=True)
            if backup.exists():
                backup = backup.with_name(f"{p.stem}.{_dt.datetime.now():%Y%m%d_%H%M%S}.json")
            shutil.copy2(p, backup)
        new["focConverted"] = {
            "source": src_name,
            "date": _dt.date.today().isoformat(),
            "original": str(backup.relative_to(ORIGINALS)).replace("\\", "/"),
            "tilesSwapped": len(conv.swaps),
            "tilesPorted": conv.ported_tiles,
            "objectsDropped": n_obj - n_new,
        }
        p.write_text(json.dumps(new, separators=(",", ":")) + "\n", encoding="utf-8")  # NOSONAR - the stamp goes back to the file it was read from
        converted += 1

    if converted:
        port.regen_ledger_md()
        print(f"\nconverted {converted} stamp(s). Originals are in {ORIGINALS}.")
        print("Next: python tools/repack_art.py  (moves art they use below the 4096 cap)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
