#!/usr/bin/env python3
"""
mapedit.py - a prefab map editor for FOC.

    python tools/mapedit.py                      new empty desert map
    python tools/mapedit.py --map mod/data/maps/REDWELL.MAP
    then open http://127.0.0.1:8778

WHY THIS EXISTS
---------------
Splitting the work where it actually splits. The mechanical half - formats, ID
spaces, script binding, the empty-tile sentinel, elevation flags, remapping
protos by art name across installs - is exact, and every trap in it is already
solved and encoded here. The spatial half - where a building goes, whether a
street reads as a street - is judgement, and a person does it in seconds by
looking.

So: the tool holds every rule, and the human does the composing.

Runs locally because the art lives in the .dat files on this machine; a hosted
page could not reach them.
"""

from __future__ import annotations

import argparse
import io
import json
import sys
import threading
import traceback
import hashlib
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

sys.path.insert(0, str(Path(__file__).resolve().parent))

from PIL import Image  # noqa: E402

import render as R  # noqa: E402
from foclib.paths import inside  # noqa: E402
from foclib import (  # noqa: E402
    OBJ_TYPE_CRITTER,
    GameIndex,
    MapFile,
    TYPE_NAME,
    hex_xy,
    parse_lst,
    pid_type,
)
from mapbuild import HEX_W, MapBuilder, MapObjectSpec, extras_for  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
CORPUS = ROOT / "corpus" / "fragments.jsonl"
STAMPS = ROOT / "corpus" / "stamps.json"
BUILDINGS = ROOT / "corpus" / "buildings.jsonl"
MAPS_OUT = ROOT / "mod" / "data" / "maps"

CRITTER_LIVE = (0, 0, 8, 0, 1, 1, -1, 50, 0, 0)   # hp=0 reads as a corpse


# --------------------------------------------------------------------- state

