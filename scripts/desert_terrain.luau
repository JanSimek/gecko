-- Desert terrain generator for Gecko's Script Console.
--
-- Fills the current elevation's floor with the Fallout 2 wasteland tileset and scatters the
-- scenery a real desert map uses. The whole run collapses into a single undo entry (Ctrl+Z).
--
-- How to use:
--   * Console: open the Script Console, paste this in, press Run (Ctrl+Return).
--   * Headless: gecko-cli map generate --script scripts/desert_terrain.luau --out out.map \
--                   --arg seed=42 --arg density=300 --data <master.dat>
--
-- The vegetation palette is taken straight from a reference map via api:mapScenery — the exact,
-- unique scenery PIDs that map is built from — so the result scatters the same plants and rocks
-- a shipped desert map does, with no guessing or hardcoded ids. `api` is the map, `args` the
-- parameters.

-- ---- parameters (override with --arg key=value, or edit here) --------------
local REFERENCE = args.reference or "maps/desert1.map"   -- map to borrow the scenery palette from
local BASE_TILE = args.tile or "edg5000"                 -- dominant wasteland sand
local VEG_COUNT = tonumber(args.density) or 250          -- how many decorations to scatter
local SEED      = tonumber(args.seed) or 1               -- change for a different layout
local EDGE_MARGIN = 6                                     -- keep scatter this many hexes off the border

-- Grid sizes (see CLAUDE.md): floor/roof tiles are 100x100, hexes are 200x200.
local TILE_W, TILE_H = 100, 100
local HEX_W,  HEX_H  = 200, 200

-- ---- resolve the palette from engine data ---------------------------------
local base = api:tileId(BASE_TILE)
if base < 0 then
    print("Can't resolve '" .. BASE_TILE .. ".frm'. Mount Fallout 2 data (master.dat) and try again.")
    return
end

local palette = api:mapScenery(REFERENCE)
if #palette == 0 then
    print("No scenery in '" .. REFERENCE .. "'. Mount Fallout 2 data and check the --arg reference path.")
    return
end

-- ---- 1. fill the floor ----------------------------------------------------
local painted = 0
for i = 0, TILE_W * TILE_H - 1 do
    if api:paintFloor(i, base) then
        painted = painted + 1
    end
end
print(string.format("Filled %d floor tiles with %s.", painted, BASE_TILE))

-- ---- 2. scatter the reference map's scenery -------------------------------
math.randomseed(SEED)
local placed = 0
for _ = 1, VEG_COUNT do
    local col = math.random(EDGE_MARGIN, HEX_W - 1 - EDGE_MARGIN)
    local row = math.random(EDGE_MARGIN, HEX_H - 1 - EDGE_MARGIN)
    local hex = row * HEX_W + col
    local pid = palette[math.random(#palette)]
    if api:placeProto(pid, hex, 0) then
        placed = placed + 1
    end
end
print(string.format("Scattered %d / %d decorations from %s (%d scenery protos).", placed, VEG_COUNT, REFERENCE, #palette))
