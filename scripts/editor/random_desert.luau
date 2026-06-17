-- random_desert.luau — a richer random desert generator (a worked template).
--
-- Builds a desert map the way the shipped desert*.map are built, but procedurally and randomly:
--   1. a weighted MIX of the desert ground tileset (edg5000 + its edg500x variants), not one flat tile,
--   2. scrub/weeds scattered in natural clumps (noise2d), like terrain.luau / scatter.luau,
--   3. optional tents dropped from a stamp captured with extract_pattern (api:placeStamp).
--
-- The tiles, their relative frequencies and the scrub/weed palette are the values
-- `gecko-cli map analyze --json /maps/desert1.map` reports — i.e. what that map is actually built
-- from. Re-run for a fresh layout; pass --arg seed=N to reproduce a specific one.
--
--   gecko-cli map generate --script scripts/editor/random_desert.luau --out desert.map \
--       [--arg density=450] [--arg tents=2 --stamp tent=tent.json] --data <master.dat> [--data <critter.dat>]
--
-- --arg density   decorations to scatter (default 450)
-- --arg tents     number of tents to place (default 0; needs --stamp tent=<file.json>)
-- --arg tent      stamp name to place for tents (default "tent")

local DENSITY = tonumber(args.density) or 450
local TENTS = tonumber(args.tents) or 0
local STAMP = args.tent or "tent"

local HEX_W, HEX_H = 200, 200
local TILE_W, TILE_H = 100, 100
local EDGE_MARGIN = 8 -- keep scatter off the border
local CLUMP_SCALE = 0.06 -- smaller = larger, smoother clumps
local COVERAGE = 0.55 -- 0..1 share of the map that grows scenery

print("Seed " .. args.seed .. " — re-run with --arg seed=" .. args.seed .. " to reproduce.")

-- ---- 1. weighted desert floor --------------------------------------------
-- The ground tileset and its real frequencies from desert1.map: edg5000 dominates, the edg500x
-- variants add texture. Names resolve to tile ids via api:tileId (raises if no data is mounted).
local FLOOR = {
    { name = "edg5000", weight = 1795 },
    { name = "edg5003", weight = 877 },
    { name = "edg5002", weight = 696 },
    { name = "edg5001", weight = 624 },
    { name = "edg5004", weight = 583 },
}
local totalWeight = 0
for _, tile in ipairs(FLOOR) do
    tile.id = api:tileId(tile.name)
    if tile.id < 0 then
        error("Unknown ground tile '" .. tile.name .. "' — is the Fallout 2 data mounted?")
    end
    totalWeight = totalWeight + tile.weight
end

local function pickFloor()
    local r = math.random() * totalWeight
    for _, tile in ipairs(FLOOR) do
        r = r - tile.weight
        if r <= 0 then
            return tile.id
        end
    end
    return FLOOR[1].id
end

local painted = 0
for i = 0, TILE_W * TILE_H - 1 do
    if api:paintFloor(i, pickFloor()) then
        painted = painted + 1
    end
end
print(string.format("Filled %d floor tiles (weighted edg5000-5004 mix).", painted))

-- ---- 2. scatter scrub/weeds in clumps ------------------------------------
-- Desert vegetation palette (scenery proto ids from analyze — small, common, non-flat).
local PALETTE = { 103, 104, 105, 106, 126, 127, 128 }
local pids = {}
for _, id in ipairs(PALETTE) do
    pids[#pids + 1] = api:proto("scenery", id)
end

local ox, oy = math.random() * 1000.0, math.random() * 1000.0
local threshold = 1.0 - COVERAGE
local placed, attempts = 0, 0
local maxAttempts = DENSITY * 8 -- bound the loop: clumping rejects many candidates
while placed < DENSITY and attempts < maxAttempts do
    attempts = attempts + 1
    local col = math.random(EDGE_MARGIN, HEX_W - 1 - EDGE_MARGIN)
    local row = math.random(EDGE_MARGIN, HEX_H - 1 - EDGE_MARGIN)
    local field = api:noise2d(col * CLUMP_SCALE + ox, row * CLUMP_SCALE + oy) -- [0,1]
    if field > threshold and math.random() < field then
        if api:placeProtoXY(pids[math.random(#pids)], col, row, 0) then
            placed = placed + 1
        end
    end
end
print(string.format("Scattered %d decorations from a %d-proto palette.", placed, #pids))

-- ---- 3. optional tents from a stamp --------------------------------------
-- Needs --stamp <name>=<file.json> (a structure captured by extract_pattern). Each tent lands at a
-- random interior hex; pcall keeps a missing stamp a warning rather than aborting the whole map.
if TENTS > 0 then
    local dropped = 0
    for _ = 1, TENTS do
        local col = math.random(40, HEX_W - 40)
        local row = math.random(40, HEX_H - 40)
        local ok, result = pcall(function()
            return api:placeStamp(STAMP, api:hexIndex(col, row), 0)
        end)
        if ok then
            dropped = dropped + 1
        else
            print("tent skipped: " .. tostring(result))
        end
    end
    print(string.format("Placed %d / %d tents from stamp '%s'.", dropped, TENTS, STAMP))
end