class Editor:
    def __init__(self, gi: GameIndex, map_path: Path | None):
        self.gi = gi
        self.lock = threading.Lock()
        self.map_path = map_path
        self.name = map_path.name if map_path else "NEWMAP.MAP"

        self.art_lists = {t: parse_lst(gi.vfs.read(f"art/{d}/{d}.lst"))
                          for t, d in ((0, "items"), (1, "critters"),
                                       (2, "scenery"), (3, "walls"),
                                       (5, "misc"))}
        self.tiles = parse_lst(gi.vfs.read("art/tiles/tiles.lst"))
        self.tile_id = {}
        for i, n in enumerate(self.tiles):
            if n:
                self.tile_id.setdefault(n.lower(), i)

        # proto filename -> pid, valid for prefabs taken from our own base
        self.proto_pid = {}
        for ot in (0, 1, 2, 3, 5):
            for i, fn in enumerate(gi.proto_lst(ot)):
                if fn:
                    self.proto_pid.setdefault((ot, fn.lower()),
                                              (ot << 24) | (i + 1))

        # art name -> our pid, for anything crossing from another install
        self.pid_by_art = {}
        for ot in (0, 1, 2, 3, 5):
            alist = self.art_lists[ot]
            for i, fn in enumerate(gi.proto_lst(ot)):
                if not fn:
                    continue
                pid = (ot << 24) | (i + 1)
                pr = gi.proto(pid)
                if pr is None:
                    continue
                ai = pr.fid & 0xFFF
                if 0 <= ai < len(alist) and alist[ai]:
                    self.pid_by_art.setdefault((ot, alist[ai].lower()), pid)

        self.mb = MapBuilder(self.name, elevations=(0,), map_id=200)
        self.undo: list = []
        if map_path and map_path.is_file():
            self._load(map_path)
        else:
            self._blank_desert()

        self._png = None
        self._origin = (0, 0)
        self._hexmap = None

    # -- map io ------------------------------------------------------------

    def _load(self, path: Path):
        data = path.read_bytes()
        m = MapFile.parse(data, self.gi.proto)
        self.mb.floors[0] = list(m.floors[0])
        self.mb.roofs[0] = list(m.roofs[0])
        self.mb.start_tile = m.default_tile
        for o in m.objects:
            if o.elevation != 0 or not (0 <= o.tile < 40000):
                continue
            pr = self.gi.proto(o.pid)
            if pr is None:
                continue
            t = pid_type(o.pid)
            extras = (CRITTER_LIVE if t == OBJ_TYPE_CRITTER
                      else extras_for(t, o.pid, pr.subtype))
            self.mb.add(MapObjectSpec(
                pid=o.pid, fid=o.fid, tile=o.tile, elevation=0,
                rotation=o.rotation, frame=o.frame,
                sid=-1, script_index=o.script_index, extras=extras))

    def _blank_desert(self):
        """Ground taken from a real map so the tile variety is human."""
        donor = self.gi.load_map(self.gi.vfs.read("maps/ARVILLAG.MAP"))
        di = donor.elevations.index(0)
        self.mb.floors[0] = list(donor.floors[di])
        self.mb.roofs[0] = [1] * 10000
        self.mb.start_tile = 20100

    # -- rendering ---------------------------------------------------------

    def render(self):
        if self._png is not None:
            return self._png, self._origin

        m = MapFile.parse(self.mb.to_bytes(), self.gi.proto)
        img = R.render_map(self.gi, m, 0, True)

        # recompute the same origin render_map used, so clicks can be inverted
        pts = []
        for sq, tid in enumerate(m.floors[0]):
            if tid > 1:
                x, y = R.square_xy(sq)
                pts.append((x, y))
        for o in m.objects:
            if 0 <= o.tile < 40000:
                x, y = R.hex_xy_screen(o.tile)
                pts.append((x - 100, y - 200))
        minx = min(p[0] for p in pts)
        miny = min(p[1] for p in pts)

        buf = io.BytesIO()
        img.save(buf, "PNG")
        self._png = buf.getvalue()
        self._origin = (minx, miny)
        return self._png, self._origin

    def invalidate(self):
        self._png = None

    def hex_at(self, sx: int, sy: int) -> int:
        """Nearest hex to a click on the rendered image."""
        if self._hexmap is None:
            self._hexmap = [(R.hex_xy_screen(t), t) for t in range(0, 40000)]
        ox, oy = self._origin
        wx, wy = sx + ox, sy + oy
        best, bd = 0, 1 << 60
        for (hx, hy), t in self._hexmap:
            d = (hx + 16 - wx) ** 2 + (hy + 8 - wy) ** 2
            if d < bd:
                bd, best = d, t
        return best

    # -- editing -----------------------------------------------------------

    def place(self, prefab: dict, tile: int):
        """
        Returns (placed, clipped). The origin is nudged so the whole prefab
        lands on the grid: without it, clicking near an edge silently dropped
        most of a building - 275 of 330 objects, with no indication why.
        """
        hx0, hy0 = hex_xy(tile)

        # Refuse to place off the walkable area. Clicking the black surround
        # used to snap the prefab to the grid edge, which looks like a bug and
        # is never what anyone meant.
        sq = (hy0 // 2) * 100 + (hx0 // 2)
        if not (0 <= sq < 10000) or self.mb.floors[0][sq] <= 1:
            return -1, 0, 0

        objs = prefab["objects"]
        if objs:
            mx = max(o["dx"] for o in objs)
            my = max(o["dy"] for o in objs)
            hx0 = max(0, min(hx0, HEX_W - 1 - mx))
            hy0 = max(0, min(hy0, 199 - my))
        sx0, sy0 = hx0 // 2, hy0 // 2
        before = len(self.mb.objects)
        floors_backup = None

        if prefab.get("floor"):
            floors_backup = (list(self.mb.floors[0]), list(self.mb.roofs[0]))
            for ry, row in enumerate(prefab["floor"]):
                for rx, c in enumerate(row):
                    if c < 0:
                        continue
                    tid = self.tile_id.get(prefab["palette"][c].lower())
                    if tid:
                        self.mb.set_floor(0, sx0 + rx, sy0 + ry, tid)
            for ry, row in enumerate(prefab.get("roof", [])):
                for rx, c in enumerate(row):
                    if c < 0:
                        continue
                    tid = self.tile_id.get(prefab["palette"][c].lower())
                    if tid:
                        self.mb.set_roof(0, sx0 + rx, sy0 + ry, tid)

        # Placement never punches holes in the ground (measured: 0 holes), and
        # a prefab floors its whole footprint, so nothing is left black
        # underneath either. What actually looks wrong is the opposite: a
        # prefab dropped past the edge EXTENDS the walkable area with a
        # rectangular bulge sticking out of the terrain's organic outline.
        # Report that, because it is the thing you have to undo.
        outside = 0
        if floors_backup is not None:
            was = floors_backup[0]
            now = self.mb.floors[0]
            outside = sum(1 for sq in range(10000)
                          if was[sq] <= 1 < now[sq])

        placed = 0
        for o in prefab["objects"]:
            t = o.get("t", 2)
            art = (o.get("art") or "").lower()
            # Prefer the stored PID. Prefabs from our own base carry a valid
            # one, and art-name lookup loses objects whose fid encodes a frame
            # or rotation variant that no proto's own fid matches - that dropped
            # 275 of a 330-object building.
            pid = o.get("pid")
            if pid is None or self.gi.proto(pid) is None:
                proto = (o.get("proto") or "").lower()
                pid = self.proto_pid.get((t, proto)) if proto else None
            if pid is None:
                pid = self.pid_by_art.get((t, art))
            if pid is None:
                continue
            pr = self.gi.proto(pid)
            if pr is None:
                continue
            hx, hy = hx0 + o["dx"], hy0 + o["dy"]
            if not (0 <= hx < HEX_W and 0 <= hy < 200):
                continue
            extras = (CRITTER_LIVE if t == OBJ_TYPE_CRITTER
                      else extras_for(t, pid, pr.subtype))
            self.mb.add(MapObjectSpec(
                pid=pid, fid=o.get("fid") or pr.fid,
                tile=hy * HEX_W + hx, elevation=0,
                rotation=o.get("rot") or 0, frame=o.get("frm") or 0,
                extras=extras))
            placed += 1

        self.undo.append(("place", before, floors_backup))
        self.invalidate()
        return placed, len(prefab["objects"]) - placed, outside

    def erase(self, tile: int, radius_hex: int = 3) -> int:
        hx0, hy0 = hex_xy(tile)
        keep, removed = [], []
        for o in self.mb.objects:
            hx, hy = hex_xy(o.tile)
            if abs(hx - hx0) <= radius_hex and abs(hy - hy0) <= radius_hex:
                removed.append(o)
            else:
                keep.append(o)
        if removed:
            self.undo.append(("erase", removed, None))
            self.mb.objects = keep
            self.invalidate()
        return len(removed)

    def undo_last(self) -> str:
        if not self.undo:
            return "nothing to undo"
        kind, payload, floors = self.undo.pop()
        if kind == "place":
            self.mb.objects = self.mb.objects[:payload]
            if floors:
                self.mb.floors[0], self.mb.roofs[0] = floors
        else:
            self.mb.objects.extend(payload)
        self.invalidate()
        return f"undid {kind}"

    def save(self) -> dict:
        MAPS_OUT.mkdir(parents=True, exist_ok=True)
        out = MAPS_OUT / self.name.upper()
        out.write_bytes(self.mb.to_bytes())
        m = MapFile.parse(out.read_bytes(), self.gi.proto)
        floored = sum(1 for v in m.floors[0] if v > 1)
        return {
            "path": str(out.relative_to(ROOT)),
            "objects": len(m.objects),
            "floored": floored,
            "density": round(len(m.objects) / max(1, floored), 2),
            "bytes": out.stat().st_size,
        }


# ------------------------------------------------------------------ prefabs

class Prefabs:
    def __init__(self, gi: GameIndex):
        self.gi = gi
        self.index: list[dict] = []
        self.by_id: dict[str, dict] = {}
        self._thumbs: dict[str, bytes] = {}
        self._render_lock = threading.Lock()
        self._load()

    def _load(self):
        if STAMPS.is_file():
            for name, s in json.loads(STAMPS.read_text(encoding="utf-8")).items():
                rec = {"id": "stamp:" + name, "kind": "stamp",
                       "source": s.get("source", ""), "w": s["hex_w"],
                       "h": s["hex_h"], "objects": s["count"],
                       "tags": [], "tileset": ""}
                self.index.append(rec)
                # Stamps record pid but no object TYPE, while fragments record
                # type but not pid. Normalise here so placing and thumbnailing
                # can treat them identically - without this, ybk tent pieces
                # (walls) were looked up as scenery and silently placed nothing.
                objs = []
                for o in s["objects"]:
                    o = dict(o)
                    if "t" not in o and o.get("pid") is not None:
                        o["t"] = pid_type(o["pid"])
                    objs.append(o)
                self.by_id[rec["id"]] = {"objects": objs, "palette": [],
                                         "floor": None}
        # Whole buildings first: walls, doorway, interior floor, roof and
        # furniture as one unit. The 14x14 corpus windows slice architecture in
        # half, which is why placing them gave random parts.
        if BUILDINGS.is_file():
            with BUILDINGS.open(encoding="utf-8") as f:
                for line in f:
                    b = json.loads(line)
                    # Adjacent buildings share roof edges and merge into one
                    # component, so the biggest "buildings" are really whole
                    # city blocks. Size-class them so a single hut is what you
                    # reach for first and a block is something you choose on
                    # purpose.
                    n = b["roof_squares"]
                    size = "hut" if n < 60 else "house" if n <= 250 else "block"
                    rec = {"id": b["id"], "kind": "building",
                           "source": b["source"], "w": b["size_sq"][0],
                           "h": b["size_sq"][1], "objects": b["object_count"],
                           "tags": b["tags"] + [size], "tileset": b.get("map", ""),
                           "size": size, "roof": n}
                    self.index.append(rec)
                    self.by_id[b["id"]] = b

        if CORPUS.is_file():
            with CORPUS.open(encoding="utf-8") as f:
                for line in f:
                    fr = json.loads(line)
                    if fr["source"] != "fo2":
                        continue          # native only: nothing to port
                    rec = {"id": fr["id"], "kind": fr["kind"],
                           "source": fr["source"], "w": fr["size_sq"][0],
                           "h": fr["size_sq"][1], "objects": fr["object_count"],
                           "tags": fr["tags"], "tileset": fr["tileset"]}
                    self.index.append(rec)
                    self.by_id[fr["id"]] = fr
        order = {"building": 0, "stamp": 1, "interior": 2, "exterior": 3}
        # Within buildings, smallest first: a hut you can actually drop into a
        # new colony beats a 1987-object Klamath block at the top of the list.
        size_order = {"hut": 0, "house": 1, "block": 2}
        self.index.sort(key=lambda r: (
            order.get(r["kind"], 9),
            size_order.get(r.get("size"), 0),
            r.get("roof", 0) if r["kind"] == "building" else -r["objects"],
            -r["objects"]))

    def search(self, q="", kind="", tag="", limit=60):
        out = []
        for r in self.index:
            if q and q.lower() not in r["id"].lower() and q.lower() not in r["tileset"]:
                continue
            if kind and r["kind"] != kind:
                continue
            if tag and tag not in r["tags"]:
                continue
            out.append(r)
            if len(out) >= limit:
                break
        return out

    def thumb(self, pid: str) -> bytes:
        """
        A palette thumbnail, cached on disk so it is rendered once ever.

        This used to render every thumbnail through one shared temp file,
        build/_thumb.png. The server is threaded and the palette asks for
        sixty images at once, so the renders raced: 30 of 40 requests came
        back EMPTY and two pairs of buildings got each other's picture - a
        palette of blank boxes. Hence the lock, the unique temp path, and the
        cache that means the lock is only ever contended on a cold start.
        """
        png = self._thumbs.get(pid)
        if png is not None:
            return png

        cache = THUMBS / (hashlib.sha1(pid.encode(), usedforsecurity=False).hexdigest() + ".png")
        if cache.is_file():
            png = cache.read_bytes()
            self._thumbs[pid] = png
            return png

        fr = self.by_id.get(pid)
        if not fr:
            self._thumbs[pid] = b""
            return b""

        with self._render_lock:            # render.py keeps module-level state
            tmp = THUMBS / f"_tmp{threading.get_ident()}.png"
            try:
                THUMBS.mkdir(parents=True, exist_ok=True)
                if fr.get("floor"):
                    R.render_fragment(fr, tmp)
                else:
                    # A stamp has objects but no tiles, and its `source` is a
                    # description ("fo2 maps/ARVILLAG.MAP") rather than a key
                    # in sources.json. Normalise both.
                    R.render_fragment({"id": pid, "source": "fo2",
                                       "palette": [], "floor": [], "roof": [],
                                       "size_sq": [1, 1],
                                       "object_count": len(fr["objects"]),
                                       "objects": fr["objects"]}, tmp)
                with Image.open(tmp) as im:
                    img = im.convert("RGB")
                img.thumbnail(THUMB_SIZE)
                buf = io.BytesIO()
                img.save(buf, "PNG")
                png = buf.getvalue()
                cache.write_bytes(png)
            except Exception as e:         # never silently serve a blank box
                print(f"  thumb {pid}: {type(e).__name__}: {e}")
                png = b""
            finally:
                tmp.unlink(missing_ok=True)

        self._thumbs[pid] = png
        return png

    def warm(self, limit: int = 400):
        """Fill the thumbnail cache in the background so the palette is
        instant. Cold start pays once; later runs read straight from disk."""
        def run():
            todo = [r["id"] for r in self.index[:limit]]
            miss = [i for i in todo
                    if not (THUMBS / (hashlib.sha1(i.encode(), usedforsecurity=False).hexdigest()
                                      + ".png")).is_file()]
            if not miss:
                return
            print(f"  warming {len(miss)} thumbnails in the background...")
            for i in miss:
                self.thumb(i)
            print(f"  thumbnail cache ready ({len(todo)} prefabs)")
        threading.Thread(target=run, daemon=True).start()


# ------------------------------------------------------------------- server

THUMBS = ROOT / "build" / "thumbs"
THUMB_SIZE = (240, 170)

UI = (Path(__file__).resolve().parent / "mapedit.html")


def make_handler(ed: Editor, pf: Prefabs):
    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def _send(self, code, ctype, body):
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.end_headers()
            self.wfile.write(body)

        def _send_ui(self):
            """The editor page itself: mapedit.html, shipped beside this script."""
            body = UI.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.end_headers()
            self.wfile.write(body)  # NOSONAR - the tool's own page from disk, nothing from the request

        def _json(self, obj, code=200):
            self._send(code, "application/json",
                       json.dumps(obj).encode("utf-8"))

        def do_GET(self):
            u = urlparse(self.path)
            q = parse_qs(u.query)
            try:
                if u.path == "/":
                    self._send_ui()
                elif u.path == "/api/render":
                    png, (ox, oy) = ed.render()
                    self.send_response(200)
                    self.send_header("Content-Type", "image/png")
                    self.send_header("X-Origin-X", str(ox))
                    self.send_header("X-Origin-Y", str(oy))
                    self.send_header("Content-Length", str(len(png)))
                    self.send_header("Cache-Control", "no-store")
                    self.end_headers()
                    self.wfile.write(png)
                elif u.path == "/api/prefabs":
                    self._json(pf.search(q.get("q", [""])[0],
                                         q.get("kind", [""])[0],
                                         q.get("tag", [""])[0],
                                         int(q.get("limit", ["60"])[0])))
                elif u.path == "/api/thumb":
                    self._send(200, "image/png", pf.thumb(q.get("id", [""])[0]))
                elif u.path == "/api/state":
                    floored = sum(1 for v in ed.mb.floors[0] if v > 1)
                    self._json({"name": ed.name,
                                "objects": len(ed.mb.objects),
                                "floored": floored,
                                "density": round(len(ed.mb.objects) /
                                                 max(1, floored), 2),
                                "undo": len(ed.undo)})
                else:
                    self._send(404, "text/plain", b"no")
            except Exception:
                traceback.print_exc()           # the details go to this console, not the page
                self._send(500, "text/plain", b"internal error - see the mapedit console")

        def do_POST(self):
            u = urlparse(self.path)
            n = int(self.headers.get("Content-Length", 0))
            body = json.loads(self.rfile.read(n) or b"{}")
            try:
                with ed.lock:
                    if u.path == "/api/place":
                        tile = ed.hex_at(int(body["x"]), int(body["y"]))
                        fr = pf.by_id.get(body["id"])
                        if not fr:
                            return self._json({"error": "unknown prefab"}, 400)
                        n, clipped, outside = ed.place(fr, tile)
                        if n < 0:
                            return self._json(
                                {"error": "that is off the map - click on the "
                                          "ground", "objects":
                                 len(ed.mb.objects)})
                        self._json({"placed": n, "clipped": clipped,
                                    "outside": outside, "tile": tile,
                                    "objects": len(ed.mb.objects)})
                    elif u.path == "/api/erase":
                        tile = ed.hex_at(int(body["x"]), int(body["y"]))
                        n = ed.erase(tile, int(body.get("r", 3)))
                        self._json({"removed": n,
                                    "objects": len(ed.mb.objects)})
                    elif u.path == "/api/undo":
                        self._json({"msg": ed.undo_last(),
                                    "objects": len(ed.mb.objects)})
                    elif u.path == "/api/save":
                        if body.get("name"):
                            ed.name = body["name"].upper()
                            if not ed.name.endswith(".MAP"):
                                ed.name += ".MAP"
                            ed.mb.name = ed.name
                        self._json(ed.save())
                    else:
                        self._json({"error": "no"}, 404)
            except Exception:
                traceback.print_exc()
                self._json({"error": "internal error - see the mapedit console"}, 500)

    return H


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--map", type=Path)
    ap.add_argument("--port", type=int, default=8778)
    args = ap.parse_args()

    gi = GameIndex()
    ed = Editor(gi, inside(args.map) if args.map else None)
    pf = Prefabs(gi)
    print(f"map      : {ed.name}  ({len(ed.mb.objects)} objects)")
    print(f"prefabs  : {len(pf.index)} available")
    pf.warm()
    print(f"\n  open  http://127.0.0.1:{args.port}\n")
    srv = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(ed, pf))
    try:
        srv.serve_forever()  # NOSONAR - a local tool: the server binds 127.0.0.1 only
    except KeyboardInterrupt:
        print("\nstopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
