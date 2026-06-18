-- Generic scatter generator — a *parameterized* terrain.luau.
--
-- Fill the floor with one ground tile and scatter a caller-supplied palette of scenery in natural
-- clumps. The tile and palette come from --arg, so there's no hardcoded biome: an agent (or you)
-- curates them from `gecko-cli map analyze --json` — pick a biome, choose the small, common,
-- non-"flat" scenery ids — and generates without editing a script.
--
--   gecko-cli map generate --script scripts/editor/scatter.luau --out out.map \
--       --arg tile=edg5000 --arg palette=102,103,104,945,946 --arg density=300 \
--       --data <master.dat>
--
-- --arg tile      ground-tile FRM name (required), e.g. edg5000
-- --arg palette   comma-separated scenery proto ids (required), e.g. 102,103,945
-- --arg type      proto type for the palette ids (default "scenery")
-- --arg density   number of decorations to scatter (default 300)

local TILE    = args.tile
local TYPE    = args.type or "scenery"
local DENSITY = tonumber(args.density) or 300

local EDGE_MARGIN = 6     -- keep scatter off the border
local CLUMP_SCALE = 0.06  -- smaller = larger, smoother clumps
local COVERAGE    = 0.55  -- 0..1 share of the map that grows scenery
local HEX_W, HEX_H   = 200, 200
local TILE_W, TILE_H = 100, 100

print("Seed " .. args.seed .. " — re-run with --arg seed=" .. args.seed .. " to reproduce.")

if not TILE or TILE == "" then
    error("--arg tile=<ground tile name> is required (e.g. --arg tile=edg5000)")
end

-- Parse the palette: comma/space-separated proto ids -> PIDs via api:proto(type, id).
local palette = {}
for token in string.gmatch(args.palette or "", "[^,%s]+") do
    local id = tonumber(token)
    if id then
        palette[#palette + 1] = api:proto(TYPE, id)
    end
end
if #palette == 0 then
    error("--arg palette=<comma-separated proto ids> is required (e.g. --arg palette=102,103,945)")
end

-- ---- 1. fill the floor ----------------------------------------------------
local base = api:tileId(TILE) -- raises if no data is mounted; -1 if the name is unknown
if base < 0 then
    error("Unknown ground tile '" .. TILE .. "' — pass a valid --arg tile=<name>.")
end
local painted = 0
for i = 0, TILE_W * TILE_H - 1 do
    if api:paintFloor(i, base) then
        painted = painted + 1
    end
end
print(string.format("Filled %d floor tiles with %s.", painted, TILE))

-- ---- 2. scatter the palette in clumps -------------------------------------
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
        if api:placeProtoXY(palette[math.random(#palette)], col, row, 0) then
            placed = placed + 1
        end
    end
end
print(string.format("Scattered %d / %d decorations from a %d-proto palette.", placed, DENSITY, #palette))
