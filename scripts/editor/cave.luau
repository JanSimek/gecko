-- Cave generator for Gecko's Script Console / gecko-cli — the worked example of the
-- region/fill helpers.
--
-- Carves a connected cavern system out of the darkness: chambers joined by corridors,
-- retextured with a whole cave-floor tile family in coherent patches.
--   tilesByPrefix -- the "cav1*" floor family by name, no hardcoded tile ids
--   fillFloorRect -- carve chambers and corridors
--   fillRegion    -- paint-bucket: flood the cavern to find (and fix) sealed-off chambers
--   tilesInRect   -- the retexture pass over the grid
--
--   gecko-cli map generate --script scripts/editor/cave.luau --out cave.map \
--       --arg seed=42 --data <master.dat>
--
-- Five different-but-reproducible caves in one run (consecutive seeds from the base):
--   gecko-cli map generate --script scripts/editor/cave.luau --out cave.map \
--       --count 5 --arg seed=42 --data <master.dat>
--
-- api:rng()/rngInt() and math.random are both seeded from args.seed. Run it from generate or
-- the Script Console; it reads back its own paints, so it is not a Fill-dialog brush.

-- ---- tunables (override with --arg key=value) ------------------------------
local FAMILY = args.family or "cav1" -- tiles.lst prefix of the cave-floor family
local CHAMBERS = tonumber(args.chambers) or 6
local MIN_HALF, MAX_HALF = 4, 9 -- chamber half-extents, in tiles
local MARGIN = 12 -- keep the cavern this far off the map border
local PATCH = 0.10 -- smaller = larger same-texture patches

local TILE_W, TILE_H = 100, 100
local EMPTY = 1 -- Map::EMPTY_TILE

print("Seed " .. args.seed .. " — re-run with --arg seed=" .. args.seed .. " to reproduce this cave.")

-- ---- 1. the floor family, by name ------------------------------------------
-- tilesByPrefix raises if no game data is mounted; an empty result means a wrong prefix.
local family = api:tilesByPrefix(FAMILY)
local names = {}
for name in pairs(family) do
    table.insert(names, name)
end
table.sort(names) -- pairs() order is unspecified; sort so a seed reproduces exactly
if #names == 0 then
    error("No tiles match prefix '" .. FAMILY .. "' — try --arg family=cav1 (or cav2, brick, ...).")
end
local variants = {}
for _, name in ipairs(names) do
    table.insert(variants, family[name])
end
local base = variants[1]
print(string.format("Floor family '%s*': %d tiles (%s .. %s).", FAMILY, #names, names[1], names[#names]))

-- ---- 2. carve chambers + corridors ------------------------------------------
local chambers = {}
local carved = 0
for _ = 1, CHAMBERS do
    local w = api:rngInt(MIN_HALF, MAX_HALF)
    local h = api:rngInt(MIN_HALF, MAX_HALF)
    local cx = api:rngInt(MARGIN + w, TILE_W - 1 - MARGIN - w)
    local cy = api:rngInt(MARGIN + h, TILE_H - 1 - MARGIN - h)
    carved += api:fillFloorRect(cx - w, cy - h, cx + w, cy + h, base)
    table.insert(chambers, { x = cx, y = cy })
end

-- An L-shaped corridor between two chamber centres, two tiles wide.
local function corridor(a, b)
    local n = api:fillFloorRect(math.min(a.x, b.x), a.y, math.max(a.x, b.x), a.y + 1, base)
    n += api:fillFloorRect(b.x, math.min(a.y, b.y), b.x + 1, math.max(a.y, b.y), base)
    return n
end
for i = 2, #chambers do
    carved += corridor(chambers[i - 1], chambers[i])
end
print(string.format("Carved %d chambers (%d floor-tile paints).", #chambers, carved))

-- ---- 3. connectivity, the paint-bucket way ----------------------------------
-- Flood the cavern from chamber 1 with a marker variant: every walk-connected floor tile takes
-- the marker, so a chamber whose centre still reads `base` is a sealed-off pocket (overlapping
-- corridors can carve one off). Cut it a corridor and flood it in too. The marker is just
-- another family tile — the retexture pass repaints everything below.
local marker = variants[#variants]
if marker ~= base then
    api:fillRegion(chambers[1].x, chambers[1].y, marker)
    for i = 2, #chambers do
        local c = chambers[i]
        if api:getFloorXY(c.x, c.y) == base then
            corridor(chambers[1], c)
            api:fillRegion(c.x, c.y, marker)
            print(string.format("Reconnected a sealed-off chamber at tile (%d, %d).", c.x, c.y))
        end
    end
end

-- ---- 4. retexture the cavern in coherent patches ----------------------------
-- A noise field picks the variant per tile, so same-texture patches form instead of static.
local ox, oy = api:rng() * 1000.0, api:rng() * 1000.0
local retextured = 0
for _, t in ipairs(api:tilesInRect(0, 0, TILE_W - 1, TILE_H - 1)) do
    if api:getFloor(t) ~= EMPTY then
        local field = api:noise2d(api:tileCol(t) * PATCH + ox, api:tileRow(t) * PATCH + oy)
        local pick = 1 + math.min(#variants - 1, math.floor(field * #variants))
        api:paintFloor(t, variants[pick])
        retextured += 1
    end
end
print(string.format("Retextured %d cavern tiles across %d variants.", retextured, #variants))

-- ---- 5. drop the player into the first chamber ------------------------------
-- A floor tile spans roughly 2x2 hexes, so hex (2*col, 2*row) lands close enough to tile
-- (col, row) to put the spawn mid-chamber.
api:setPlayerStart(api:hexIndex(2 * chambers[1].x, 2 * chambers[1].y), 0, 0)
print(string.format("Player start in the first chamber, tile (%d, %d).", chambers[1].x, chambers[1].y))
