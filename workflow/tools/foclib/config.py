"""
config.py - where THIS computer keeps its games. The only place the tools learn that.

Everything machine-specific lives in one file, workflow/config.json, which
`python setup.py` writes for you and which is never committed:

    {
      "fallout2": "C:/Games/Fallout 2",     your Fallout 2 install, with the Restoration
                                            Project Unofficial in mods/. Read-only: it is
                                            only ever COPIED, to make base/.
      "base":     "",                       the working copy the tools read and deploy into.
                                            Empty = workflow/base
      "gecko":    "C:/Tools/gecko/gecko.exe",
      "stamps":   "",                       stamp library. Empty = <repo>/resources/stamps
      "extra_stamps": [],                   more stamp folders (a second library)
      "own_worldmap": false,                true for a total conversion with its own world
                                            map (see own_worldmap() below)
      "games": {                            other games to borrow from, keyed as in
        "sonora": "C:/Games/Fallout Sonora",    tools/sources.json. Any you do not own: leave out.
        "nevada": "C:/Games/Fallout Nevada"
      },
      "extra_paths": []                     more folders the tools may read and write (see
                                            allowed_roots() below)
    }

Paths may use / or \\, ~ and %ENV% variables.

Environment overrides (for tests and portable setups):
    FOC_CONFIG            use this config file instead of workflow/config.json
    GECKO_SETTINGS_DIR    gecko's settings folder (default %LOCALAPPDATA%\\gecko\\gecko)
"""

from __future__ import annotations

import json
import os
from pathlib import Path

KIT = Path(__file__).resolve().parents[2]            # .../workflow
REPO = KIT.parent                                    # the gecko checkout the kit lives in
CONFIG_FILE = Path(os.environ["FOC_CONFIG"]) if os.environ.get("FOC_CONFIG") else KIT / "config.json"


def _path(v) -> Path | None:
    if not v:
        return None
    return Path(os.path.expandvars(os.path.expanduser(str(v))))


def load() -> dict:
    try:
        return json.loads(CONFIG_FILE.read_text(encoding="utf-8-sig"))
    except FileNotFoundError:
        return {}
    except json.JSONDecodeError as e:
        raise SystemExit(f"{CONFIG_FILE} is not valid JSON ({e}).\n"
                         f"Fix it by hand, or delete it and run:  python setup.py")


def save(cfg: dict) -> None:
    CONFIG_FILE.write_text(json.dumps(cfg, indent=2) + "\n", encoding="utf-8")


def fallout2_dir() -> Path | None:
    return _path(load().get("fallout2"))


def base_dir() -> Path:
    """The working copy of Fallout 2 + RPU that the tools read, and deploy into."""
    return _path(load().get("base")) or KIT / "base"


def stamps_dir() -> Path:
    """Root of the stamp library: it contains 'Fallout 2 Stamps', 'Sonora Stamps', ..."""
    configured = _path(load().get("stamps"))
    if configured:
        return configured
    # a git checkout keeps them in resources/stamps; the release zip in stamps/
    for cand in (REPO / "resources" / "stamps", REPO / "stamps"):
        if cand.is_dir():
            return cand
    return REPO / "resources" / "stamps"


def stamp_libraries() -> list[Path]:
    return [stamps_dir()] + [p for p in map(_path, load().get("extra_stamps", [])) if p]


def gecko_exe() -> Path | None:
    p = _path(load().get("gecko"))
    if p and p.is_file():
        return p
    # the release zip has gecko.exe beside workflow/; a source build has it in build/
    for cand in (REPO / "gecko.exe", REPO / "build" / "gecko.exe", REPO / "build" / "Release" / "gecko.exe"):
        if cand.is_file():
            return cand
    return p


def own_worldmap() -> bool:
    """True for a total conversion with its own world map (as the mod these tools were
    written for has): deploy then hides and locks every vanilla town, flattens
    worldmap.txt to match replacement art in mod/data/art/intrface/WRLDMP00..19.FRM, and
    adds its own encounter table. False (the default): Fallout 2's world map, towns and
    encounters stay as they are and your maps are added to it."""
    return bool(load().get("own_worldmap", False))


def gecko_cli() -> Path:
    """gecko-cli.exe, the headless half of gecko (render, generate, reachability).
    It ships in the same folder as gecko.exe."""
    exe = gecko_exe()
    return Path(exe).with_name("gecko-cli.exe") if exe else REPO / "build" / "gecko-cli.exe"


def gecko_settings_dir() -> Path:
    env = os.environ.get("GECKO_SETTINGS_DIR")
    if env:
        return Path(env)
    return Path(os.environ.get("LOCALAPPDATA", str(Path.home() / "AppData" / "Local"))) / "gecko" / "gecko"


def patterns_dir() -> Path:
    """gecko's own stamp folder: stamps saved in the editor land here."""
    return gecko_settings_dir() / "patterns"


def games() -> dict[str, Path]:
    """Other games on this machine, key -> install folder (only those that exist)."""
    out = {}
    for k, v in load().get("games", {}).items():
        p = _path(v)
        if p and p.is_dir():
            out[k] = p
    return out


def allowed_roots() -> list[Path]:
    """The folders the tools may read and write when a path comes from the command line or a
    plan, recipe or stamp list: this kit, the gecko checkout it lives in, the working copy,
    the stamp libraries, gecko's settings folder, the Fallout 2 install, the other games, and
    any folder listed under "extra_paths" in config.json (see foclib/paths.py)."""
    cfg = load()
    roots = [KIT, REPO, base_dir(), gecko_settings_dir(), *stamp_libraries()]
    fo2 = fallout2_dir()
    if fo2:
        roots.append(fo2)
    roots.extend(games().values())
    roots.extend(p for p in map(_path, cfg.get("extra_paths", [])) if p)
    return roots


def apply_sources(srcs: dict[str, dict]) -> dict[str, dict]:
    """Fill tools/sources.json's blank paths from config.json; drop games not installed.

    A tool that asks for a source this machine does not have gets the same "unknown
    source" answer as for a name that does not exist, with the list of those it does.
    """
    cfg = load()
    g = games()
    out: dict[str, dict] = {}
    for name, entry in srcs.items():
        e = dict(entry)
        if name == "fo2":
            p = fallout2_dir()
        elif e.get("derived"):
            # a tree the tools generate: relative to workflow/
            p = KIT / e["path"] if e.get("path") else None
            client = _path(cfg.get("games", {}).get(name + "_client"))
            if client:
                e["client"] = str(client)
        else:
            p = g.get(name)
        if not p:
            continue
        e["path"] = str(p)
        out[name] = e
    return out
