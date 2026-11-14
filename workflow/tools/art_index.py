#!/usr/bin/env python3
"""
art_index.py - a pixel-hash index of every piece of art we already own, so a new
asset source can be checked for repeats by what the picture IS, not what it is
called.

    python tools/art_index.py                 build/refresh the cache, print the totals
    python tools/art_index.py --refresh       rebuild every source from scratch
    python tools/art_index.py --find hash.png hmm  (see ArtIndex.lookup for library use)

WHAT IS INDEXED
---------------
Every FRM frame (all directions, all frames) under art\\ in:
  * FOC itself: the base (Fallout 2 + RPU) + whatever is deployed + mod/data
    (so art ported but not yet deployed counts as ours)
  * every install in tools/sources.json that is not marked "derived"
    (a derived source is a tree this project generated; indexing it would make
    its own output look like a repeat)

A frame's hash is its size plus its colours reduced to the palette's own 6 bits
per channel, with transparent pixels zeroed. Reducing to 6 bits makes it equal
to a PNG that was converted out of an FRM whichever way the converter scaled
the palette up to 8 bits (x4, or x4 + x>>4). Critter art (art\\critters) is
indexed by the md5 of the whole file instead: those FRMs are big, 6 directions
at a time, and the other sources ship them byte for byte.

CACHE
-----
build/art_index/<source>.pkl, one per source, rebuilt when the source's archives
or loose data change (size + mtime). A first full build takes several minutes
(Fallout Yesterday alone is 53,000 files); sources are hashed in parallel.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import pickle
import struct
import sys
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import numpy as np  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / "build" / "art_index"
FOC_KEY = "FOC"
VERSION = 2  # bump when the hash definition changes

ART_FRM_EXT = (".frm",)


def _is_art_file(ext: str) -> bool:
    return ext == ".frm" or (len(ext) == 4 and ext.startswith(".fr") and ext[3].isdigit())


# ----------------------------------------------------------------- hashing

def palette6(raw: bytes) -> np.ndarray:
    """color.pal -> 256x3 of 6-bit channel values. Entries 0 and 255 hold bytes
    above 63 in some installs; the engine's tables only use 6 bits."""
    a = np.frombuffer(raw[:768], dtype=np.uint8).reshape(256, 3).astype(np.int32)
    return np.minimum(a, 63).astype(np.uint8)


def hash_pixels(w: int, h: int, rgb6: np.ndarray, opaque: np.ndarray) -> bytes:
    rgb = rgb6.copy()
    rgb[~opaque] = 0
    m = hashlib.blake2b(digest_size=12)
    m.update(struct.pack("<ii", w, h))
    m.update(rgb.tobytes())
    m.update(opaque.astype(np.uint8).tobytes())
    return m.digest()


def frm_frames(data: bytes):
    """Yield (direction, frame, w, h, index bytes) for each distinct direction."""
    if len(data) < 0x3E:
        return
    fpd = struct.unpack_from(">H", data, 8)[0]
    offs = struct.unpack_from(">6I", data, 0x22)
    seen = set()
    for d in range(6):
        if offs[d] in seen:
            continue
        seen.add(offs[d])
        p = 0x3E + offs[d]
        for f in range(fpd):
            if p + 12 > len(data):
                return
            w, h, size = struct.unpack_from(">HHI", data, p)
            if w == 0 or h == 0 or p + 12 + w * h > len(data):
                return
            yield d, f, w, h, data[p + 12:p + 12 + w * h]
            p += 12 + size


def frame_hash(w: int, h: int, idx: bytes, pal6: np.ndarray) -> bytes:
    a = np.frombuffer(idx, dtype=np.uint8).reshape(h, w)
    return hash_pixels(w, h, pal6[a], a != 0)


def raw_rgba_hash(w: int, h: int, rgba: np.ndarray) -> bytes:
    """Hash for a decoded 8-bit RGBA image (what FOnline's .png files are)."""
    return hash_pixels(w, h, rgba[:, :, :3] >> 2, rgba[:, :, 3] > 0)


# ----------------------------------------------------------------- sources

def _walk_sig(path: Path) -> tuple:
    n = size = newest = 0
    for r, _d, fs in os.walk(path):
        for f in fs:
            try:
                st = os.stat(os.path.join(r, f))
            except OSError:
                continue
            n += 1
            size += st.st_size
            newest = max(newest, st.st_mtime_ns)
    return n, size, newest


def _dat_sig(path: Path) -> tuple:
    out = []
    for p in sorted(path.glob("*.dat")):
        try:
            st = p.stat()
        except OSError:
            continue
        out.append((p.name.lower(), st.st_size, st.st_mtime_ns))
    return tuple(out)


def source_signature(key: str, path: Path) -> tuple:
    if key == FOC_KEY:
        base = ROOT / "base"
        return (VERSION, _dat_sig(base), _dat_sig(base / "mods"),
                _walk_sig(base / "mods" / "foc"), _walk_sig(ROOT / "mod" / "data" / "art"))
    sig = [VERSION, _dat_sig(path)]
    for sub in ("data", "mods"):
        if (path / sub).is_dir():
            sig.append((sub, _walk_sig(path / sub)))
    for p in sorted(path.glob("patch*.dat")):
        if p.is_dir():
            sig.append((p.name, _walk_sig(p)))
    return tuple(sig)


