"""
Fallout 1's .dat archives (DAT1), read-only. Same interface as Dat2, so the
VFS can mount Fallout 1's MASTER.DAT / CRITTER.DAT as a source.

Layout (all integers big-endian):

    u32 dir_count, u32, u32, u32
    dir_count x (u8 len, name)                     "ART\\CRITTERS", "." for root
    per directory, in the same order:
        u32 file_count, u32, u32, u32
        file_count x (u8 len, name, u32 flags, u32 offset, u32 size, u32 packed)

flags 0x40 means the file is LZSS-packed. The packed data is a run of
blocks, each led by a u16: with the top bit set the block is (value &
0x7FFF) bytes stored as-is, otherwise it is that many bytes of LZSS
(4096-byte window filled with spaces, write position 4078, match length
= low nibble + 3, one flag byte per eight items with 1 = literal). The
window starts afresh for every block.
"""

from __future__ import annotations

import struct
from pathlib import Path

from .dat2 import norm


class Dat1Error(Exception):
    pass


def lzss_block(src: bytes, out: bytearray) -> None:
    ring = bytearray(b" " * 4096)
    r = 4078
    i, n = 0, len(src)
    while i < n:
        flags = src[i]
        i += 1
        for bit in range(8):
            if i >= n:
                break
            if flags & (1 << bit):
                c = src[i]
                i += 1
                out.append(c)
                ring[r] = c
                r = (r + 1) & 0xFFF
            else:
                if i + 1 >= n:
                    i = n
                    break
                b1, b2 = src[i], src[i + 1]
                i += 2
                pos = b1 | ((b2 & 0xF0) << 4)
                length = (b2 & 0x0F) + 3
                for k in range(length):
                    c = ring[(pos + k) & 0xFFF]
                    out.append(c)
                    ring[r] = c
                    r = (r + 1) & 0xFFF


def unpack(data: bytes, size: int) -> bytes:
    out = bytearray()
    p = 0
    while p + 2 <= len(data) and len(out) < size:
        v = struct.unpack_from(">H", data, p)[0]
        p += 2
        if v == 0:
            break
        if v & 0x8000:
            ln = v & 0x7FFF
            out += data[p:p + ln]
            p += ln
        else:
            lzss_block(data[p:p + v], out)
            p += v
    return bytes(out[:size])


class Dat1:
    def __init__(self, path: str | Path):
        self.path = Path(path)
        self.entries: dict[str, tuple[int, int, int, int]] = {}
        with self.path.open("rb") as f:
            head = f.read(16)
            if len(head) < 16:
                raise Dat1Error("too short")
            dirs = struct.unpack(">I", head[:4])[0]
            if not 0 < dirs < 10000:
                raise Dat1Error("not a DAT1 archive")
            names = []
            for _ in range(dirs):
                ln = f.read(1)[0]
                names.append(f.read(ln).decode("cp1252"))
            for d in names:
                count = struct.unpack(">I", f.read(16)[:4])[0]
                for _ in range(count):
                    ln = f.read(1)[0]
                    fn = f.read(ln).decode("cp1252")
                    flags, off, size, packed = struct.unpack(">IIII", f.read(16))
                    full = fn if d == "." else f"{d}\\{fn}"
                    self.entries[norm(full)] = (flags, off, size, packed)

    def __contains__(self, name: str) -> bool:
        return norm(name) in self.entries

    def __len__(self) -> int:
        return len(self.entries)

    def names(self):
        return list(self.entries)

    def read(self, name: str) -> bytes:
        flags, off, size, packed = self.entries[norm(name)]
        with self.path.open("rb") as f:
            f.seek(off)
            if flags & 0x40:
                return unpack(f.read(packed), size)
            return f.read(size)

    def glob_prefix(self, prefix: str):
        p = norm(prefix)
        return [n for n in self.entries if n.startswith(p)]
