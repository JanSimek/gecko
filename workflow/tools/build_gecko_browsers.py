#!/usr/bin/env python3
"""
build_gecko_browsers.py - one gecko launcher per game you own, plus the one you edit with.

    python tools/build_gecko_browsers.py            write the launchers into tools/gecko_browse/
    python tools/build_gecko_browsers.py --edit     point gecko at YOUR mod (base/) and open it
    python tools/build_gecko_browsers.py --profile  same, but do not open gecko
    python foc.py browse                            menu of the launchers (what most people use)

Which games it makes launchers for is decided by workflow/config.json ("games" and
"fallout2"), which `python setup.py` writes. Install a game later, add its folder to
config.json, run this again.

WHY ONE LAUNCHER PER GAME, NOT ONE MERGED VIEW
Gecko keeps exactly one settings.json with one flat list of data paths. Fallout 2, Sonora,
Nevada, Olympus and the rest each ship a full master.dat/critter.dat that reuses Fallout 2's
own file names (art/tiles/edg5000.frm, art/critters/hmjmps*.frm ...) for DIFFERENT pictures.
Mount them together and a name collision silently picks whichever game mounted last: your own
tiles or critters start drawing as some other game's art, with no error. There is no safe
priority order for unrelated games. So each launcher swaps settings.json for one that mounts
ONE game, runs gecko, and puts your editing profile back when the window closes
(tools/gecko_browse/_swap_and_launch.ps1). Stamps you save while browsing are converted to your
mod's ids on the way out (tools/port_stamp.py).

WHY SOME GAMES GET A _clean/<install name> [clean] FOLDER
Several Russian-community total conversions ship .lst files with Cyrillic comments after the
';'. The game ignores them; gecko checks the first 64 bytes of every .lst for plain ASCII and
refuses the file. gecko_browse/clean_lst_for_gecko.py copies each install's .lst files with
the comment bytes blanked (field data is untouched) into _clean/, and the launcher mounts that
folder LAST so its copies win. Nothing in the install is written. The " [clean]" suffix is
load-bearing: gecko silently drops a mounted folder whose last name matches another mounted
folder's, so the clean copy must not share the install's folder name.

WHY _fallback_art/
A tile id outside a game's own tiles.lst needs SOME art (blank.frm) or gecko fails outright
instead of degrading. _fallback_art/ is a copy of the six small files gecko ships in
resources/art/, and nothing else - never a folder that has grown into a full data set.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from foclib import config  # noqa: E402

OUT = HERE / "gecko_browse"
CLEAN_ROOT = OUT / "_clean"
FALLBACK_ART_SRC = config.REPO / "resources" / "art"
FALLBACK_ART_OUT = OUT / "_fallback_art" / "art"

# key (as in tools/sources.json) -> launcher name, label, extra .dat files mounted after
# master.dat/critter.dat (lowest priority first), has its own master.dat/critter.dat,
# game that must sit underneath it, whether stamps saved in it are converted on exit.
GAMES: dict[str, dict] = {
    "fo2":             dict(name="Fallout 2 - vanilla (no RPU)", label="Fallout 2 (vanilla, no RPU)",
                            extra=[], own=True, under=None, convert=False),
    "fo1":             dict(name="Fallout 1", label="Fallout 1",
                            extra=[], own=True, under=None, convert=False),
    "sonora":          dict(name="Fallout Sonora", label="Fallout: Sonora",
                            extra=[], own=True, under=None, convert=True),
    "nevada":          dict(name="Fallout Nevada", label="Fallout: Nevada",
                            extra=[], own=True, under=None, convert=True),
    "olympus":         dict(name="Fallout Olympus 2207", label="Olympus 2207",
                            extra=[], own=True, under=None, convert=True),
    "resurrection":    dict(name="Fallout Resurrection", label="Fallout 1.5: Resurrection",
                            extra=["fores.dat"], own=True, under=None, convert=True),
    "sotj":            dict(name="Fallout Story of the Jackal", label="Fallout: Story of the Jackal",
                            extra=[], own=True, under=None, convert=True),
    "yesterday":       dict(name="Fallout Yesterday", label="Fallout: Yesterday",
                            extra=["patch001.dat", "patch002.dat"], own=True, under=None, convert=True),
    # Nevada Mod (Extended) has no master.dat/critter.dat of its own and is not a standalone
    # game (no color.pal anywhere in its patches): it expects Fallout: Nevada underneath.
    "nevada_extended": dict(name="Nevada Mod Extended", label="Nevada Mod (Extended)",
                            extra=["Patch000.dat", "Patch001.dat"], own=False, under="nevada",
                            convert=True),
}


def say(msg: str = "") -> None:
    print(msg)


# -------------------------------------------------------------------- your editing profile

def foc_data_paths(base: Path) -> list[str]:
    """What gecko mounts to edit YOUR mod: the working copy's archives in the game's load
    order (rpu.dat and the add-ons: without rpu.dat gecko sees master.dat's 3102-entry
    tiles.lst while the game uses RPU's 4077, and a map with a tile id >= 3102 crashes it on
    load), then the base's loose data, then your deployed mod. Only paths that exist: a
    path that does not exist can stop gecko mounting the ones after it."""
    paths = [base / "master.dat", base / "critter.dat"]
    order = base / "mods" / "mods_order.txt"
    if order.is_file():
        for raw in order.read_text(encoding="cp1252", errors="replace").splitlines():
            n = raw.strip()
            if n and not n.startswith((";", "#")) and n.lower() != "foc":
                paths.append(base / "mods" / n)
    paths.append(base)
    paths.append(base / "mods" / "foc")
    return [str(p) for p in paths if p.exists()]


def write_edit_profile() -> Path:
    """Point gecko's settings.json at the working base, keeping everything else in it
    (window layout, colours) as it was."""
    base = config.base_dir()
    if not (base / "master.dat").is_file():
        raise SystemExit(f"No working copy of the game at {base}.\n"
                         f"Run:  python setup.py --build-base")
    sdir = config.gecko_settings_dir()
    sdir.mkdir(parents=True, exist_ok=True)
    sp = sdir / "settings.json"
    try:
        cur = json.loads(sp.read_text(encoding="utf-8-sig"))
        if not isinstance(cur, dict):
            cur = {}
    except (OSError, json.JSONDecodeError):
        cur = {}
    cur.setdefault("gameLocation", {})
    cur.setdefault("selectionColors", {})
    cur.setdefault("textEditor", {"mode": "system"})
    cur.setdefault("ui", {"edgeScrollEnabled": True, "mergeSelectionOutlines": True,
                          "windowMaximized": True})
    cur["version"] = cur.get("version", "1.1")
    cur["dataPaths"] = foc_data_paths(base)
    sp.write_text(json.dumps(cur, indent=2) + "\n", encoding="utf-8")  # NOSONAR - gecko's own settings file, updated in place
    return sp


def gecko_running() -> bool:
    try:
        out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq gecko.exe"],
                             capture_output=True, text=True).stdout
        return "gecko.exe" in out.lower()
    except OSError:
        return False


def edit(launch: bool) -> int:
    if gecko_running():
        say("gecko is already running. Close it first: two copies fight over one settings file.")
        return 1
    sp = write_edit_profile()
    say(f"gecko profile -> your mod's working copy ({sp})")
    if not launch:
        return 0
    exe = config.gecko_exe()
    if not exe or not Path(exe).is_file():
        say("gecko.exe not found. Download a release, or build it, then run:  python setup.py")
        return 1
    subprocess.Popen([str(exe)], cwd=str(Path(exe).parent))
    say("gecko started.")
    return 0


# ------------------------------------------------------------------------ the launchers

def write_launcher(stem: str, label: str, data_paths: list[str], source: str) -> None:
    ps1_lines = ", ".join("'" + p.replace("'", "''") + "'" for p in data_paths)
    (OUT / f"{stem}.ps1").write_text(
        f'# Launches gecko on "{label}" only. Generated by tools/build_gecko_browsers.py -\n'
        f"# regenerate there rather than hand-editing.\n"
        f'& "$PSScriptRoot\\_swap_and_launch.ps1" -Name "{label}" -DataPaths @({ps1_lines})'
        f' -Source "{source}"\n',
        encoding="utf-8")
    (OUT / f"{stem}.bat").write_text(
        "@echo off\r\n"
        f'powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0{stem}.ps1"\r\n',
        encoding="utf-8", newline="")


def rebuild_fallback_art() -> None:
    if FALLBACK_ART_OUT.exists():
        shutil.rmtree(FALLBACK_ART_OUT)
    if not FALLBACK_ART_SRC.is_dir():
        say(f"  note: {FALLBACK_ART_SRC} not found (is this folder inside a gecko checkout?)")
        return
    shutil.copytree(FALLBACK_ART_SRC, FALLBACK_ART_OUT)
    n = sum(1 for p in FALLBACK_ART_OUT.rglob("*") if p.is_file())
    say(f"  _fallback_art/: {n} file(s) from gecko's resources/art/")


def game_root(key: str) -> Path | None:
    if key == "fo2":
        return config.fallout2_dir()
    return config.games().get(key)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--edit", action="store_true", help="point gecko at your mod and open it")
    ap.add_argument("--profile", action="store_true", help="point gecko at your mod, do not open it")
    args = ap.parse_args()
    if args.edit or args.profile:
        return edit(launch=args.edit)

    OUT.mkdir(parents=True, exist_ok=True)
    exe = config.gecko_exe()
    info = {"gecko": str(exe) if exe else "", "base": str(config.base_dir())}
    (OUT / "launch.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    if not exe or not Path(exe).is_file():
        say(f"  note: gecko.exe not found ({exe}); launchers are written anyway - run "
            f"`python setup.py` once it is installed")
    rebuild_fallback_art()

    made = []
    for key, g in GAMES.items():
        root = game_root(key)
        if not root:
            continue
        paths: list[str] = []
        if g["under"]:
            under = game_root(g["under"])
            if not under:
                say(f"  skip {g['name']}: it needs {g['under']} installed underneath it")
                continue
            paths += [str(under / "master.dat"), str(under / "critter.dat")]
        if g["own"]:
            paths += [str(root / "master.dat"), str(root / "critter.dat")]
        paths += [str(root / d) for d in g["extra"]]
        paths.append(str(root))  # the loose data folder, last of the real install paths
        # scrub this install's .lst files if that has not been done
        res = subprocess.run([sys.executable, str(OUT / "clean_lst_for_gecko.py"), str(root)],
                             capture_output=True, text=True)
        for line in res.stdout.splitlines():
            say(f"  {line}")
        clean = CLEAN_ROOT / f"{root.name} [clean]"
        if clean.is_dir() and any(clean.rglob("*.lst")):
            paths.append(str(clean))  # highest priority: shadows only the scrubbed .lst files
        source = key if g["convert"] else ""
        write_launcher(g["name"], g["label"], paths, source)
        made.append(g["name"])

    say(f"\n{len(made)} launcher(s) written to {OUT}")
    for m in made:
        say(f"  {m}")
    if not made:
        say("  none: no games are listed in config.json yet. Run `python setup.py`.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
