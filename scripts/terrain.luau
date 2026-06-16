-- Terrain generator for Gecko's Script Console / gecko-cli.
--
-- Reproduces a shipped map's terrain by borrowing its palette: it fills the floor with that map's
-- dominant ground tile and scatters the scenery that map is actually built from, in the same
-- proportions -- common vegetation dominates and rare features stay rare, because placement is
-- weighted by how often the reference uses each proto (not uniform over distinct protos). Works for
-- any biome (desert, cave, town, ...): the palette is the reference map's real data, and a proto's
-- PID is its unique id, so nothing is guessed. The whole run is one undo entry (Ctrl+Z).
--
-- Pick the reference with --arg reference=maps/cave0.map; omit it for a random map.
--   gecko-cli map generate --script scripts/terrain.luau --out out.map \
--       --arg reference=maps/desert1.map --arg seed=42 --arg density=300 --data <master.dat>
--
-- `api` is the map, `args` the parameters.

-- ---- parameters (override with --arg key=value) ---------------------------
local DENSITY     = tonumber(args.density)  -- objects to scatter; default = the reference's own count
local EDGE_MARGIN = 6                        -- keep scatter this many hexes off the border

-- Grid sizes (see CLAUDE.md): floor/roof tiles are 100x100, hexes are 200x200.
local TILE_W, TILE_H = 100, 100
local HEX_W,  HEX_H  = 200, 200

-- The host seeds the RNG from args.seed — a fresh value each run when you didn't pass --arg seed.
-- Echo it so a layout you like can be reproduced exactly with --arg seed=<that value>.
print("Seed " .. args.seed .. " — re-run with --arg seed=" .. args.seed .. " to reproduce this layout.")

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

-- Frequency histogram { [pid] = count }: how many of each scenery proto the reference places.
-- Flatten it into a cumulative-weight table so a pick lands on a proto with probability count/total
-- -- common vegetation dominates, a 2-of-500 car stays a ~1-in-500 pick.
local hist = api:mapSceneryHistogram(reference)
local pids, cumulative, total = {}, {}, 0
for pid, count in pairs(hist) do
    total = total + count
    pids[#pids + 1] = pid
    cumulative[#pids] = total
end

local function pickProto()
    local r = math.random(total) -- 1..total
    for i = 1, #cumulative do
        if r <= cumulative[i] then
            return pids[i]
        end
    end
    return pids[#pids]
end

-- ---- 1. fill the floor ----------------------------------------------------
local painted = 0
for i = 0, TILE_W * TILE_H - 1 do
    if api:paintFloor(i, base) then
        painted = painted + 1
    end
end
print(string.format("Filled %d floor tiles from %s.", painted, reference))

-- ---- 2. scatter the reference map's scenery -------------------------------
-- Default to the reference's own object count (its real density for a full map); --arg density=N
-- overrides.
local target = DENSITY or total
local placed = 0
if total > 0 then
    for _ = 1, target do
        local col = math.random(EDGE_MARGIN, HEX_W - 1 - EDGE_MARGIN)
        local row = math.random(EDGE_MARGIN, HEX_H - 1 - EDGE_MARGIN)
        local hex = row * HEX_W + col
        if api:placeProto(pickProto(), hex, 0) then
            placed = placed + 1
        end
    end
end
print(string.format("Scattered %d / %d decorations from %s (%d scenery protos; reference places %d).",
    placed, target, reference, #pids, total))
