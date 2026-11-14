#!/usr/bin/env python3
"""
gridmap.py - compile a data-first map grid (JSON) into a Fallout 2 .map.

    python tools/gridmap.py mod/maps_src/crossroads.grid.json
    python tools/gridmap.py <grid.json> --probe      # just resolve every id
    python tools/gridmap.py <grid.json> --no-render   # generate, skip PNGs

The JSON schema is described in the sibling .design.md. This script does no
map-format work itself: it serialises the grid into a Luau table and emits a
generic interpreter that drives `gecko-cli map generate` through the documented
`api:` surface (reference/gecko/src/scripting/ScriptApiReference.h). Proto and
tile ids are resolved by name at generation time, never hard-coded here - the
one reliable way per HANDOFF 7.11.

Output goes to build/<name>/ only. This does NOT register the map with the
engine (no maps.txt / worldmap / header script); that is a separate step.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
from foclib import config as _config  # noqa: E402
GECKO = _config.gecko_cli()
DATA = ["--data", str(ROOT / "base" / "master.dat"),
        "--data", str(ROOT / "base" / "critter.dat")]


def lua_literal(v) -> str:
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return repr(v)
    if isinstance(v, str):
        return '"' + v.replace("\\", "\\\\").replace('"', '\\"') + '"'
    if isinstance(v, list):
        return "{" + ", ".join(lua_literal(x) for x in v) + "}"
    if isinstance(v, dict):
        parts = []
        for k, val in v.items():
            key = f"[{k}]" if isinstance(k, int) else f'["{k}"]'
            parts.append(f"{key} = {lua_literal(val)}")
        return "{" + ", ".join(parts) + "}"
    if v is None:
        return "nil"
    raise TypeError(type(v))


# The Luau interpreter. DATA (the grid JSON) is prepended by emit_luau().
LUA_ENGINE = r"""
-- ---- helpers -------------------------------------------------------------
local W, H = DATA.grid.width, DATA.grid.height
local AX0, AY0, AX1, AY1 = DATA.authored[1], DATA.authored[2], DATA.authored[3], DATA.authored[4]
local claimed = {}
local function key(c, r) return r * 1000 + c end
local function claim(c, r) claimed[key(c, r)] = true end
local function inAuthored(c, r) return c >= AX0 and c <= AX1 and r >= AY0 and r <= AY1 end
local function free(c, r) return inAuthored(c, r) and not claimed[key(c, r)] end
local function pick(t) return t[api:rngInt(1, #t)] end
local function hexOf(c, r) return api:hexIndex(c * 2, r * 2) end
local function chance(p) return api:rng() < p end

local function paint(c, r, id)
    if c < 0 or c >= W or r < 0 or r >= H then return end
    if id and id >= 0 then api:paintFloorXY(c, r, id) end
end
local function paintRoof(c, r, id)
    if c < 0 or c >= W or r < 0 or r >= H then return end
    if id and id >= 0 then api:paintRoofXY(c, r, id) end
end

local function tileIds(names)
    local out = {}
    for _, n in ipairs(names) do
        local id = api:tileId(n)
        if id >= 0 then out[#out + 1] = id else print("  !! unknown tile " .. n) end
    end
    return out
end
local function protoIds(typ, nums)
    local out = {}
    for _, n in ipairs(nums) do out[#out + 1] = api:proto(typ, n) end
    return out
end

local function putProto(typ, pids, c, r, d, gate)
    if gate and not free(c, r) then return false end
    local ok = api:placeProtoXY(pick(pids), c * 2, r * 2, d or api:rngInt(0, 5))
    if ok and gate then claim(c, r) end
    return ok
end
local function putMisc(n, c, r)
    if c < 0 or c >= W or r < 0 or r >= H then return end
    api:placeProtoXY(api:proto("misc", n), c * 2, r * 2, 0)
end

local function fillZone(rect, baseId, accIds, accCh)
    local x0, y0, x1, y1 = rect[1], rect[2], rect[3], rect[4]
    for r = y0, y1 do
        for c = x0, x1 do
            if #accIds > 0 and chance(accCh) then paint(c, r, pick(accIds))
            else paint(c, r, baseId) end
        end
    end
end

local function zoneByName(name)
    for _, z in ipairs(DATA.tiles.zones) do if z.name == name then return z end end
    return nil
end
local function inRect(c, r, rect) return c >= rect[1] and c <= rect[3] and r >= rect[2] and r <= rect[4] end

api:newMap()
print("gridmap: " .. DATA.display .. "  seed " .. tostring(args.seed))

-- ---- ground -------------------------------------------------------------
do
    local g = DATA.tiles.ground
    fillZone({AX0, AY0, AX1, AY1}, api:tileId(g.base), tileIds(g.accents), g.accent_chance)
    print("  ground filled " .. (AX1 - AX0 + 1) .. "x" .. (AY1 - AY0 + 1))
end

-- ---- tile zones -------------------------------------------------------
for _, z in ipairs(DATA.tiles.zones) do
    fillZone(z.rect, api:tileId(z.base), tileIds(z.accents), z.accent_chance)
end
print("  " .. #DATA.tiles.zones .. " tile zones painted")

-- ---- curbs around roads --------------------------------------------------
if DATA.tiles.curbs then
    local cids = tileIds(DATA.tiles.curbs.tiles)
    for _, zn in ipairs(DATA.tiles.curbs.around) do
        local z = zoneByName(zn)
        if z then
            local x0, y0, x1, y1 = z.rect[1], z.rect[2], z.rect[3], z.rect[4]
            for c = x0 - 1, x1 + 1 do paint(c, y0 - 1, pick(cids)) paint(c, y1 + 1, pick(cids)) end
            for r = y0 - 1, y1 + 1 do paint(x0 - 1, r, pick(cids)) paint(x1 + 1, r, pick(cids)) end
        end
    end
    print("  curbs placed")
end

-- ---- roofs ------------------------------------------------------------
for _, rf in ipairs(DATA.roofs or {}) do
    local ids = tileIds(rf.tiles)
    for r = rf.rect[2], rf.rect[4] do
        for c = rf.rect[1], rf.rect[3] do paintRoof(c, r, pick(ids)) end
    end
end

-- ---- structures (broken-rectangle wall outlines) -----------------------
local function isGap(side, c, r, x0, y0, x1, y1, gaps)
    for _, g in ipairs(gaps) do
        local gs, gv = g[1], g[2]
        if gs == side then
            if (side == "N" and r == y0 and c == gv) then return true end
            if (side == "S" and r == y1 and c == gv) then return true end
            if (side == "W" and c == x0 and r == gv) then return true end
            if (side == "E" and c == x1 and r == gv) then return true end
        end
    end
    return false
end

for _, s in ipairs(DATA.structures or {}) do
    local x0, y0, x1, y1 = s.rect[1], s.rect[2], s.rect[3], s.rect[4]
    local wpids = protoIds(s.wall.type, s.wall.numbers)
    local cpids = s.cap and protoIds(s.cap.type, s.cap.numbers) or nil
    local jit = s.jitter or 3
    local n, wallhexes = 0, {}
    local function wallAt(c, r, vertical)
        if isGap(nil, c, r, x0, y0, x1, y1, {}) then end
        if api:placeProtoXY(pick(wpids), c * 2, r * 2, vertical and 1 or 0) then
            n = n + 1; wallhexes[#wallhexes + 1] = {c, r}; claim(c, r)
        end
    end
    -- top/bottom runs
    for c = x0, x1 do
        if (c - x0) % (jit + 1) ~= jit then
            if not isGap("N", c, y0, x0, y0, x1, y1, s.gaps) then wallAt(c, y0, false) end
            if not isGap("S", c, y1, x0, y0, x1, y1, s.gaps) then wallAt(c, y1, false) end
        end
        if cpids and chance(s.cap_chance or 0.4) then
            api:placeProtoXY(pick(cpids), c * 2, (y0 - 1) * 2, 0)
        end
    end
    -- left/right runs
    for r = y0 + 1, y1 - 1 do
        if (r - y0) % (jit + 1) ~= jit then
            if not isGap("W", x0, r, x0, y0, x1, y1, s.gaps) then wallAt(x0, r, true) end
            if not isGap("E", x1, r, x0, y0, x1, y1, s.gaps) then wallAt(x1, r, true) end
        end
    end
    -- interior partitions
    for p = 1, (s.partitions or 0) do
        local py = y0 + math.floor((y1 - y0) * p / ((s.partitions or 0) + 1))
        for c = x0 + 1, x1 - 1 do
            if (c - x0) % 4 ~= 2 then wallAt(c, py, false) end
        end
    end
    -- solid-hex blockers on every wall hex
    if s.blocker_misc then
        for _, wh in ipairs(wallhexes) do putMisc(s.blocker_misc, wh[1], wh[2]) end
    end
    print(string.format("  structure %s: %d wall segments", s.name, n))
end

-- ---- objects ----------------------------------------------------------
for _, o in ipairs(DATA.objects or {}) do
    local pids = protoIds(o.type, o.numbers)
    local placed = 0
    if o.at then
        for _, xy in ipairs(o.at) do
            if putProto(o.type, pids, xy[1], xy[2], o.dir, false) then
                claim(xy[1], xy[2]); placed = placed + 1
            end
        end
    elseif o.rows then
        local rc = o.rows.rect
        for r = rc[2], rc[4], (o.rows.row_step or 2) do
            for c = rc[1], rc[3] do
                if free(c, r) and chance(o.rows.chance or 0.8) then
                    if putProto(o.type, pids, c, r, 0, true) then placed = placed + 1 end
                end
            end
        end
    elseif o.scatter_edges then
        local e = o.scatter_edges
        local step = o.step or 3
        for c = e[1], e[3], step do
            putProto(o.type, pids, c, e[2], 0, true)
            putProto(o.type, pids, c, e[4], 0, true)
            placed = placed + 2
        end
        for r = e[2], e[4], step do
            putProto(o.type, pids, e[1], r, 1, true)
            putProto(o.type, pids, e[3], r, 1, true)
            placed = placed + 2
        end
    elseif o.region then
        local rg = o.region
        for _ = 1, (o.count or 1) * 4 do
            if placed >= (o.count or 1) then break end
            local c = api:rngInt(rg[1], rg[3])
            local r = api:rngInt(rg[2], rg[4])
            if putProto(o.type, pids, c, r, o.dir, true) then placed = placed + 1 end
        end
    end
    print(string.format("  obj %-16s %d", o.group, placed))
end

-- ---- scatter --------------------------------------------------------
if DATA.scatter then
    local sc = DATA.scatter
    local excl = {}
    for _, zn in ipairs(sc.exclude_zones or {}) do
        local z = zoneByName(zn); if z then excl[#excl + 1] = z.rect end
    end
    local rules = {}
    for _, ru in ipairs(sc.rules) do
        rules[#rules + 1] = { pids = protoIds("scenery", ru.numbers), ch = ru.chance }
    end
    local total = 0
    for r = AY0 + 1, AY1 - 1 do
        for c = AX0 + 1, AX1 - 1 do
            if free(c, r) then
                local blocked = false
                for _, rect in ipairs(excl) do if inRect(c, r, rect) then blocked = true break end end
                if not blocked then
                    for _, ru in ipairs(rules) do
                        if chance(ru.ch) then
                            if api:placeProtoXY(pick(ru.pids), c * 2, r * 2, api:rngInt(0, 5)) then
                                claim(c, r); total = total + 1
                            end
                            break
                        end
                    end
                end
            end
        end
    end
    print("  scatter " .. total)
end

-- ---- perimeter -------------------------------------------------------
if DATA.perimeter then
    local p = DATA.perimeter
    local wpids = protoIds("wall", p.wall_numbers)
    local cx = math.floor((AX0 + AX1) / 2)
    local cy = math.floor((AY0 + AY1) / 2)
    local rg = p.road_gap or 3
    local rim = 0
    for c = AX0, AX1, (p.step or 2) do
        if math.abs(c - cx) > rg then
            if putProto("wall", wpids, c, AY0, 0, true) then rim = rim + 1 end
            if putProto("wall", wpids, c, AY1, 0, true) then rim = rim + 1 end
        end
    end
    for r = AY0, AY1, (p.step or 2) do
        if math.abs(r - cy) > rg then
            if putProto("wall", wpids, AX0, r, 1, true) then rim = rim + 1 end
            if putProto("wall", wpids, AX1, r, 1, true) then rim = rim + 1 end
        end
    end
    local tow = tileIds(p.tower_tiles or {})
    if #tow > 0 then
        for _, w in ipairs({ {AX0 + 1, AY0 + 1}, {AX1 - 1, AY0 + 1}, {AX0 + 1, AY1 - 1}, {AX1 - 1, AY1 - 1} }) do
            for dy = 0, 1 do for dx = 0, 1 do
                paint(w[1] + dx, w[2] + dy, pick(tow))
                putMisc(DATA.blockers.wall_blocker_misc or 620, w[1] + dx, w[2] + dy)
            end end
        end
    end
    local dpids = protoIds("scenery", p.debris_numbers)
    local deb = 0
    for _ = 1, (p.debris_count or 0) * 3 do
        if deb >= (p.debris_count or 0) then break end
        local c = api:rngInt(AX0, AX1)
        local r = api:rngInt(AY0, AY1)
        if (c < AX0 + 8 or c > AX1 - 8 or r < AY0 + 8 or r > AY1 - 8) and free(c, r) then
            if api:placeProtoXY(pick(dpids), c * 2, r * 2, api:rngInt(0, 5)) then
                claim(c, r); deb = deb + 1
            end
        end
    end
    print(string.format("  perimeter: rim %d, debris %d", rim, deb))
end

-- ---- scroll blockers ----------------------------------------------------
if DATA.blockers and DATA.blockers.scroll_ring then
    for c = 0, W - 1 do putMisc(12, c, 0) putMisc(12, c, H - 1) end
    for r = 1, H - 2 do putMisc(12, 0, r) putMisc(12, W - 1, r) end
    for c = AX0 - 1, AX1 + 1 do putMisc(12, c, AY0 - 1) putMisc(12, c, AY1 + 1) end
    for r = AY0 - 1, AY1 + 1 do putMisc(12, AX0 - 1, r) putMisc(12, AX1 + 1, r) end
    print("  scroll ring placed")
end

-- ---- exits ----------------------------------------------------------
local exits = 0
for _, ex in ipairs(DATA.exits or {}) do
    for _, xy in ipairs(ex.at) do
        if api:placeExitGrid(hexOf(xy[1], xy[2]), ex.dest, ex.dest_hex or 0, ex.dest_elev or 0, ex.orient or 0) then
            exits = exits + 1
        end
    end
end
print("  exits " .. exits)

-- ---- player start -------------------------------------------------------
local ps = DATA.player_start
api:setPlayerStart(hexOf(ps.at[1], ps.at[2]), ps.orient or 0, 0)
print("  player start (" .. ps.at[1] .. "," .. ps.at[2] .. ")")
print("gridmap: done")
"""

PROBE_LUA = r"""
local function tryTile(n)
    local id = api:tileId(n)
    print(string.format("  tile  %-10s -> %s", n, id >= 0 and tostring(id) or "MISSING"))
end
local function tryProto(typ, n)
    local pid = api:proto(typ, n)
    print(string.format("  %-8s %-5s -> %s", typ, tostring(n), api:protoName(pid)))
end
local seenT, seenP = {}, {}
local function walkTiles(t)
    if type(t) ~= "table" then return end
    for k, v in pairs(t) do
        if type(v) == "string" and (k == "base" or type(k) == "number") then
            -- heuristic: looks like a tile name if resolvable
        end
        walkTiles(v)
    end
end
-- explicit lists from DATA
for _, z in ipairs(DATA.tiles.zones) do
    if not seenT[z.base] then seenT[z.base] = true tryTile(z.base) end
    for _, a in ipairs(z.accents) do if not seenT[a] then seenT[a] = true tryTile(a) end end
end
for _, n in ipairs(DATA.tiles.ground.accents) do if not seenT[n] then seenT[n] = true tryTile(n) end end
if not seenT[DATA.tiles.ground.base] then tryTile(DATA.tiles.ground.base) end
for _, rf in ipairs(DATA.roofs or {}) do for _, n in ipairs(rf.tiles) do if not seenT[n] then seenT[n] = true tryTile(n) end end end
if DATA.tiles.curbs then for _, n in ipairs(DATA.tiles.curbs.tiles) do if not seenT[n] then seenT[n] = true tryTile(n) end end end
for _, p in ipairs(DATA.perimeter and DATA.perimeter.tower_tiles or {}) do if not seenT[p] then seenT[p] = true tryTile(p) end end
local function proKey(t, n) return t .. ":" .. tostring(n) end
for _, s in ipairs(DATA.structures or {}) do
    for _, n in ipairs(s.wall.numbers) do if not seenP[proKey(s.wall.type,n)] then seenP[proKey(s.wall.type,n)]=true tryProto(s.wall.type, n) end end
    if s.cap then for _, n in ipairs(s.cap.numbers) do if not seenP[proKey(s.cap.type,n)] then seenP[proKey(s.cap.type,n)]=true tryProto(s.cap.type, n) end end end
end
for _, o in ipairs(DATA.objects or {}) do
    for _, n in ipairs(o.numbers) do if not seenP[proKey(o.type,n)] then seenP[proKey(o.type,n)]=true tryProto(o.type, n) end end
end
for _, ru in ipairs(DATA.scatter and DATA.scatter.rules or {}) do
    for _, n in ipairs(ru.numbers) do if not seenP[proKey("scenery",n)] then seenP[proKey("scenery",n)]=true tryProto("scenery", n) end end
end
for _, n in ipairs(DATA.perimeter and DATA.perimeter.wall_numbers or {}) do tryProto("wall", n) end
for _, n in ipairs(DATA.perimeter and DATA.perimeter.debris_numbers or {}) do if not seenP[proKey("scenery",n)] then seenP[proKey("scenery",n)]=true tryProto("scenery", n) end end
print("probe: done")
"""


def emit_luau(grid: dict, body: str) -> str:
    return f"local DATA = {lua_literal(grid)}\n\n{body}"


def run(cmd: list[str]) -> None:
    print("$", " ".join(str(c) for c in cmd))
    r = subprocess.run([str(c) for c in cmd], cwd=str(ROOT))
    if r.returncode != 0:
        sys.exit(f"command failed ({r.returncode})")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("grid", type=Path)
    ap.add_argument("--probe", action="store_true", help="only resolve every id in the grid")
    ap.add_argument("--no-render", action="store_true")
    ap.add_argument("--seed", type=int)
    args = ap.parse_args()

    grid = json.loads(args.grid.read_text())
    if args.seed is not None:
        grid["seed"] = args.seed
    name = grid["name"].upper()
    outdir = ROOT / "build" / name.lower()
    outdir.mkdir(parents=True, exist_ok=True)

    luau_path = outdir / f"{name.lower()}.gen.luau"
    body = PROBE_LUA if args.probe else LUA_ENGINE
    luau_path.write_text(emit_luau(grid, body))
    print(f"wrote {luau_path.relative_to(ROOT)}")

    if not GECKO.exists():
        sys.exit(f"gecko-cli not found at {GECKO}")

    gen = [GECKO, "map", "generate", "--script", luau_path,
           "--arg", f"seed={grid.get('seed', 1)}", *DATA]
    if args.probe:
        gen += ["--out", outdir / "_probe.map"]
        run(gen)
        return

    map_path = outdir / f"{name}.MAP"
    gen += ["--out", map_path]
    run(gen)

    # verification
    run([GECKO, "map", "reachability", "--map", map_path, *DATA])
    if not args.no_render:
        for style, flag in (("natural", None), ("schematic", "--schematic"),
                            ("semantic", "--semantic")):
            cmd = [GECKO, "map", "render", "--map", map_path,
                   "--out", outdir / f"{style}.png", "--full", "--max-dim", "1800", *DATA]
            if flag:
                cmd.append(flag)
            run(cmd)
    print(f"\nmap + views in {outdir.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
