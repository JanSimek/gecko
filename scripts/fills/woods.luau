-- name: Woods
--
-- Procedural WOODS / OASIS fill: the universal edg5000 sand ground (what every Fallout 2 surface map
-- uses) thickly planted with tree groves, plants and scrub — a green, wooded patch, the most distinct
-- look from open desert while still using correct walkable surface tiles. Selection-scoped
-- (api:areaFloorTiles() + api:areaHexes()) and seeded, like the other biome fills.
--
-- Ids were read from the shipped maps with `gecko-cli map dump-grid`, then every scenery NUMBER was
-- resolved to its FRM with `resolve_fid` and eyeballed with `render_frm` so only complete, free-standing
-- art is scattered: trees 66/316/319/320/321 (tre1000, tree1, tree4-6) + 945/946/947 (tree7-9), with a
-- little scrub 102/103 (weed01/02) at the edges. (The generic "Plant" protos 366/367/369/944 are garden
-- crops — cabbages, corn, marijuana — so they are deliberately NOT used.) The floor WEIGHTS mirror the
-- real edg5000-series gradient. Trees are placed in tight groves.
--
--   gecko-cli map generate --script scripts/fills/woods.luau --out out.map --data <master.dat>
--
-- --arg density   scenery chance per hex inside a grove (default 0.07)
-- --arg coverage  0..1 share of the area that grows groves (default 0.4)

local DENSITY = tonumber(args.density) or 0.07
local COVERAGE = tonumber(args.coverage) or 0.4

-- Graded sand floor (the universal surface ground), weights ≈ real edg5000-series counts. {id,weight}.
local GROUND = {
    { 191, 24 }, { 194, 12 }, { 193, 9 }, { 192, 8 }, { 195, 8 }, { 196, 6 }, { 197, 5 }, { 198, 4 },
}

-- Woodland scenery by scenery-proto NUMBER, weighted: trees dominate (the groves), then a little scrub
-- at the edges. Every entry resolves to complete, free-standing tree/bush art. {number, weight}.
local SCENERY = {
    { 946, 8 }, { 945, 7 }, { 947, 7 }, -- tree8/tree7/tree9 (leafy)
    { 66, 5 }, { 316, 4 }, { 319, 4 }, { 320, 3 }, { 321, 3 }, -- tre1000/tree1/tree4/tree5/tree6
    { 102, 2 }, { 103, 2 }, -- scrub (weed01/weed02)
}

local GROUND_SCALE = 0.05 -- dune-region size
local CLUMP_SCALE = 0.10 -- grove size (larger value = smaller, tighter groves)
local TILE_W, TILE_H = 100, 100
local HEX_W, HEX_H = 200, 200

-- Targets: the selection when one is bound (Fill Selection), else the whole elevation (generate).
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

-- Cumulative-weight picker: maps f in [0,1) to a value by weight.
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

-- 1. sand floor over the target tiles.
local ox, oy = api:rng() * 1000.0, api:rng() * 1000.0
local painted = 0
for _, t in ipairs(targetFloorTiles()) do
    local f = api:noise2d(api:tileCol(t) * GROUND_SCALE + ox, api:tileRow(t) * GROUND_SCALE + oy)
    if api:paintFloor(t, groundFor(f)) then
        painted = painted + 1
    end
end

-- 2. tree groves over the target hexes — dense inside a clump, open between.
local sx, sy = api:rng() * 1000.0, api:rng() * 1000.0
local threshold = 1.0 - COVERAGE
local placed = 0
for _, h in ipairs(targetHexes()) do
    local col, row = api:hexCol(h), api:hexRow(h)
    local clump = api:noise2d(col * CLUMP_SCALE + sx, row * CLUMP_SCALE + sy)
    if clump > threshold and api:rng() < DENSITY * clump then
        local pid = api:proto("scenery", sceneryFor(api:rng()))
        if api:placeProtoXY(pid, col, row, 0) then
            placed = placed + 1
        end
    end
end

print(string.format("Woods: %d floor tiles, %d trees/plants.", painted, placed))
