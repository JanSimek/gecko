"""
paths.py - keep the tools to the folders this kit works in.

A path given on the command line, or read from a plan, recipe or stamp list, goes
through inside() before anything reads or writes it: it is resolved (symlinks and
.. included) and accepted only when it is one of config.allowed_roots() or lies
under one, compared with the separator after the folder so that a sibling such
as base-old is not taken for base. A map name that the tools join onto a maps
folder goes through bare_name(), so it cannot climb out of it.

To let the tools use another folder, list it under "extra_paths" in config.json.
"""

from __future__ import annotations

import os
from pathlib import Path

from . import config


def inside(path: str | os.PathLike) -> Path:
    """`path` resolved, if it is one of the kit's folders or lies under one; otherwise stop."""
    real = os.path.realpath(path)
    for root in config.allowed_roots():
        base = os.path.realpath(root)
        if real == base or real.startswith(os.path.join(base, "")):
            return Path(real)
    raise SystemExit(f"{path}: outside the folders these tools work in.\n"
                     f"To allow it, add its folder to \"extra_paths\" in {config.CONFIG_FILE}")


def bare_name(name: str) -> str:
    """A file name with no folder part (MAP.MAP, not ../MAP.MAP); otherwise stop."""
    base = os.path.basename(name)
    if not base or base != name or base in (".", ".."):
        raise SystemExit(f"{name!r}: give a file name only, without a folder")
    return base