def _names_of(gi) -> set[str]:
    out = set()
    for prov in gi.vfs.providers:
        if prov.kind == "dat":
            for n in prov.dat.names():
                out.add(n.lower().replace("/", "\\"))
        else:
            art = prov.root / "art"
            if art.is_dir():
                for r, _d, fs in os.walk(art):
                    for f in fs:
                        rel = os.path.relpath(os.path.join(r, f), prov.root)
                        out.add(rel.lower().replace("/", "\\"))
    return out


def _open(key: str, path: Path):
    from foclib import GameIndex
    if key == FOC_KEY:
        return GameIndex(extra_dirs=[ROOT / "mod" / "data"], pristine=False)
    return GameIndex(path, pristine=False)


def _index_one(args):
    """Worker: hash one source. Returns (key, signature, data)."""
    key, path_s, sig, fallback_pal = args
    path = Path(path_s)
    gi = _open(key, path)
    try:
        pal6 = palette6(gi.vfs.read("color.pal"))
    except FileNotFoundError:
        pal6 = palette6(fallback_pal)       # overlay mods ship no palette of their own
    frames: dict[bytes, list] = {}
    names: dict[tuple, set] = {}
    crit: dict[bytes, list] = {}
    nfile = nfrm = 0
    for n in sorted(_names_of(gi)):
        if not n.startswith("art\\"):
            continue
        parts = n.split("\\")
        if len(parts) < 3:
            continue
        adir = parts[1]
        stem, ext = os.path.splitext(parts[-1])
        if not _is_art_file(ext):
            continue
        names.setdefault((adir, stem), set()).add(key)
        try:
            data = gi.vfs.read(n)
        except Exception:  # noqa: BLE001
            continue
        nfile += 1
        if adir == "critters":
            crit.setdefault(hashlib.md5(data, usedforsecurity=False).digest(), []).append((key, n))
            continue
        try:
            for _d, _f, w, h, idx in frm_frames(data):
                hh = frame_hash(w, h, idx, pal6)
                lst = frames.setdefault(hh, [])
                if len(lst) < 4:
                    lst.append((key, n))
                nfrm += 1
        except Exception:  # noqa: BLE001
            pass
    return key, sig, {"frames": frames, "names": names, "crit": crit, "files": nfile, "nframes": nfrm}


class ArtIndex:
    """The merged index. frames: hash -> [(source, path)]; names: (artdir, stem) ->
    {sources}; crit: md5 of a critter file -> [(source, path)]."""

    def __init__(self, parts: dict[str, dict]):
        self.parts = parts
        self.frames: dict[bytes, list] = {}
        self.names: dict[tuple, set] = {}
        self.crit: dict[bytes, list] = {}
        for key, p in parts.items():
            for h, lst in p["frames"].items():
                tgt = self.frames.setdefault(h, [])
                for x in lst:
                    if len(tgt) < 4:
                        tgt.append(x)
            for k, s in p["names"].items():
                self.names.setdefault(k, set()).update(s)
            for h, lst in p["crit"].items():
                self.crit.setdefault(h, []).extend(lst[:2])

    def has_frame(self, h: bytes) -> bool:
        return h in self.frames

    def sources_of(self, h: bytes) -> list[str]:
        return sorted({s for s, _p in self.frames.get(h, [])})

    def name_sources(self, adir: str, stem: str) -> set[str]:
        return self.names.get((adir, stem.lower()), set())

    def summary(self) -> str:
        rows = [f"{k:16s} files {p['files']:6d}  frames {p['nframes']:7d}" for k, p in sorted(self.parts.items())]
        return "\n".join(rows) + f"\n{len(self.frames)} distinct frames, {len(self.names)} names, {len(self.crit)} critter files"


def load_index(refresh: bool = False, quiet: bool = False) -> ArtIndex:
    """Build (or load from cache) the index of everything we own."""
    import port
    from foclib import GameIndex

    srcs = {k: v for k, v in port.load_sources().items()
            if not v.get("derived") and k not in ("rpu_talking_heads", "owner_callum_photos")}
    CACHE.mkdir(parents=True, exist_ok=True)
    fallback_pal = GameIndex().vfs.read("color.pal")

    jobs, parts = [], {}
    todo = [(FOC_KEY, ROOT / "base")] + [(k, Path(v["path"])) for k, v in srcs.items()]
    for key, path in todo:
        if key != FOC_KEY and not path.is_dir():
            if not quiet:
                print(f"  {key}: not on this machine ({path}) - not indexed")
            continue
        sig = source_signature(key, path)
        cf = CACHE / f"{key}.pkl"
        if cf.is_file() and not refresh:
            try:
                got = pickle.loads(cf.read_bytes())
                if got.get("sig") == sig:
                    parts[key] = got["data"]
                    continue
            except Exception:  # noqa: BLE001
                pass
        jobs.append((key, str(path), sig, fallback_pal))
    if jobs:
        if not quiet:
            print(f"  indexing {len(jobs)} source(s): {', '.join(j[0] for j in jobs)} (cached afterwards)")
        t0 = time.time()
        with ProcessPoolExecutor(max_workers=min(len(jobs), max(1, (os.cpu_count() or 2) - 1))) as ex:
            for key, sig, data in ex.map(_index_one, jobs):
                (CACHE / f"{key}.pkl").write_bytes(pickle.dumps({"sig": sig, "data": data}))
                parts[key] = data
                if not quiet:
                    print(f"    {key}: {data['files']} files, {data['nframes']} frames ({time.time() - t0:.0f}s)")
    return ArtIndex(parts)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--refresh", action="store_true", help="rebuild every source")
    args = ap.parse_args()
    idx = load_index(refresh=args.refresh)
    print(idx.summary())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
