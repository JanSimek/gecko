#!/usr/bin/env python3
"""
Format-parser self-test.

Parses every .map reachable through the base install's override chain. This is
the real check on foclib.formats: walking a map's object array requires correct
per-type extra-field sizes AND working proto resolution, so any mistake in
either desynchronises the walk and blows up loudly rather than quietly.

    python tools/selftest/test_formats.py [--verbose]

Exit code 0 only if every map parses.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from foclib import GameIndex, TYPE_NAME, pid_type  # noqa: E402


def main() -> int:
    verbose = "--verbose" in sys.argv
    gi = GameIndex()

    print("resolution chain:")
    print(gi.vfs.describe())
    print()

    names = set()
    for prov in gi.vfs.providers:
        if prov.kind == "dat":
            for n in prov.dat.names():
                if n.lower().endswith(".map"):
                    names.add(n)
        else:
            for p in prov.root.rglob("*"):
                if p.is_file() and p.suffix.lower() == ".map":
                    names.add(str(p.relative_to(prov.root)))
    names = sorted(names, key=str.lower)
    print(f"maps reachable : {len(names)}")

    ok = 0
    errs: list[tuple[str, str]] = []
    total_objects = 0
    total_scripts = 0
    pid_types_seen: dict[int, int] = {}
    unresolved_pids: set[int] = set()
    unresolved_scripts: set[int] = set()

    for n in names:
        try:
            m = gi.load_map(gi.vfs.read(n))
        except Exception as e:  # noqa: BLE001 - we want the message, whatever it is
            errs.append((n, f"{type(e).__name__}: {e}"))
            continue
        ok += 1
        total_objects += len(m.objects)
        total_scripts += len(m.script_pids)

        stack = list(m.objects)
        while stack:
            o = stack.pop()
            stack.extend(o.inventory)
            t = pid_type(o.pid)
            pid_types_seen[t] = pid_types_seen.get(t, 0) + 1
            if gi.proto(o.pid) is None:
                unresolved_pids.add(o.pid)
            if o.script_index >= 0 and gi.script_name(o.script_index + 1) is None:
                unresolved_scripts.add(o.script_index)

        if verbose:
            print(f"  {n:34s} v{m.version} id={m.map_id:5d} "
                  f"obj={len(m.objects):5d} scr={len(m.script_pids):4d}")

    print(f"parsed OK      : {ok}")
    print(f"FAILED         : {len(errs)}")
    print(f"objects walked : {total_objects}")
    print(f"map scripts    : {total_scripts}")
    print("object types   : " + ", ".join(
        f"{TYPE_NAME.get(t, t)}={c}" for t, c in sorted(pid_types_seen.items())))
    print(f"unresolved PIDs        : {len(unresolved_pids)}")
    print(f"unresolved script idxs : {len(unresolved_scripts)}")

    for n, e in errs[:15]:
        print(f"   FAIL {n} -> {e}")
    if len(errs) > 15:
        print(f"   ... and {len(errs) - 15} more")

    return 1 if errs else 0


if __name__ == "__main__":
    raise SystemExit(main())
