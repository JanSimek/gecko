#!/usr/bin/env python3
"""
port_bulk.py - port every distinct proto of the given types from one or more
sources into FOC's own data in one pass, skipping anything whose ART is
byte-identical to something FOC's data already has or that an earlier source
in this same run already contributed.

    python tools/port_bulk.py --sources yesterday sonora nevada resurrection \
        --types critters scenery walls items --dry-run
    python tools/port_bulk.py --sources yesterday sonora nevada resurrection \
        --types critters scenery walls items --log build/port_bulk.log

Reuses port.py's own cmd_proto (copy + allocate + patch + ledger) so every
port lands exactly the way a manual `python tools/port.py proto <source> <pid>`
call would - same ID ranges, same ledger, same edge-case handling - just
applied to every proto in a source/type at once, gated by an art-hash check
that decides what actually counts as new.

Dedup is by ART BYTES, not by name or proto data: many TCs ship large chunks
of unmodified vanilla Fallout 2 art alongside their own new content, and two
different filenames can be the exact same pixels. Hashing catches both.

Run --dry-run first: it costs nothing, and IDs allocated for real can never be
reordered or deleted once anything references them (see CLAUDE.md's ID space
section), so the real counts should be seen before committing.
"""
from __future__ import annotations

import argparse
import contextlib
import hashlib
import io
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import port  # noqa: E402  (reuses open_source / cmd_proto / ROOT)
from foclib.paths import inside  # noqa: E402
from foclib import GameIndex, TYPE_NAME, parse_lst  # noqa: E402

TYPE_BY_NAME = {v: k for k, v in TYPE_NAME.items()}  # "critters" -> 1, etc.


def sha1_bytes(data: bytes) -> str:
    return hashlib.sha1(data, usedforsecurity=False).hexdigest()


def hash_all_art(vfs, art_dir: str) -> set[str]:
    """Every distinct art hash an install's own <art_dir>.lst names — the
    dedup baseline: anything matching one of these is not new no matter which
    source offers it."""
    hashes: set[str] = set()
    try:
        names = parse_lst(vfs.read(f"art\\{art_dir}\\{art_dir}.lst"))
    except FileNotFoundError:
        return hashes
    for name in names:
        if not name:
            continue
        try:
            hashes.add(sha1_bytes(vfs.read(f"art\\{art_dir}\\{art_filename(art_dir, name)}")))
        except OSError:
            # Loose-file providers raise PermissionError (not FileNotFoundError) when the
            # constructed name resolves to a directory instead of a file, e.g. a malformed
            # critter base-name; either way, this one entry just doesn't resolve.
            continue
    return hashes


def art_filename(tname: str, entry: str) -> str:
    """Every art .lst entry is already a filename except critters (comma-records —
    see port.critter_art_filename, the single source of truth cmd_proto also uses)."""
    return port.critter_art_filename(entry) if tname == "critters" else entry


def candidate_pids(src, tname: str) -> list[tuple[int, str]]:
    """(pid, art_hash) for every proto in this source/type whose art actually
    resolves — skips lines with no proto, no art entry, or unreadable art."""
    t = TYPE_BY_NAME[tname]
    lst = src.proto_lst(t)
    art_dir = tname
    try:
        art_names = parse_lst(src.vfs.read(f"art\\{art_dir}\\{art_dir}.lst"))
    except FileNotFoundError:
        art_names = []

    out: list[tuple[int, str]] = []
    for line_no, fname in enumerate(lst, start=1):
        if not fname:
            continue
        pid = (t << 24) | line_no
        proto = src.proto(pid)
        if proto is None:
            continue
        old_art_idx = proto.fid & 0xFFF
        if not (0 <= old_art_idx < len(art_names)) or not art_names[old_art_idx]:
            continue
        art_name = art_filename(tname, art_names[old_art_idx])
        try:
            art_bytes = src.vfs.read(f"art\\{art_dir}\\{art_name}")
        except OSError:
            continue
        out.append((pid, sha1_bytes(art_bytes)))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--sources", nargs="+", required=True)
    ap.add_argument("--types", nargs="+", default=sorted(TYPE_BY_NAME),
                     choices=sorted(TYPE_BY_NAME))
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--log", help="write cmd_proto's per-item output here instead of stdout")
    args = ap.parse_args()

    # Baseline: what FOC's own data (base + already-deployed mod) already has,
    # per type. pristine=False: we want the FULL "what's already here" view.
    foc = GameIndex(pristine=False)
    seen: dict[str, set[str]] = {t: hash_all_art(foc.vfs, t) for t in args.types}
    for t in args.types:
        print(f"baseline: {len(seen[t])} distinct {t} art hash(es) already in FOC's data")

    wanted: dict[str, list[int]] = {s: [] for s in args.sources}
    totals = {"new": 0, "dup": 0, "unresolved": 0}

    for source in args.sources:
        src = port.open_source(source)
        for tname in args.types:
            cands = candidate_pids(src, tname)
            new_here = 0
            for pid, h in cands:
                if h in seen[tname]:
                    totals["dup"] += 1
                    continue
                seen[tname].add(h)  # dedupe across sources within this run too
                wanted[source].append(pid)
                new_here += 1
            totals["new"] += new_here
            print(f"  {source:14s} {tname:10s} {len(cands):5d} resolved, {new_here:5d} new")

    print(f"\n{'would port' if args.dry_run else 'porting'}: {totals['new']} new item(s), "
          f"{totals['dup']} skipped as duplicate art")

    if args.dry_run:
        return 0

    log_buf = io.StringIO()
    for source, pids in wanted.items():
        if not pids:
            continue
        ns = argparse.Namespace(source=source, pids=[hex(p) for p in pids], notes="bulk import")
        with contextlib.redirect_stdout(log_buf if args.log else sys.stdout):
            port.cmd_proto(ns)

    if args.log:
        log = inside(args.log)
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(log_buf.getvalue(), encoding="utf-8")  # NOSONAR - path checked by foclib.paths.inside()
        print(f"per-item log: {args.log}")

    print("\nRun tools/gen_ids.py, tools/validate.py, then tools/deploy.py next.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
