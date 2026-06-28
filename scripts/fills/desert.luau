-- name: Desert
--
-- Procedural DESERT fill for the current selection: a noise-blended sand floor with sparse, clumped
-- scrub, weeds and the odd cactus — matched to how the shipped Fallout 2 desert encounter maps
-- actually look. Selection-scoped (api:areaFloorTiles() + api:areaHexes()), so it works as a "Fill
-- Selection" script in the editor and, over a whole elevation, as a gecko-cli generator. Seeded:
-- the same seed reproduces the layout.
--
-- Every id below was read from the shipped maps with `gecko-cli map dump-grid maps/desert{3,5,6}.map`
-- / `map analyze`: the floor WEIGHTS mirror the real edg5000-series proportions (edg5000 ~24%, the
-- rest a graded gradient), the scenery NUMBERS are the api:proto values (scrub 102-105, weed 125-127,
-- cactus 64-65, rocks 93), and DENSITY matches the real ~1-3% scenery-per-tile (not a carpet).
--
--   gecko-cli map generate --script scripts/fills/desert.luau --out out.map --data <master.dat>
--
-- --arg density   scenery placement chance per clumped hex (default 0.01 ≈ real desert)
-- --arg coverage  0..1 share of the area that grows scenery (default 0.55)

local DENSITY = tonumber(args.density) or 0.01
local COVERAGE = tonumber(args.coverage) or 0.55

-- Graded sand floor, weights ≈ real desert-map tile counts (edg5000 dominant, then a smooth fall-off
-- through edg5001..5007). Ordered so the noise bands below read as dunes, not hard patches. {id,weight}.
local GROUND = {
    { 191, 24 }, { 194, 12 }, { 193, 9 }, { 192, 8 }, { 195, 8 }, { 196, 6 }, { 197, 5 }, { 198, 4 },
}

-- Desert scenery by scenery-proto NUMBER (the value api:proto wants), weighted by real frequency:
-- scrub is most common, then weeds, a few cacti, the rare rock. {number, weight}.
local SCENERY = {
    { 102, 10 }, { 103, 10 }, { 104, 8 }, { 105, 8 }, -- scrub
    { 125, 5 }, { 126, 5 }, { 127, 4 }, -- weeds
    { 64, 3 }, { 65, 2 }, -- cacti
    { 93, 1 }, -- rocks
}

local GROUND_SCALE = 0.05 -- smaller = larger, smoother dune regions
local CLUMP_SCALE = 0.08 -- scenery clump size
local TILE_W, TILE_H = 100, 100
local HEX_W, HEX_H = 200, 200

-- Targets: the selection when one is bound (Fill Selection in the editor), else the whole elevation
-- (gecko-cli generate, which binds no area). One script, both uses.
local function targetFloorTiles()
    if api:hasArea() then
        return api:areaFloorTiles()
    end
    local all = {}
    for i = 0, TILE_W * TILE_H - 1 do
        all[#all + 1] = i
    end
    return all
end

local function targetHexes()
    if api:hasArea() then
        return api:areaHexes()
    end
    local all = {}
    for i = 0, HEX_W * HEX_H - 1 do
        all[#all + 1] = i
    end
    return all
end

-- Build a cumulative-weight picker: returns a function mapping f in [0,1) to a value by weight.
local function weightedPicker(entries)
    local total = 0
    for _, e in ipairs(entries) do
        total = total + e[2]
    end
    return function(f)
        local x = f * total
        local acc = 0
        for _, e in ipairs(entries) do
            acc = acc + e[2]
            if x < acc then
                return e[1]
            end
        end
        return entries[#entries][1]
    end
end

local groundFor = weightedPicker(GROUND)
local sceneryFor = weightedPicker(SCENERY)

print("Seed " .. args.seed .. " — re-run with --arg seed=" .. args.seed .. " to reproduce.")

-- 1. noise-blended sand floor over the target tiles.
local ox, oy = api:rng() * 1000.0, api:rng() * 1000.0
local painted = 0
for _, t in ipairs(targetFloorTiles()) do
    local f = api:noise2d(api:tileCol(t) * GROUND_SCALE + ox, api:tileRow(t) * GROUND_SCALE + oy)
    if api:paintFloor(t, groundFor(f)) then
        painted = painted + 1
    end
end

-- 2. sparse, clumped scenery over the target hexes.
local sx, sy = api:rng() * 1000.0, api:rng() * 1000.0
local threshold = 1.0 - COVERAGE
local placed = 0
for _, h in ipairs(targetHexes()) do
    local col, row = api:hexCol(h), api:hexRow(h)
    local clump = api:noise2d(col * CLUMP_SCALE + sx, row * CLUMP_SCALE + sy)
    if clump > threshold and api:rng() < DENSITY then
        local pid = api:proto("scenery", sceneryFor(api:rng()))
        if api:placeProtoXY(pid, col, row, 0) then
            placed = placed + 1
        end
    end
end

print(string.format("Desert: %d floor tiles, %d scenery.", painted, placed))
