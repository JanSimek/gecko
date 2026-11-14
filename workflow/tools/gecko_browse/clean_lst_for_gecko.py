#!/usr/bin/env python3
"""
clean_lst_for_gecko.py - make ALL of a TC's .lst files loadable by gecko.

    python clean_lst_for_gecko.py "C:\\Fallout Sonora (Extended Release)"

Several Russian-community Fallout 2 total conversions ship LST files with
Cyrillic (CP1251) comments after the ';' on a line - human documentation,
harmless to the real game, which only reads up to the comma-separated fields
before it. gecko is stricter: FormatValidator::validateLstFile checks the
FIRST 64 BYTES of an LST file for pure ASCII (32-126, plus \\r\\n\\t) and
throws "LST file contains non-text data" on the first high byte it finds -
which a Cyrillic comment on line 1 hits immediately.

v2: the output folder name got a " [clean]" suffix. Reproduced live: when
this folder's basename was IDENTICAL to the real install root's basename
(both "Fallout Sonora (Extended Release)"), gecko mounted 4 of the 5
configured data paths and silently dropped the clean override entirely - no
log line, no error, just absent - and the map loader then hit the game's own
broken intrface.lst as if the override had never been written. Confirmed by
a minimal 2-path test (this folder alone worked fine) vs the full 4-path
list (silently dropped) - gecko's mount registry appears to be keyed by
folder basename, so two different full paths sharing one name collide.

v1 of this script only scrubbed gecko's 8 "essential" LST files (the ones
ResourceInitializer.cpp checks at startup) and that was NOT enough: opening
an actual map (not just browsing thumbnails) lazily loads scripts/scripts.lst
too, which several of these installs also carry Cyrillic-commented, and it
hit the identical failure the first time a real Sonora map was opened rather
than just browsed. Fixed by scanning EVERY .lst file in master.dat and
critter.dat rather than a fixed list, and scrubbing whichever ones actually
need it - cheap to do exhaustively, and it means the next lazily-loaded LST
gecko discovers doesn't need yet another one-off fix.

This does NOT touch the TC's own install. Every byte outside the
ASCII-printable-or-whitespace range is replaced with a space, so line length
and every comma-separated field before a ';' is untouched - only the
human-readable comment text is scrubbed. Output goes to
gecko_browse/_clean/<install folder name>/<same relative path>.lst.

The matching launcher mounts that _clean/<name> folder LAST (highest
priority), so gecko reads the scrubbed copies for just these files and the
TC's real dat archives for literally everything else - maps, art, protos,
sound, none of it touched.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from foclib.dat2 import Dat2  # noqa: E402

HERE = Path(__file__).resolve().parent
OUT_ROOT = HERE / "_clean"


def is_clean(data: bytes) -> bool:
    return all(b in (9, 10, 13) or 32 <= b <= 126 for b in data)


def scrub(data: bytes) -> bytes:
    return bytes(b if (b in (9, 10, 13) or 32 <= b <= 126) else 0x20 for b in data)


def main() -> int:
    if len(sys.argv) != 2:
        sys.exit(f"usage: {sys.argv[0]} <TC install directory>")
    install_dir = Path(sys.argv[1])
    if not install_dir.is_dir():
        sys.exit(f"not a directory: {install_dir}")

    # " [clean]" suffix is load-bearing, not cosmetic - see the module
    # docstring's v2 note. Must match build_gecko_browsers.py's own
    # clean_dir computation exactly, or the launcher won't find this output.
    out_dir = OUT_ROOT / f"{install_dir.name} [clean]"
    scrubbed = 0
    scanned = 0

    # Every *.dat in the install root, not just master.dat/critter.dat - a
    # standalone patch-based install (Nevada Mod Extended has neither of
    # those, only Patch000.dat/Patch001.dat) still needs its own lst files
    # scanned, and that's exactly where its Cyrillic scripts.lst was found.
    dat_paths = sorted(install_dir.glob("*.dat"), key=lambda p: p.name.lower())
    for dat_path in dat_paths:
        dat = Dat2(str(dat_path))
        for name in dat.names():
            if not name.lower().endswith(".lst"):
                continue
            scanned += 1
            data = dat.read(name)
            if is_clean(data):
                continue
            cleaned = scrub(data)
            # LOWERCASE, not the archive's own casing. Confirmed at the source:
            # gecko's own DatReader::openFile lowercases every entry name it
            # parses (DatReader.cpp, std::transform(...tolower...)), so a DAT
            # file matches gecko's lowercase-hardcoded ResourcePaths regardless
            # of its original case (Sonora's own archive stores INTRFACE.LST
            # uppercase). NativeFileSystem, which mounts this directory, does
            # NO such normalization - it indexes by the literal on-disk name.
            # Writing this override in the archive's original case (uppercase
            # for Sonora) meant it silently never matched gecko's lowercase
            # query and gecko fell through to the game's own broken copy -
            # reproduced by testing the override in isolation (it worked) vs
            # combined with a second .dat (broke), which turned out to be a
            # methodological red herring: ResourceInitializer checks EXISTS
            # for all 8 files before LOADING any of them, so the isolated test
            # never reached the load/validate step at all (a missing
            # critters.lst threw first). Once every file genuinely existed
            # somewhere, the load step consistently preferred the case-mangled
            # override never matching over the archive's own lowercased entry.
            rel = name.replace("\\", "/").lower()
            dest = out_dir / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(cleaned)
            n_bad = sum(1 for a, b in zip(data, cleaned) if a != b)
            print(f"  scrubbed {rel:32s} ({n_bad} byte(s), from {dat_path.name}) -> "
                  f"{dest.relative_to(HERE)}")
            scrubbed += 1

    print(f"\n{install_dir.name}: scanned {scanned} .lst file(s), "
          f"scrubbed {scrubbed}")
    if scrubbed:
        print(f"  clean copies at: {out_dir}")
    else:
        print("  nothing to scrub - this install did not need the fix")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
