#!/usr/bin/env python3
"""
foc.py - the front door of the map workflow.

    python foc.py                   a menu
    python foc.py doctor            check that everything is in place, and say how to fix what is not
    python foc.py edit              open gecko on your mod
    python foc.py browse [game]     open gecko on another game, to look and save stamps
    python foc.py tools [word]      list the tools (optionally only those matching a word)
    python foc.py extract <path>    copy a file out of the game's archives, e.g. maps/artemple.map
    python foc.py <tool> [args]     run a tool: `python foc.py render --help`
                                    (shortcuts below, or any file name in tools/ without .py)

Shortcuts: render survey corpus findart plan compose edges exits link port port-stamp
           ids validate deploy worldmap
"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

KIT = Path(__file__).resolve().parent
TOOLS = KIT / "tools"
sys.path.insert(0, str(TOOLS))

from foclib import config  # noqa: E402

ALIASES = {
    "render": "render.py", "survey": "map_survey.py", "corpus": "corpus.py",
    "findart": "findart.py", "plan": "site_plan.py", "compose": "compose_map.py",
    "edges": "map_edges.py", "exits": "exit_layout.py", "link": "map_link.py",
    "port": "port.py", "port-stamp": "port_stamp.py", "ids": "ids.py",
    "validate": "validate.py", "deploy": "deploy.py", "worldmap": "worldmap.py",
}


def run_tool(name: str, args: list[str]) -> int:
    script = ALIASES.get(name, name)
    if not script.endswith(".py"):
        script += ".py"
    path = TOOLS / Path(script).name
    if Path(script).name != script or not path.is_file():
        print(f"No tool called '{name}'. `python foc.py tools` lists them.")
        return 2
    if script in ("validate.py", "deploy.py"):
        # both compare against the header gen_ids writes from mod/ids: keep it current
        r = subprocess.run([sys.executable, str(TOOLS / "gen_ids.py")], cwd=str(KIT),
                           capture_output=True, text=True)
        if r.returncode:
            print(r.stdout + r.stderr)
            return r.returncode
    # the arguments are the tool's own: they follow the script, so Python never reads them
    return subprocess.call([sys.executable, str(path), *args], cwd=str(KIT))  # NOSONAR


# ------------------------------------------------------------------------------ doctor

def doctor() -> int:
    bad = 0

    def ok(m: str) -> None:
        print(f"  [ok] {m}")

    def fail(m: str, fix: str) -> None:
        nonlocal bad
        bad += 1
        print(f"  [!!] {m}\n       fix: {fix}")

    def info(m: str) -> None:
        print(f"  [--] {m}")

    print("Checking the map workflow\n")
    v = sys.version_info
    if v >= (3, 10):
        ok(f"Python {v.major}.{v.minor}")
    else:
        fail(f"Python {v.major}.{v.minor} is too old", "install Python 3.10 or newer from python.org")
    for mod, pkg in (("PIL", "pillow"), ("numpy", "numpy"), ("scipy", "scipy")):
        try:
            __import__(mod)
        except ImportError:
            fail(f"Python package {pkg} missing", f"python -m pip install {pkg}")
    if not config.CONFIG_FILE.is_file():
        fail("no config.json yet", "python setup.py")
        print(f"\n{bad} problem(s).")
        return 1
    ok(f"config: {config.CONFIG_FILE}")

    exe = config.gecko_exe()
    if exe and Path(exe).is_file():
        ok(f"gecko: {exe}")
        if config.gecko_cli().is_file():
            ok("gecko-cli: next to gecko.exe")
        else:
            fail("gecko-cli.exe is not next to gecko.exe (render, exits and edges need it)",
                 "use the release zip, which ships both")
    else:
        fail("gecko.exe not found", "download https://github.com/HogLord69/gecko/releases/latest, "
             "unzip, then python setup.py")

    fo2 = config.fallout2_dir()
    if fo2 and (fo2 / "master.dat").is_file():
        ok(f"Fallout 2: {fo2}")
        if not (fo2 / "mods" / "rpu.dat").is_file():
            fail("Fallout 2 has no mods\\rpu.dat (Restoration Project Unofficial)",
                 "install RPU into it: https://github.com/BGforge/fallout2-rpu")
    else:
        fail("Fallout 2 not set", "python setup.py --fallout2 \"C:\\path\\to\\Fallout 2\"")

    base = config.base_dir()
    if (base / "master.dat").is_file():
        ok(f"working copy: {base}")
        r = subprocess.run([sys.executable, str(TOOLS / "ids.py"), "--check"],
                           capture_output=True, text=True, cwd=str(KIT))
        if r.returncode == 0:
            ok("id space: the base fits below every mod range")
        else:
            fail("id space check failed:\n       " + (r.stdout + r.stderr).strip().replace("\n", "\n       "),
                 "see docs/TROUBLESHOOTING.md, 'ids.py --check fails'")
    else:
        fail(f"no working copy of the game at {base}", "python setup.py --build-base")

    stamps = config.stamps_dir()
    n = sum(1 for _ in stamps.rglob("*.json")) if stamps.is_dir() else 0
    if n:
        ok(f"stamps: {n} in {stamps}")
    else:
        fail(f"no stamps in {stamps}", "this folder ships with the repo; set \"stamps\" in config.json if you moved it")

    games = config.games()
    if games:
        ok("other games: " + ", ".join(sorted(games)))
    else:
        info("no other games set (optional - they are what you borrow buildings from)")
    launchers = sorted(p.stem for p in (TOOLS / "gecko_browse").glob("*.bat"))
    if launchers:
        ok(f"gecko launchers: {len(launchers)}")
    elif games or fo2:
        fail("gecko launchers not generated", "python tools/build_gecko_browsers.py")

    corpus = KIT / "corpus"
    info("fragment corpus: " + ("built" if corpus.is_dir() and any(corpus.iterdir())
                                else "not built (optional: python foc.py corpus index, ~1 min)"))
    print(f"\n{'All good.' if not bad else f'{bad} problem(s) - each line above says how to fix it.'}")
    return 1 if bad else 0


# ------------------------------------------------------------------------------- tools

def list_tools(word: str = "") -> int:
    cat = json.loads((TOOLS / "_catalog.json").read_text(encoding="utf-8"))
    w = word.lower()
    for g in cat:
        rows = [t for t in g["tools"] if not w or w in t["tool"].lower() or w in t["what"].lower()]
        if not rows:
            continue
        print(f"\n{g['group']}\n  {g['blurb']}")
        for t in rows:
            print(f"  {t['tool'][:-3]:24s} {t['what'][:100]}")
    print("\nRun one:  python foc.py <name> --help      Full list: docs/TOOLS.md")
    return 0


def extract(args: list[str]) -> int:
    """Copy a file out of the game's .dat archives (as the game resolves it) to disk."""
    if not args:
        print("usage: python foc.py extract <path inside the game, e.g. maps/artemple.map> [out file]")
        return 2
    from foclib import GameIndex
    inner = args[0].replace("/", "\\")
    out = Path(args[1]) if len(args) > 1 else KIT / "build" / Path(inner.replace("\\", "/")).name
    try:
        data = GameIndex().vfs.read(inner)
    except FileNotFoundError:
        print(f"{args[0]} is not in the game's data (base: {config.base_dir()})")
        return 1
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    print(f"wrote {out} ({len(data)} bytes)")
    return 0


