"""
DAT2 (Fallout 2) archive reader.

Format was confirmed empirically against nine archives on this machine
(vanilla master.dat/critter.dat, rpu.dat, and the Sonora/Nevada/Olympus/
Resurrection/SotJ TC archives), not taken on trust from a wiki page:

    trailer (last 8 bytes) : <u32 treeSize> <u32 totalFileSize>
    tree begins at          : filesize - 8 - treeSize
    tree                    : <u32 entryCount>
                              then per entry:
                                <u32 nameLen> <char[nameLen] name>
                                <u8 compressed> <u32 realSize>
                                <u32 packedSize> <u32 offset>

`compressed` is 1 for zlib, 0 for stored. Names use backslashes and are
case-insensitive in practice, so everything is keyed lowercased with forward
slashes normalised to backslashes.

Read-only by design. Nothing here opens a file for writing.
"""

from __future__ import annotations

import struct
import zlib
from pathlib import Path


def norm(name: str) -> str:
    """Canonical in-archive key: lowercase, backslash-separated."""
    return name.replace("/", "\\").lstrip("\\").lower()


class Dat2Error(Exception):
    pass


class Entry:
    __slots__ = ("name", "compressed", "real_size", "packed_size", "offset")

    def __init__(self, name, compressed, real_size, packed_size, offset):
        self.name = name
        self.compressed = compressed
        self.real_size = real_size
        self.packed_size = packed_size
        self.offset = offset

    def __repr__(self):
        return f"<Entry {self.name} {self.real_size}B>"


class Dat2:
    """A DAT2 archive opened read-only."""

    def __init__(self, path: str | Path):
        self.path = Path(path)
        self._entries: dict[str, Entry] = {}
        self._load_tree()

    def _load_tree(self) -> None:
        size = self.path.stat().st_size
        with self.path.open("rb") as f:
            if size < 8:
                raise Dat2Error(f"{self.path.name}: too small to be a DAT2")
            f.seek(size - 8)
            tree_size, total = struct.unpack("<II", f.read(8))
            if total != size:
                raise Dat2Error(
                    f"{self.path.name}: trailer says {total} bytes but file is "
                    f"{size} — not a DAT2 (Fallout 1 DAT1 is a different format)"
                )
            start = size - 8 - tree_size
            if start < 0:
                raise Dat2Error(f"{self.path.name}: bad tree size {tree_size}")
            f.seek(start)
            blob = f.read(tree_size)

        (count,) = struct.unpack_from("<I", blob, 0)
        p = 4
        for _ in range(count):
            (nlen,) = struct.unpack_from("<I", blob, p)
            p += 4
            raw = blob[p:p + nlen]
            p += nlen
            comp, real, packed, off = struct.unpack_from("<BIII", blob, p)
            p += 13
            name = raw.decode("cp1252", "replace")
            self._entries[norm(name)] = Entry(name, comp, real, packed, off)

    # -- interface ---------------------------------------------------------

    def __contains__(self, name: str) -> bool:
        return norm(name) in self._entries

    def __len__(self) -> int:
        return len(self._entries)

    def names(self):
        """Original-cased names, in tree order."""
        return [e.name for e in self._entries.values()]

    def read(self, name: str) -> bytes:
        e = self._entries.get(norm(name))
        if e is None:
            raise KeyError(f"{name} not in {self.path.name}")
        with self.path.open("rb") as f:
            f.seek(e.offset)
            raw = f.read(e.packed_size)
        if not e.compressed:
            return raw
        data = zlib.decompress(raw)
        if len(data) != e.real_size:
            raise Dat2Error(
                f"{self.path.name}:{e.name} decompressed to {len(data)} bytes, "
                f"tree says {e.real_size}"
            )
        return data

    def glob_prefix(self, prefix: str):
        """All entries whose path starts with `prefix` (e.g. 'scripts\\')."""
        p = norm(prefix)
        return [e.name for k, e in self._entries.items() if k.startswith(p)]
