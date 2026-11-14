#!/usr/bin/env python3
"""
find_source_item.py - search every source game's item names (pro_item.msg)
for words, to find something to port with port_item.py.

    python tools/find_source_item.py hazmat "radiation suit" "rad suit"
"""

from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import port  # noqa: E402


def main() -> int:
    words = [w.lower() for w in sys.argv[1:]]
    for name in port.load_sources():
        if name in ("fonline3", "rpu_talking_heads", "owner_callum_photos", "fo1"):
            continue
        try:
            src = port.open_source(name)
            txt = src.vfs.read("text\\english\\game\\pro_item.msg").decode("cp1252", "replace")
        except BaseException as e:  # noqa: BLE001
            print(f"{name}: {e}")
            continue
        msg = {int(m.group(1)): m.group(2) for m in re.finditer(r"\{(\d+)\}\{[^}]*\}\{([^}]*)\}", txt)}
        lst = src.proto_lst(0)
        for i in range(1, len(lst) + 1):
            try:
                b = src.vfs.read(src.proto_path(i))
            except Exception:  # noqa: BLE001
                continue
            nm = msg.get(struct.unpack_from(">i", b, 4)[0], "")
            if any(w in nm.lower() for w in words):
                sub = struct.unpack_from(">i", b, 0x20)[0]
                desc = msg.get(struct.unpack_from(">i", b, 4)[0] + 1, "")[:90]
                print(f"{name:12} 0x{i:04X} type {sub} {nm!r}: {desc}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
