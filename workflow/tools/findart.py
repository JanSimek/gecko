#!/usr/bin/env python3
"""
findart.py - what art exists for a thing, across every source, with its cost.

    python tools/findart.py tent adobe well

Searches every source's art .lst files AND its proto .lst files for names
matching each term, and marks whether the name already exists in our base
(free to use) or would have to be ported.

Names are searched, not descriptions, so this finds what Fallout's artists
happened to call things - which is the only handle we have.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from foclib import TYPE_NAME, GameIndex, parse_lst  # noqa: E402
from port import load_sources  # noqa: E402

ART_DIRS = ("scenery", "walls", "items", "critters", "misc", "tiles")
FREE = {"fo2", "resurrection", "sotj", "nevada", "yesterday"}


def art_lists(gi: GameIndex) -> dict[str, list[str]]:
    out = {}
    for d in ART_DIRS:
        try:
            out[d] = parse_lst(gi.vfs.read(f"art/{d}/{d}.lst"))
        except FileNotFoundError:
            out[d] = []
    return out


def main() -> int:
    terms = [t.lower() for t in sys.argv[1:]]
    if not terms:
        sys.exit("usage: findart.py <term> [term ...]")

    base = GameIndex()
    base_art = {d: {n.lower() for n in lst if n} for d, lst in art_lists(base).items()}

    srcs = load_sources()
    for term in terms:
        print(f"\n{'=' * 72}\n{term.upper()}\n{'=' * 72}")
        seen: dict[str, list[str]] = {}
        for name, meta in sorted(srcs.items()):
            p = Path(meta["path"])
            if not p.is_dir():
                continue
            try:
                gi = GameIndex(p, pristine=False)
            except FileNotFoundError:
                continue
            hits = []
            for d, lst in art_lists(gi).items():
                for n in lst:
                    if n and term in n.lower():
                        free = n.lower() in base_art.get(d, ())
                        hits.append((d, n, free))
            if hits:
                seen[name] = hits

        if not seen:
            print("  nothing found")
            continue

        for src in sorted(seen, key=lambda s: (s not in FREE, s)):
            hits = seen[src]
            free_n = sum(1 for _, _, f in hits if f)
            tag = "FREE" if src in FREE else "port"
            print(f"\n  {src} [{tag}] - {len(hits)} match, {free_n} already in our base")
            shown = {}
            for d, n, f in hits:
                shown.setdefault(d, []).append(f"{n}{'' if f else ' *'}")
            for d in sorted(shown):
                items = shown[d]
                print(f"    {d:9s} {', '.join(items[:14])}"
                      f"{f'  (+{len(items)-14} more)' if len(items) > 14 else ''}")
    print("\n  * = not in our base, would need porting")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
