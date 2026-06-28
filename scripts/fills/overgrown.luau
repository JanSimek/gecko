-- name: Overgrown
--
-- Procedural OVERGROWN / WILD fill: the universal edg5000 sand ground overrun with low plants, weeds
-- and scrub — a reclaimed-by-nature look, denser low greenery than desert but without trees.
-- Selection-scoped (api:areaFloorTiles() + api:areaHexes()) and seeded, like the other biome fills.
--
-- Ids were read from the shipped maps with `gecko-cli map dump-grid`, then every scenery NUMBER was
-- resolved to its FRM with `resolve_fid` and eyeballed with `render_frm` so only complete, free-standing
-- art is scattered: dense scrub 102/103/104/105 (weed01-weed04) + weed 125/126/127 (weed24-weed26), a
-- thick low ground cover. (The generic "Plant" protos 366/367/369/963-965 are garden crops — cabbages,
-- corn, marijuana — so they are deliberately NOT used.) The floor WEIGHTS mirror the real edg5000-series
-- gradient.
--
--   gecko-cli map generate --script scripts/fills/overgrown.luau --out out.map --data <master.dat>
--
-- --arg density   growth chance per clumped hex (default 0.05)
-- --arg coverage  0..1 share of the area that grows (default 0.6)

local DENSITY = tonumber(args.density) or 0.05
local COVERAGE = tonumber(args.coverage) or 0.6

-- Graded sand floor (the universal surface ground), weights ≈ real edg5000-series counts. {id,weight}.
local GROUND = {
    { 191, 24 }, { 194, 12 }, { 193, 9 }, { 192, 8 }, { 195, 8 }, { 196, 6 }, { 197, 5 }, { 198, 4 },
}

-- Overgrowth scenery by scenery-proto NUMBER, weighted: dense scrub bushes dominate, then weeds. Every
-- entry resolves to complete, free-standing bush/weed art — a thick low ground cover. {number, weight}.
local SCENERY = {
    { 102, 6 }, { 103, 6 }, { 104, 5 }, { 105, 5 }, -- scrub (weed01-weed04)
    { 125, 4 }, { 126, 4 }, { 127, 3 }, -- weeds (weed24-weed26)
}

local GROUND_SCALE = 0.05 -- dune-region size
local CLUMP_SCALE = 0.07 -- growth-patch size
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

-- 2. dense low growth over the target hexes.
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

print(string.format("Overgrown: %d floor tiles, %d growth.", painted, placed))
