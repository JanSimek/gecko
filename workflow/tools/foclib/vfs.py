r"""
The game's virtual filesystem: resolve a game-relative path the way the engine
does, through the whole override chain.

Load order is not guesswork. sfall documents it in ddraw_CheckAddr.ini:

    master_patches > critter_patches > mods_order.txt > PatchFileXX
        > patchXXX.dat > sfall.dat > critter_dat > f2_res_patches
        > f2_res_dat > master_dat

(highest priority first). And LoadOrder.cpp carries the comment

    // Patches placed at the back of the vector will have priority in the
    // chain over the front(previous) patches

so within mods_order.txt, LATER lines win. Lines starting with ';' or '#'
are disabled.

`master_patches` / `critter_patches` are the loose data folders named in
fallout2.cfg — normally `data`, i.e. <base>\data\.

This module answers three questions the validator needs:
  - does this game path exist at all?
  - which provider supplies it? (so we can say where a proto really came from)
  - what are its bytes?
"""

from __future__ import annotations

import re
from pathlib import Path

from .dat1 import Dat1, Dat1Error
from .dat2 import Dat2, Dat2Error, norm


def open_dat(p: Path):
    """A .dat archive: Fallout 2's DAT2, or failing that Fallout 1's DAT1."""
    try:
        return Dat2(p)
    except Dat2Error:
        return Dat1(p)


class Provider:
    """One layer of the chain."""

    def __init__(self, label: str, kind: str):
        self.label = label
        self.kind = kind  # 'dir' | 'dat'

    def has(self, path: str) -> bool:
        raise NotImplementedError

    def read(self, path: str) -> bytes:
        raise NotImplementedError


class DirProvider(Provider):
    def __init__(self, root: Path, label: str):
        super().__init__(label, "dir")
        self.root = root
        # Windows is case-insensitive so a plain path probe is enough here.

    def has(self, path: str) -> bool:
        return (self.root / path.replace("\\", "/")).is_file()

    def read(self, path: str) -> bytes:
        return (self.root / path.replace("\\", "/")).read_bytes()


class DatProvider(Provider):
    def __init__(self, dat: Dat2, label: str):
        super().__init__(label, "dat")
        self.dat = dat

    def has(self, path: str) -> bool:
        return path in self.dat

    def read(self, path: str) -> bytes:
        return self.dat.read(path)


class GameVFS:
    """
    Resolution over a base install directory.

    providers[0] is highest priority.
    """

    def __init__(self, base: Path, extra_dirs: list[Path] | None = None,
                 exclude: list[str] | None = None):
        """
        exclude: provider labels containing any of these substrings are left
        out of the chain. Used to build a PRISTINE view of the base that
        ignores the mod's own deployed layer - otherwise reading the ID
        baseline after a deploy counts our own additions as 'base' and every
        subsequent allocation is off.
        """
        self.base = Path(base)
        self.exclude = [e.lower() for e in (exclude or [])]
        self.providers: list[Provider] = []
        self._cache: dict[str, Provider | None] = {}

        # --- highest: any extra loose dirs the caller wants layered on top
        for d in (extra_dirs or []):
            if d.is_dir():
                self._add(DirProvider(d, f"loose:{d}"))

        # --- master_patches / critter_patches (loose data folder)
        for name in self._patch_dirs():
            d = self.base / name
            if d.is_dir():
                self._add(DirProvider(d, f"loose:{name}\\"))

        # --- mods_order.txt, LAST line first
        mods = self.base / "mods"
        order_file = mods / "mods_order.txt"
        if order_file.is_file():
            lines = order_file.read_text(encoding="cp1252", errors="replace").splitlines()
            entries = []
            for raw in lines:
                s = raw.strip()
                if not s or s.startswith((";", "#")):
                    continue
                entries.append(s)
            for name in reversed(entries):
                p = mods / name
                if p.is_dir():
                    self._add(DirProvider(p, f"mods\\{name}\\"))
                elif p.is_file():
                    try:
                        self._add(DatProvider(Dat2(p), f"mods\\{name}"))
                    except Dat2Error:
                        pass

        # --- patchXXX.dat, highest number first
        #
        # These are not always archives. Fallout Yesterday ships patch001.dat
        # as an extracted FOLDER of the same name, and the engine reads either,
        # so probe before assuming.
        patches = []
        for p in sorted(self.base.glob("patch*.dat")):
            m = re.match(r"patch0*(\d+)\.dat$", p.name, re.I)
            if m:
                patches.append((int(m.group(1)), p))
        for _, p in sorted(patches, reverse=True):
            if p.is_dir():
                self._add(DirProvider(p, p.name + "\\"))
                continue
            try:
                self._add(DatProvider(Dat2(p), p.name))
            except (Dat2Error, OSError):
                pass

        # --- any other root archive we have not already mounted.
        #
        # Not every install uses the vanilla names: FOnline packs everything
        # into fallout.dat. Lowest priority, since these are asset dumps rather
        # than the game's own data.
        known = {"sfall.dat", "critter.dat", "master.dat"}
        others = sorted(
            q for q in self.base.glob("*.dat")
            if q.is_file() and q.name.lower() not in known
            and not re.match(r"patch0*\d+\.dat$", q.name, re.I)
        )

        # --- sfall.dat, critter.dat, master.dat
        for name in ("sfall.dat", "critter.dat", "master.dat"):
            p = self.base / name
            if p.is_dir():
                self._add(DirProvider(p, name + "\\"))
                continue
            if p.is_file():
                try:
                    self._add(DatProvider(open_dat(p), name))
                except (Dat2Error, Dat1Error, OSError):
                    pass

        for q in others:
            try:
                self._add(DatProvider(Dat2(q), q.name))
            except (Dat2Error, OSError):
                pass

    def _add(self, prov: Provider) -> None:
        low = prov.label.lower()
        if any(e in low for e in self.exclude):
            return
        self.providers.append(prov)

    def _patch_dirs(self) -> list[str]:
        """master_patches / critter_patches from fallout2.cfg (default 'data')."""
        cfg = self.base / "fallout2.cfg"
        names: list[str] = []
        if cfg.is_file():
            txt = cfg.read_text(encoding="cp1252", errors="replace")
            for key in ("master_patches", "critter_patches"):
                m = re.search(rf"^\s*{key}\s*=\s*(.+)$", txt, re.I | re.M)
                if m:
                    v = m.group(1).strip()
                    if v and v not in names:
                        names.append(v)
        if not names:
            names = ["data"]
        return names

    # -- interface ---------------------------------------------------------

    def provider_for(self, path: str) -> Provider | None:
        key = norm(path)
        if key in self._cache:
            return self._cache[key]
        found = None
        for prov in self.providers:
            if prov.has(path):
                found = prov
                break
        self._cache[key] = found
        return found

    def exists(self, path: str) -> bool:
        return self.provider_for(path) is not None

    def read(self, path: str) -> bytes:
        prov = self.provider_for(path)
        if prov is None:
            raise FileNotFoundError(path)
        return prov.read(path)

    def read_text(self, path: str) -> str:
        return self.read(path).decode("cp1252", "replace")

    def describe(self) -> str:
        return "\n".join(
            f"  {i:2d}. [{p.kind:5s}] {p.label}" for i, p in enumerate(self.providers)
        )
