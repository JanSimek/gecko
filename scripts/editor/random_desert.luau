-- random_desert.luau — procedurally rebuild a Fallout 2 wilderness desert, randomly each run.
--
-- The floor palette and the scenery palette below are exactly what the shipped desert maps are
-- built from: `gecko-cli map analyze` (the MCP `analyze` tool) over desert1..desert6.map reports
--   * a ground field dominated by the edg500x desert tileset, and
--   * upright scenery that is overwhelmingly Scrub, Weed and Cactus, with the odd Tumbleweed/Rocks.
-- Both tables are weighted by those real placement counts, so common vegetation dominates and the
-- rare props stay rare — the same distribution the originals have, not an even sprinkle.
--
-- Random every run: with no `--arg seed=N` the host picks a fresh seed and seeds math.random for
-- us, and the clump noise is offset by a per-run random amount. The resolved seed is printed so a
-- layout you like can be reproduced with `--arg seed=<that number>`.
--
--   gecko-cli map generate --script scripts/editor/random_desert.luau --out desert.map \
--       --data <master.dat> [--data <critter.dat>] \
--       [--arg density=420] [--arg coverage=0.5] [--arg scale=0.07] [--arg seed=N]
--
-- --arg density   scenery objects to scatter (default 420)
-- --arg coverage  0..1 share of the map that grows vegetation (default 0.5)
-- --arg scale     clump-noise frequency; smaller = larger, smoother clumps (default 0.07)

local DENSITY = tonumber(args.density) or 420
local COVERAGE = tonumber(args.coverage) or 0.5
local SCALE = tonumber(args.scale) or 0.07

local HEX_W, HEX_H = 200, 200 -- object/placement grid
local TILE_W, TILE_H = 100, 100 -- floor/roof grid
local EDGE_MARGIN = 8 -- keep scatter off the map border

print(string.format("random_desert: seed %s (re-run with --arg seed=%s to reproduce)", args.seed, args.seed))

-- ---- 1. desert floor: weighted edg500x field --------------------------------
-- The desert "field" tileset and its real frequencies across desert1..6. edg5000 dominates; the
-- 5001-5007 variants add sand texture. Names resolve to tile ids via api:tileId (so no magic
-- numbers, and it raises clearly if the Fallout 2 data is not mounted).
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

local function pickFloor()
    local r = math.random() * floorTotal
    for _, t in ipairs(FLOOR) do
        r = r - t.weight
        if r <= 0 then
            return t.id
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
print(string.format("Filled %d floor tiles (weighted edg5000-5007 desert field).", painted))

-- ---- 2. scatter desert scenery in natural clumps ----------------------------
-- The non-flat (upright, scatter-safe) desert scenery and its real frequencies. `number` is the
-- low 24 bits of the proto PID, which api:proto turns into the full PID — e.g. analyze reports
-- Scrub at pid 0x02000066, so number = 0x66 = 102 (one less than the 00000103.pro filename).
local PALETTE = {
    { name = "Scrub", number = 102, weight = 83 }, -- 0x02000066 / 00000103.pro
    { name = "Scrub", number = 103, weight = 75 }, -- 0x02000067 / 00000104.pro
    { name = "Scrub", number = 105, weight = 66 }, -- 0x02000069 / 00000106.pro
    { name = "Scrub", number = 104, weight = 56 }, -- 0x02000068 / 00000105.pro
    { name = "Weed", number = 125, weight = 40 }, -- 0x0200007D / 00000126.pro
    { name = "Weed", number = 126, weight = 37 }, -- 0x0200007E / 00000127.pro
    { name = "Weed", number = 127, weight = 33 }, -- 0x0200007F / 00000128.pro
    { name = "Cactus", number = 64, weight = 24 }, -- 0x02000040 / 00000065.pro
    { name = "Cactus", number = 65, weight = 12 }, -- 0x02000041 / 00000066.pro
    { name = "Tumbleweed", number = 63, weight = 3 }, -- 0x0200003F / 00000064.pro
    { name = "Rocks", number = 92, weight = 3 }, -- 0x0200005C / 00000093.pro
    { name = "Rocks", number = 93, weight = 3 }, -- 0x0200005D / 00000094.pro
}
local palTotal = 0
for _, p in ipairs(PALETTE) do
    p.pid = api:proto("scenery", p.number)
    palTotal = palTotal + p.weight
end

local function pickProto()
    local r = math.random() * palTotal
    for _, p in ipairs(PALETTE) do
        r = r - p.weight
        if r <= 0 then
            return p.pid
        end
    end
    return PALETTE[1].pid
end

-- Density field: api:noise2d gives smooth coherent noise in [0,1]; offset by a per-run random
-- amount so the clumps and clearings land somewhere different every run. Vegetation grows where
-- the field is high, leaving bare patches where it is low — like real desert cover.
local ox, oy = math.random() * 4096.0, math.random() * 4096.0
local threshold = 1.0 - COVERAGE
local occupied = {} -- hexIndex -> true, so we don't stack two plants on one hex
local placed, attempts = 0, 0
local maxAttempts = DENSITY * 12 -- clumping rejects many candidates; bound the loop
while placed < DENSITY and attempts < maxAttempts do
    attempts = attempts + 1
    local col = math.random(EDGE_MARGIN, HEX_W - 1 - EDGE_MARGIN)
    local row = math.random(EDGE_MARGIN, HEX_H - 1 - EDGE_MARGIN)
    local field = api:noise2d(col * SCALE + ox, row * SCALE + oy)
    -- Accept inside clumps (field over threshold), and even there denser toward clump centres.
    if field > threshold and math.random() < field then
        local hex = api:hexIndex(col, row)
        if hex >= 0 and not occupied[hex] then
            if api:placeProtoXY(pickProto(), col, row, 0) then
                occupied[hex] = true
                placed = placed + 1
            end
        end
    end
end
print(string.format("Scattered %d desert plants in clumps (palette of %d protos, %d%% coverage).",
    placed, #PALETTE, math.floor(COVERAGE * 100)))
