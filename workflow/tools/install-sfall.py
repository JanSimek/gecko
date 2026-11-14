#!/usr/bin/env python3
"""
install-sfall.py - install an upstream sfall release into the working base.

    python tools/install-sfall.py <path-to-sfall_X.Y.7z-or-.zip> [--dry-run]

Upstream sfall is https://sourceforge.net/projects/sfall/files/ (sfall_*.7z).
SourceForge sits behind a bot challenge, so the archive has to be downloaded
by hand; this script takes it from there.

IMPORTANT - this installs UPSTREAM sfall ("sfall Team", 4.x line), which is
what Fallout 2 + RPU targets. It is NOT the same thing as the "sfall 5.x" in
the TC installs on this machine: those are NsFall, a separate Russian fork
(gitverse.ru / nuclear-grot) tuned per-TC. Do not mix them.

What it does:
  1. Extracts the archive to a temp dir.
  2. Backs up the current ddraw.dll + ddraw.ini into base/_sfall_backup/<ver>/.
  3. Installs the new ddraw.dll (and any other payload files it ships).
  4. Merges ddraw.ini: starts from the NEW ini so new keys arrive with their
     defaults, then carries over every key whose value you had customised and
     which still exists upstream. Anything it could not carry is reported, not
     silently dropped.
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASE = ROOT / "base"


# --------------------------------------------------------------------------- ini

# A settings line, live or commented out. sfall ships most options commented
# out at their defaults (";KarmaFRMs=47,48,49"), so a merge that only looks at
# live lines reports half the settings as "missing upstream" and silently drops
# them. The key must be a bare identifier immediately followed by '=', which
# keeps it from matching prose comments like ";Set to 1 to ...".
SETTING_RE = re.compile(r"^(\s*)(;\s*)?([A-Za-z_][A-Za-z0-9_]*)\s*=(.*)$")


def parse_ini(text: str) -> dict[tuple[str, str], tuple[str, bool]]:
    """
    section/key -> (value, is_live).

    is_live is False for a commented-out default. Callers that want "what is
    actually in effect" should filter on it; callers that want "does upstream
    know this key at all" should not.
    """
    out: dict[tuple[str, str], tuple[str, bool]] = {}
    section = ""
    for line in text.splitlines():
        s = line.strip()
        m = re.match(r"\[(.+?)\]", s)
        if m:
            section = m.group(1).strip().lower()
            continue
        m = SETTING_RE.match(line)
        if not m:
            continue
        _, comment, key, value = m.groups()
        k = (section, key.lower())
        # first occurrence in a section wins; don't let later doc examples clobber
        if k not in out:
            out[k] = (value.strip(), comment is None)
    return out


def apply_ini_values(text: str, values: dict[tuple[str, str], str]) -> tuple[str, list]:
    """
    Rewrite `text`, substituting values. Uncomments a commented-out key so the
    carried-over setting actually takes effect. Returns (text, applied).
    """
    applied = []
    done: set[tuple[str, str]] = set()
    section = ""
    lines = text.splitlines(keepends=True)
    for i, line in enumerate(lines):
        s = line.strip()
        m = re.match(r"\[(.+?)\]", s)
        if m:
            section = m.group(1).strip().lower()
            continue
        m = SETTING_RE.match(line)
        if not m:
            continue
        indent, _comment, key, _value = m.groups()
        k = (section, key.lower())
        if k in values and k not in done:
            newline = "\n" if line.endswith("\n") else ""
            lines[i] = f"{indent}{key}={values[k]}{newline}"
            applied.append((section, key, values[k]))
            done.add(k)
    return "".join(lines), applied


def sfall_version(dll: Path) -> str:
    """
    Identify a ddraw.dll: PE FileVersion plus the product it calls itself.

    The product name matters as much as the number. Upstream sfall says
    "sfall, built for Fallout 2 v1.02 US"; the TC builds on this machine say
    "NsFall v5.x", which is a different fork despite the higher number.
    """
    if not dll.exists():
        return "absent"
    blob = dll.read_bytes()

    ver = "?"
    i = blob.find("FileVersion".encode("utf-16le"))
    if i >= 0:
        seg = blob[i + 22:i + 120].decode("utf-16le", "replace")
        m = re.search(r"(\d+[\d.,\s]*\d)", seg)
        if m:
            ver = m.group(1).strip()

    strings = [m.decode("utf-16le", "replace")
               for m in re.findall(rb"(?:[\x20-\x7e]\x00){8,}", blob)]
    product = "unknown"
    for s in strings:
        if "NsFall" in s:
            product = s.strip()
            break
    else:
        for s in strings:
            if "sfall" in s.lower() and "built for" in s.lower():
                product = s.strip()
                break
    return f"{ver} ({product})"


# ----------------------------------------------------------------------- extract

def extract(archive: Path, dest: Path) -> None:
    suf = archive.suffix.lower()
    if suf == ".zip":
        with zipfile.ZipFile(archive) as z:
            z.extractall(dest)
        return
    if suf == ".7z":
        try:
            import py7zr
        except ImportError:
            sys.exit("7z archive needs py7zr:  python -m pip install py7zr")
        with py7zr.SevenZipFile(archive, "r") as z:
            z.extractall(dest)
        return
    sys.exit(f"don't know how to extract {archive.name} (expected .7z or .zip)")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("archive", type=Path)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if not args.archive.exists():
        sys.exit(f"no such file: {args.archive}")
    if not (BASE / "fallout2.exe").exists():
        sys.exit(f"base install not found at {BASE} - run tools/make-base.ps1 first")

    old_dll = BASE / "ddraw.dll"
    old_ini = BASE / "ddraw.ini"
    old_ver = sfall_version(old_dll)
    print(f"current base sfall : {old_ver}")

    if "NsFall" in old_ver:
        print("\n  !! The base is running NsFall, not upstream sfall.")
        print("     Installing upstream over it is probably not what you want.")
        if not args.dry_run:
            sys.exit("     Refusing. Re-run with --dry-run to inspect, or restore the base.")

    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        extract(args.archive, tmp)

        found = {p.name.lower(): p for p in tmp.rglob("*") if p.is_file()}
        new_dll = found.get("ddraw.dll")
        new_ini = found.get("ddraw.ini")
        if new_dll is None:
            sys.exit("archive contains no ddraw.dll - is this really an sfall release?")

        new_ver = sfall_version(new_dll)
        print(f"archive sfall      : {new_ver}")
        print(f"archive payload    : {len(found)} files")

        # what else does it ship that we should carry across?
        extras = [p for n, p in sorted(found.items())
                  if n not in ("ddraw.dll", "ddraw.ini")
                  and p.suffix.lower() in (".dat", ".int", ".dll", ".ini")]
        for p in extras:
            print(f"  extra: {p.relative_to(tmp)}")

        if args.dry_run:
            print("\n--dry-run: nothing written.")
            return 0

        # ---- backup ------------------------------------------------------
        safe = re.sub(r"[^A-Za-z0-9._-]+", "_", old_ver)[:60] or "previous"
        bdir = BASE / "_sfall_backup" / safe
        bdir.mkdir(parents=True, exist_ok=True)
        for f in (old_dll, old_ini):
            if f.exists():
                shutil.copy2(f, bdir / f.name)
        print(f"\nbacked up old sfall -> {bdir}")

        # ---- ini merge ---------------------------------------------------
        if new_ini is not None and old_ini.exists():
            old_text = old_ini.read_text(encoding="cp1252", errors="replace")
            new_text = new_ini.read_text(encoding="cp1252", errors="replace")
            old_vals = parse_ini(old_text)
            new_vals = parse_ini(new_text)

            # Only carry settings that were LIVE in the old ini - a commented
            # default there was never in effect and shouldn't become one now.
            customised = {
                k: v for k, (v, live) in old_vals.items()
                if live and k in new_vals and new_vals[k][0] != v
            }
            dropped = [k for k, (_v, live) in old_vals.items()
                       if live and k not in new_vals]

            merged, applied = apply_ini_values(new_text, customised)
            old_ini.write_text(merged, encoding="cp1252")

            print(f"ddraw.ini          : new upstream ini, {len(applied)} of your "
                  f"settings carried over")
            for sec, k, v in applied[:20]:
                print(f"    kept  [{sec}] {k}={v}")
            if len(applied) > 20:
                print(f"    ... and {len(applied) - 20} more")
            if dropped:
                print(f"  !! {len(dropped)} keys existed in your old ini but not "
                      f"upstream's - NOT carried:")
                for sec, k in dropped[:15]:
                    print(f"    lost  [{sec}] {k} = {old_vals[(sec, k)][0]}")
                if len(dropped) > 15:
                    print(f"    ... and {len(dropped) - 15} more "
                          f"(full old ini is in {bdir})")
        elif new_ini is not None:
            shutil.copy2(new_ini, old_ini)
            print("ddraw.ini          : installed fresh (no previous ini)")

        # ---- dll + extras ------------------------------------------------
        shutil.copy2(new_dll, old_dll)
        print(f"ddraw.dll          : installed ({new_ver})")

        for p in extras:
            rel = p.relative_to(tmp)
            # sfall archives are usually flat; keep any subdir structure they use
            target = BASE / (rel.name if len(rel.parts) == 1 else str(rel))
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.exists():
                shutil.copy2(target, bdir / target.name)
            shutil.copy2(p, target)
            print(f"installed extra    : {target.relative_to(BASE)}")

    print(f"\nDone. Verify in game, or roll back by copying {bdir} back over base\\.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
