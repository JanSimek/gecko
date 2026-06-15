-- Desert terrain generator for Gecko's Script Console.
--
-- Fills the current elevation's floor with the Fallout 2 wasteland tileset and
-- scatters vegetation, reproducing the palette the shipped desert encounter maps
-- (desert1..desert8) are built from. The whole run collapses into a single undo
-- entry, so one Ctrl+Z reverts it.
--
-- How to use: open the Script Console, paste this in, and press Run (Ctrl+Return).
-- The map API is the global `api`; it edits the elevation currently shown.
--
-- The palette below comes straight from analysing master.dat with
--   gecko-cli map analyze --data <master.dat>
-- edg5000 is the base sand used in 126 maps; the [scenery] PIDs are the exact
-- vegetation those maps place. Tweak the tunables and re-run to taste.

-- ---- tunables -------------------------------------------------------------
local BASE_TILE   = "edg5000" -- dominant wasteland sand
local VEG_COUNT   = 250       -- how many decorations to scatter
local EDGE_MARGIN = 6         -- keep scatter this many hexes off the border
local SEED        = 1         -- change for a different layout

-- Vegetation protos by raw engine PID (type << 24 | proto index). Listed roughly
-- by how common each is in the desert maps; duplicates bias the random pick.
local VEGETATION = {
    0x02000066, 0x02000067, 0x02000068, 0x02000069, -- Scrub
    0x0200007D, 0x0200007E, 0x0200007F,             -- Weed
    0x020003AF, 0x020003B0,                         -- Plant
    0x0200004A, 0x0200004B, 0x0200004F, 0x02000050, -- Rocks
    0x020003B1, 0x020003B2, 0x020003B3,             -- Tree
}

-- Grid sizes (see CLAUDE.md): floor/roof tiles are 100x100, hexes are 200x200.
local TILE_W, TILE_H = 100, 100
local HEX_W,  HEX_H  = 200, 200

-- ---- 1. fill the floor ----------------------------------------------------
local base = api:tileId(BASE_TILE)
if base < 0 then
    print("Can't resolve '" .. BASE_TILE .. ".frm'. Mount Fallout 2 data (master.dat) and try again.")
    return
end

local painted = 0
for i = 0, TILE_W * TILE_H - 1 do
    if api:paintFloor(i, base) then
        painted = painted + 1
    end
end
print(string.format("Filled %d floor tiles with %s.", painted, BASE_TILE))

-- ---- 2. scatter vegetation ------------------------------------------------
math.randomseed(SEED)
local placed = 0
for _ = 1, VEG_COUNT do
    local col = math.random(EDGE_MARGIN, HEX_W - 1 - EDGE_MARGIN)
    local row = math.random(EDGE_MARGIN, HEX_H - 1 - EDGE_MARGIN)
    local hex = row * HEX_W + col
    local pid = VEGETATION[math.random(#VEGETATION)]
    if api:placeProto(pid, hex, 0) then
        placed = placed + 1
    end
end
print(string.format("Scattered %d / %d decorations.", placed, VEG_COUNT))
