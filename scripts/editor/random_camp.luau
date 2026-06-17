-- random_camp.luau — a random desert with structures: the wilderness of random_desert.luau, plus
-- tents (or any prefab) dropped from a stamp captured with extract_pattern. The worked example of
-- the full extract -> generate loop: analyze a reference map, extract_pattern a tent to tent.json,
-- then generate with this script to scatter both the vegetation and the real tents.
--
--   gecko-cli map generate --script scripts/editor/random_camp.luau --out camp.map \
--       --stamp tent=tent.json --arg tents=3 \
--       --data <master.dat> [--data <critter.dat>]
--
-- --arg density   scenery objects to scatter (default 420)
-- --arg coverage  0..1 share of the map that grows vegetation (default 0.5)
-- --arg scale     clump-noise frequency; smaller = larger, smoother clumps (default 0.07)
-- --arg tents     number of stamps to place (default 2)
-- --arg tent      stamp name to place (default "tent"; load it with --stamp <name>=<file.json>)
-- --arg spacing   minimum hexes between two placed stamps (default 30)

local DENSITY = tonumber(args.density) or 420
local COVERAGE = tonumber(args.coverage) or 0.5
local SCALE = tonumber(args.scale) or 0.07
local TENTS = tonumber(args.tents) or 2
local STAMP = args.tent or "tent"
local SPACING = tonumber(args.spacing) or 30

local HEX_W, HEX_H = 200, 200
local TILE_W, TILE_H = 100, 100
local EDGE_MARGIN = 8

print(string.format("random_camp: seed %s (re-run with --arg seed=%s to reproduce)", args.seed, args.seed))

-- ---- 1. desert floor: weighted edg500x field (see random_desert.luau) -------
local FLOOR = {
    { name = "edg5000", weight = 11018 },
    { name = "edg5003", weight = 5404 },
    { name = "edg5002", weight = 4285 },
    { name = "edg5001", weight = 3900 },
    { name = "edg5004", weight = 3543 },
    { name = "edg5005", weight = 2827 },
    { name = "edg5006", weight = 2506 },
    { name = "edg5007", weight = 2135 },
}
local floorTotal = 0
for _, t in ipairs(FLOOR) do
    t.id = api:tileId(t.name)
    if t.id < 0 then
        error("Unknown ground tile '" .. t.name .. "' — is the Fallout 2 data mounted?")
    end
    floorTotal = floorTotal + t.weight
end

local function weightedPick(list, total, key)
    local r = math.random() * total
    for _, entry in ipairs(list) do
        r = r - entry.weight
        if r <= 0 then
            return entry[key]
        end
    end
    return list[1][key]
end

for i = 0, TILE_W * TILE_H - 1 do
    api:paintFloor(i, weightedPick(FLOOR, floorTotal, "id"))
end
print("Filled the floor with the weighted edg5000-5007 desert field.")

-- ---- 2. scatter desert scenery in clumps (see random_desert.luau) -----------
local PALETTE = {
    { number = 102, weight = 83 }, -- Scrub
    { number = 103, weight = 75 }, -- Scrub
    { number = 105, weight = 66 }, -- Scrub
    { number = 104, weight = 56 }, -- Scrub
    { number = 125, weight = 40 }, -- Weed
    { number = 126, weight = 37 }, -- Weed
    { number = 127, weight = 33 }, -- Weed
    { number = 64, weight = 24 }, -- Cactus
    { number = 65, weight = 12 }, -- Cactus
}
local palTotal = 0
for _, p in ipairs(PALETTE) do
    p.pid = api:proto("scenery", p.number)
    palTotal = palTotal + p.weight
end

local ox, oy = math.random() * 4096.0, math.random() * 4096.0
local threshold = 1.0 - COVERAGE
local placed, attempts = 0, 0
local maxAttempts = DENSITY * 12
while placed < DENSITY and attempts < maxAttempts do
    attempts = attempts + 1
    local col = math.random(EDGE_MARGIN, HEX_W - 1 - EDGE_MARGIN)
    local row = math.random(EDGE_MARGIN, HEX_H - 1 - EDGE_MARGIN)
    local field = api:noise2d(col * SCALE + ox, row * SCALE + oy)
    if field > threshold and math.random() < field then
        if api:placeProtoXY(weightedPick(PALETTE, palTotal, "pid"), col, row, 0) then
            placed = placed + 1
        end
    end
end
print(string.format("Scattered %d desert plants in clumps.", placed))

-- ---- 3. place the tents -----------------------------------------------------
-- Spread the stamps out (at least SPACING hexes apart) so two camps don't overlap. placeStamp
-- raises if the stamp wasn't loaded with --stamp; pcall turns that into a clear one-line warning
-- instead of aborting the whole map.
if TENTS > 0 then
    local sites = {}
    local function farEnough(col, row)
        for _, s in ipairs(sites) do
            if math.abs(col - s.col) < SPACING and math.abs(row - s.row) < SPACING then
                return false
            end
        end
        return true
    end

    local dropped, tries = 0, 0
    while dropped < TENTS and tries < TENTS * 40 do
        tries = tries + 1
        local col = math.random(40, HEX_W - 40)
        local row = math.random(40, HEX_H - 40)
        if farEnough(col, row) then
            local ok, result = pcall(function()
                return api:placeStamp(STAMP, api:hexIndex(col, row), 0)
            end)
            if not ok then
                print("tent skipped: " .. tostring(result))
                break -- the stamp isn't loaded; no point retrying
            end
            sites[#sites + 1] = { col = col, row = row }
            dropped = dropped + 1
        end
    end
    print(string.format("Placed %d / %d '%s' stamps.", dropped, TENTS, STAMP))
end