# ------------------------------------------------------------------------------ gecko

def browse(which: str = "") -> int:
    bats = sorted((TOOLS / "gecko_browse").glob("*.bat"))
    if not bats:
        print("No launchers yet. Add games with `python setup.py`.")
        return 1
    pick = None
    if which:
        pick = next((b for b in bats if which.lower() in b.stem.lower()), None)
    if not pick:
        for i, b in enumerate(bats, 1):
            print(f"  {i}  {b.stem}")
        a = input("Which game? ").strip()
        if not a.isdigit() or not 1 <= int(a) <= len(bats):
            return 1
        pick = bats[int(a) - 1]
    return subprocess.call(["cmd", "/c", str(pick)])


def menu() -> int:
    items = [
        ("Set up (find your games, build the working copy)", lambda: subprocess.call([sys.executable, str(KIT / "setup.py")])),
        ("Check that everything works", doctor),
        ("Open gecko on my mod", lambda: run_tool("build_gecko_browsers", ["--edit"])),
        ("Open gecko on another game (look, save stamps)", browse),
        ("List the tools", list_tools),
    ]
    print("FOC map workflow\n")
    for i, (label, _) in enumerate(items, 1):
        print(f"  {i}  {label}")
    print("  q  Quit\n")
    a = input("> ").strip().lower()
    if a.isdigit() and 1 <= int(a) <= len(items):
        return items[int(a) - 1][1]() or 0
    return 0


def main() -> int:
    a = sys.argv[1:]
    if not a:
        return menu()
    cmd, rest = a[0], a[1:]
    if cmd in ("-h", "--help", "help"):
        print(__doc__)
        return 0
    if cmd == "doctor":
        return doctor()
    if cmd == "tools":
        return list_tools(rest[0] if rest else "")
    if cmd == "edit":
        return run_tool("build_gecko_browsers", ["--edit"])
    if cmd == "browse":
        return browse(rest[0] if rest else "")
    if cmd == "extract":
        return extract(rest)
    if cmd == "setup":
        return subprocess.call([sys.executable, str(KIT / "setup.py"), *rest])
    return run_tool(cmd, rest)


if __name__ == "__main__":
    raise SystemExit(main())
