-- Desert terrain generator for Gecko's Script Console / gecko-cli.
--
-- Fills the floor with wasteland sand and scatters a *curated* palette of desert vegetation in
-- natural clumps. Curated is deliberate: the palette is a hand-picked list of small decorations
-- (scrub, weeds, rocks, trees), so only sensible scatter objects ever appear -- no cars, no
-- vault doors, no structural pieces. To retheme (cave, town, ...) swap PALETTE and BASE_TILE for
-- that biome's decorations and ground. The whole run is one undo entry (Ctrl+Z).
--
--   gecko-cli map generate --script scripts/editor/terrain.luau --out out.map \
--       --arg seed=42 --arg density=350 --data <master.dat>
--
-- `api` is the map, `args` the parameters. (To discover a biome's PIDs and tiles, run
--  `gecko-cli map analyze --data <master.dat>`; api:protoName(pid) names one.)

-- ---- curated palette (engine PIDs: type << 24 | proto index) ---------------
-- Small desert decorations only; the desert encounter maps (desert1..8) are built from these.
-- Listing a proto more than once weights it up, so common plants dominate.
local PALETTE = {
    0x02000066, 0x02000067, 0x02000068, 0x02000069, -- Scrub
    0x02000066, 0x02000067,                          -- (weighted up: scrub is everywhere)
    0x0200007D, 0x0200007E, 0x0200007F,              -- Weed
    0x020003AF, 0x020003B0,                          -- Plant
    0x0200004A, 0x0200004B, 0x0200004F, 0x02000050,  -- Rocks
    0x020003B1, 0x020003B2, 0x020003B3,              -- Tree
}
local BASE_TILE = args.tile or "edg5000" -- wasteland sand; override with --arg tile=<name>

-- ---- tunables (override with --arg key=value) -----------------------------
local DENSITY     = tonumber(args.density) or 300 -- target number of decorations
local EDGE_MARGIN = 6                             -- keep scatter this many hexes off the border
local CLUMP_SCALE = 0.06                          -- smaller = larger, smoother clumps
local COVERAGE    = 0.55                          -- 0..1 share of the map that grows vegetation

-- Grid sizes (see CLAUDE.md): floor/roof tiles are 100x100, hexes are 200x200.
local TILE_W, TILE_H = 100, 100
local HEX_W,  HEX_H  = 200, 200

-- The host seeds the RNG from args.seed — a fresh value each run when you didn't pass --arg seed.
-- Echo it so a layout you like can be reproduced exactly with --arg seed=<that value>.
print("Seed " .. args.seed .. " — re-run with --arg seed=" .. args.seed .. " to reproduce this layout.")

-- ---- 1. fill the floor ----------------------------------------------------
-- tileId raises a clear error if Fallout 2 data isn't mounted (the run reports it); it returns -1
-- only when the data IS present but the name is unknown.
local base = api:tileId(BASE_TILE)
if base < 0 then
    error("Unknown ground tile '" .. BASE_TILE .. "' — pass a valid --arg tile=<name>.")
end

local painted = 0
for i = 0, TILE_W * TILE_H - 1 do
    if api:paintFloor(i, base) then
        painted = painted + 1
    end
end
print(string.format("Filled %d floor tiles with %s.", painted, BASE_TILE))

-- ---- 2. scatter vegetation in clumps --------------------------------------
-- A noise density field makes plants gather in patches and leave clearings, instead of an even
-- sprinkle. Place a candidate only where the field is high (rejection sampling), and bias toward
-- the densest spots. Offset the field by a per-run amount so each seed gives a different layout.
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
        local hex = row * HEX_W + col
        if api:placeProto(PALETTE[math.random(#PALETTE)], hex, 0) then
            placed = placed + 1
        end
    end
end
print(string.format("Scattered %d / %d decorations in clumps.", placed, DENSITY))
