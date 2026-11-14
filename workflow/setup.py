#!/usr/bin/env python3
"""
setup.py - one-time setup for the map workflow. Run it, answer a few questions.

    python setup.py                       interactive: finds your games, asks about the rest
    python setup.py --yes                 take every default, ask nothing
    python setup.py --build-base          also copy Fallout 2 + RPU into workflow/base (1.2 GB)

Everything it decides is written to workflow/config.json, a small text file you can read and
edit. Nothing in any game install is ever written to: games are only read, and the one thing
that is copied (Fallout 2, to make base/) is checked afterwards to be byte-for-byte unchanged.

Flags for scripts / no questions:
    --fallout2 PATH        your Fallout 2 install (the folder with master.dat; RPU in mods/)
    --gecko PATH           gecko.exe
    --base PATH            an existing working copy to use instead of building one
    --game KEY=PATH        another game, repeatable. Keys: fo1 sonora nevada olympus
                           resurrection sotj yesterday nevada_extended
    --no-pip               do not install Python packages
    --no-launchers         do not generate the gecko launchers
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import string
import subprocess
import sys
from pathlib import Path

KIT = Path(__file__).resolve().parent
sys.path.insert(0, str(KIT / "tools"))

from foclib import config  # noqa: E402

REQUIRED_PY = (3, 10)
PACKAGES = {"PIL": "pillow", "numpy": "numpy", "scipy": "scipy"}
RELEASES = "https://github.com/HogLord69/gecko/releases/latest"

# key -> (what it is, folder-name pattern, a file that proves it is that game)
GAMES = {
    "fo1":             ("Fallout 1",                         r"^fallout$|fallout 1\b|fallout1", "master.dat"),
    "sonora":          ("Fallout: Sonora",                   r"sonora", "master.dat"),
    "nevada":          ("Fallout: Nevada",                   r"fallout.?nevada(?!.*mod)", "master.dat"),
    "olympus":         ("Olympus 2207",                      r"olympus", "master.dat"),
    "resurrection":    ("Fallout 1.5: Resurrection",         r"resurrection", "master.dat"),
    "sotj":            ("Fallout: Story of the Jackal",      r"jackal|sotj", "master.dat"),
    "yesterday":       ("Fallout: Yesterday",                r"yesterday", "master.dat"),
    "nevada_extended": ("Nevada Mod (Extended) - needs Nevada", r"nevada.?mod", "Patch000.dat"),
}


def say(m: str = "") -> None:
    print(m, flush=True)


def ask(prompt: str, default: str = "", yes: bool = False) -> str:
    if yes:
        return default
    try:
        a = input(f"{prompt}{f' [{default}]' if default else ''}: ").strip()
    except EOFError:
        return default
    return a.strip('"') or default


def confirm(prompt: str, default: bool, yes: bool) -> bool:
    if yes:
        return default
    try:
        a = input(f"{prompt} [{'Y/n' if default else 'y/N'}]: ").strip().lower()
    except EOFError:
        return False  # no one there to answer: never take that as a yes
    return default if not a else a.startswith("y")


# ----------------------------------------------------------------------------- finding

def drives() -> list[str]:
    return [f"{d}:\\" for d in string.ascii_uppercase if Path(f"{d}:\\").exists()] or ["/"]


def roots() -> list[Path]:
    out = []
    for d in drives():
        for sub in ("", "Games", "GOG Games", "Program Files (x86)\\GOG Galaxy\\Games",
                    "Program Files (x86)\\Steam\\steamapps\\common",
                    "Program Files\\Steam\\steamapps\\common", "Fallout", "Mods"):
            p = Path(d) / sub if sub else Path(d)
            if p.is_dir():
                out.append(p)
    home = Path.home()
    for sub in ("Games", "Downloads", "Desktop"):
        if (home / sub).is_dir():
            out.append(home / sub)
    return out


def folders() -> list[Path]:
    seen, out = set(), []
    for r in roots():
        try:
            for p in r.iterdir():
                if p.is_dir() and str(p).lower() not in seen:
                    seen.add(str(p).lower())
                    out.append(p)
        except OSError:
            pass
    return out


def find_fallout2(all_dirs: list[Path]) -> Path | None:
    cands = [p for p in all_dirs if re.search(r"fallout.?2", p.name, re.I)
             and (p / "master.dat").is_file() and (p / "critter.dat").is_file()]
    # prefer one that already has RPU
    cands.sort(key=lambda p: (not (p / "mods" / "rpu.dat").is_file(), len(str(p))))
    return cands[0] if cands else None


def find_game(key: str, all_dirs: list[Path]) -> Path | None:
    _, pat, proof = GAMES[key]
    for p in all_dirs:
        if re.search(pat, p.name, re.I) and (p / proof).is_file():
            return p
    return None


def find_gecko() -> Path | None:
    repo = config.REPO
    cands = [repo / "gecko.exe", repo / "build" / "gecko.exe", repo / "build" / "Release" / "gecko.exe",
             repo.parent / "gecko" / "gecko.exe"]
    for sub in ("Downloads", "Desktop"):
        d = Path.home() / sub
        if d.is_dir():
            cands += list(d.glob("gecko*/gecko.exe")) + list(d.glob("gecko*/*/gecko.exe"))
    for c in cands:
        if c.is_file():
            return c
    w = shutil.which("gecko")
    return Path(w) if w else None


# ------------------------------------------------------------------------------ python

def check_python(yes: bool, no_pip: bool) -> bool:
    ok = sys.version_info >= REQUIRED_PY
    say(f"  Python {sys.version.split()[0]}: " + ("ok" if ok else
        f"too old - need {REQUIRED_PY[0]}.{REQUIRED_PY[1]}+ (python.org)"))
    if not ok:
        return False
    missing = []
    for mod, pkg in PACKAGES.items():
        try:
            __import__(mod)
        except ImportError:
            missing.append(pkg)
    if not missing:
        say("  packages (pillow, numpy, scipy): ok")
        return True
    say(f"  missing Python packages: {', '.join(missing)}")
    if no_pip:
        say(f"  install them with:  python -m pip install {' '.join(missing)}")
        return False
    if confirm(f"  Install {', '.join(missing)} with pip?", True, yes):
        r = subprocess.run([sys.executable, "-m", "pip", "install", *missing])
        return r.returncode == 0
    return False


# ------------------------------------------------------------------------------- base

def build_base(fo2: Path, base: Path, yes: bool) -> bool:
    if (base / "master.dat").is_file():
        say(f"  working copy already exists at {base} - leaving it alone")
        return True
    say(f"\n  This copies {fo2} into {base} (about 1.2 GB).")
    say("  Your install is only read; the script re-counts it afterwards and stops if a byte moved.")
    if not confirm("  Build the working copy now?", True, yes):
        say("  skipped. Later:  python setup.py --build-base")
        return False
    ps1 = KIT / "tools" / "make-base.ps1"
    src, dst = fo2.resolve(), base.resolve()
    if not src.is_dir():
        say(f"  {src} is not a folder")
        return False
    r = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(ps1),
                        "-Src", str(src), "-Dst", str(dst)])
    return r.returncode == 0 and (base / "master.dat").is_file()


# ------------------------------------------------------------------------------- main

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--yes", "-y", action="store_true", help="take every default, ask nothing")
    ap.add_argument("--build-base", action="store_true")
    ap.add_argument("--fallout2")
    ap.add_argument("--gecko")
    ap.add_argument("--base", help="use an existing working copy instead of workflow/base")
    ap.add_argument("--game", action="append", default=[], metavar="KEY=PATH")
    ap.add_argument("--no-pip", action="store_true")
    ap.add_argument("--no-launchers", action="store_true")
    args = ap.parse_args()
    yes = args.yes

    say("FOC map workflow - setup\n")
    say("1. Python")
    py_ok = check_python(yes, args.no_pip)

    cfg = config.load()
    cfg.setdefault("games", {})
    say("\n2. Looking for your games (this reads folder names only)")
    all_dirs = folders()

    # --- Fallout 2
    fo2 = Path(args.fallout2) if args.fallout2 else (config.fallout2_dir() or find_fallout2(all_dirs))
    if fo2:
        say(f"  Fallout 2: found {fo2}")
    fo2s = ask("  Fallout 2 folder (the one with master.dat)", str(fo2) if fo2 else "", yes or bool(args.fallout2))
    fo2 = Path(fo2s) if fo2s else None
    if fo2 and not (fo2 / "master.dat").is_file():
        say(f"  ! no master.dat in {fo2} - not a Fallout 2 install")
        fo2 = None
    if fo2:
        cfg["fallout2"] = str(fo2)
        if not (fo2 / "mods" / "rpu.dat").is_file():
            say("  ! mods\\rpu.dat is missing. The tools expect the Restoration Project Unofficial\n"
                "    (it carries the 4077-entry tile list and the map fixes). Install it into Fallout 2\n"
                "    first: https://github.com/BGforge/fallout2-rpu  - then run setup again.")
    else:
        say("  Fallout 2 not set. The workflow needs it; everything else is optional.")

    # --- other games
    for kv in args.game:
        k, _, v = kv.partition("=")
        if k not in GAMES:
            raise SystemExit(f"--game {k}: unknown key. Keys: {', '.join(GAMES)}")
        cfg["games"][k] = v
    say("\n  Other games are optional: each one you own gives you a library of maps to borrow from.")
    for key, (label, _, _) in GAMES.items():
        if key in cfg["games"] and Path(cfg["games"][key]).is_dir():
            say(f"  {label}: {cfg['games'][key]}")
            continue
        found = find_game(key, all_dirs)
        if yes:
            if found:
                cfg["games"][key] = str(found)
                say(f"  {label}: found {found}")
            continue
        a = ask(f"  {label}", str(found) if found else "")
        if a and Path(a).is_dir():
            cfg["games"][key] = a
        elif a:
            say(f"    ! {a} is not a folder - skipped")
    cfg["games"] = {k: v for k, v in cfg["games"].items() if v}

    # --- gecko
    say("\n3. gecko (the map editor)")
    g = Path(args.gecko) if args.gecko else config.gecko_exe()
    if not g or not Path(g).is_file():
        g = find_gecko()
    gs = ask("  gecko.exe", str(g) if g else "", yes or bool(args.gecko))
    if gs and Path(gs).is_file():
        cfg["gecko"] = gs
        say(f"  gecko: {gs}")
    else:
        say(f"  gecko.exe not found. Download the zip, unzip it anywhere: {RELEASES}\n"
            f"  then run  python setup.py  again (or put its path in config.json).")

    if args.base:
        cfg["base"] = args.base
    config.save(cfg)
    say(f"\n  wrote {config.CONFIG_FILE}")

    # --- working copy
    if fo2:
        say("\n4. Working copy of the game (workflow/base)")
        base = config.base_dir()
        if (base / "master.dat").is_file():
            say(f"  {base}: ok")
        elif args.build_base:
            build_base(fo2, base, yes)
        elif yes:
            say("  not built (it copies 1.2 GB). When you want it:  python setup.py --build-base")
        else:
            build_base(fo2, base, False)

    # --- launchers
    if not args.no_launchers and (cfg.get("games") or fo2):
        say("\n5. gecko launchers")
        subprocess.run([sys.executable, str(KIT / "tools" / "build_gecko_browsers.py")])

    say("\n6. Check")
    subprocess.run([sys.executable, str(KIT / "foc.py"), "doctor"])
    say("\nDone. Next:  python foc.py        (a menu)   or read README.md")
    return 0 if py_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
