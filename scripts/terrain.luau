-- Terrain generator for Gecko's Script Console / gecko-cli.
--
-- Reproduces a shipped map's terrain by borrowing its palette: it fills the floor with that map's
-- dominant ground tile and scatters the scenery that map is actually built from. Works for any
-- biome (desert, cave, town, ...) because the palette comes from the reference map's real data --
-- a proto's PID is its unique id, so nothing is guessed. The whole run is one undo entry (Ctrl+Z).
--
-- Pick the reference with --arg reference=maps/cave0.map; omit it for a random map.
--   gecko-cli map generate --script scripts/terrain.luau --out out.map \
--       --arg reference=maps/desert1.map --arg seed=42 --arg density=300 --data <master.dat>
--
-- `api` is the map, `args` the parameters.

-- ---- parameters (override with --arg key=value) ---------------------------
local VEG_COUNT   = tonumber(args.density) or 250  -- how many decorations to scatter
local SEED        = tonumber(args.seed)            -- set for a reproducible layout; omit for a fresh one
local EDGE_MARGIN = 6                               -- keep scatter this many hexes off the border

-- Grid sizes (see CLAUDE.md): floor/roof tiles are 100x100, hexes are 200x200.
local TILE_W, TILE_H = 100, 100
local HEX_W,  HEX_H  = 200, 200

-- With no --arg seed, leave the host's per-run random seed in place so each Run picks a different
-- reference map and scatter. Pass --arg seed=N (e.g. from gecko-cli) to reproduce an exact layout.
if SEED then
    math.randomseed(SEED)
end

-- ---- choose the reference map ---------------------------------------------
local reference = args.reference
if not reference or reference == "" then
    local maps = api:listMaps()
    if #maps == 0 then
        print("No maps mounted — load Fallout 2 data (master.dat) and try again.")
        return
    end
    reference = maps[math.random(#maps)]
    print("No --arg reference given; picked " .. reference .. " at random.")
end

-- ---- resolve the palette from the reference map ---------------------------
-- Base ground = the reference's most-used floor tile (override with --arg tile=edg5000).
local base
if args.tile then
    base = api:tileId(args.tile)
else
    base = api:mapFloorTiles(reference)[1]
end
if not base or base < 0 then
    print("Couldn't read a floor tile from '" .. reference .. "'. Mount Fallout 2 data and check the reference path.")
    return
end

local scenery = api:mapScenery(reference)

-- ---- 1. fill the floor ----------------------------------------------------
local painted = 0
for i = 0, TILE_W * TILE_H - 1 do
    if api:paintFloor(i, base) then
        painted = painted + 1
    end
end
print(string.format("Filled %d floor tiles from %s.", painted, reference))

-- ---- 2. scatter the reference map's scenery -------------------------------
local placed = 0
if #scenery > 0 then
    for _ = 1, VEG_COUNT do
        local col = math.random(EDGE_MARGIN, HEX_W - 1 - EDGE_MARGIN)
        local row = math.random(EDGE_MARGIN, HEX_H - 1 - EDGE_MARGIN)
        local hex = row * HEX_W + col
        if api:placeProto(scenery[math.random(#scenery)], hex, 0) then
            placed = placed + 1
        end
    end
end
print(string.format("Scattered %d decorations from %s (%d scenery protos).", placed, reference, #scenery))
