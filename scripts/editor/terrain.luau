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
-- Pass --arg ref=maps/desert1.map to QUILT the floor from that map instead of the flat fill:
-- api:quiltFloorRect learns the reference's hand-authored tile blending (the edg* edge/corner
-- families) and synthesizes ground that transitions the same way, seeded and reproducible.
-- --arg refElevation=<0..2> picks the reference elevation (default 0).
--
-- `api` is the map, `args` the parameters. (To discover a biome's protos and tiles, run
--  `gecko-cli map analyze --data <master.dat>`; api:protoName(pid) names one.)

-- ---- curated palette -------------------------------------------------------
-- Name each scenery proto by its id (the number `map analyze` reports) so the palette reads in
-- words, not hex: api:proto("scenery", id) builds the PID. Small desert decorations only — the
-- desert encounter maps (desert1..8) are built from these.
local SCRUB1, SCRUB2, SCRUB3, SCRUB4 = 102, 103, 104, 105
local WEED1, WEED2, WEED3            = 125, 126, 127
local PLANT1, PLANT2                 = 943, 944
local ROCK1, ROCK2, ROCK3, ROCK4    = 74, 75, 79, 80
local TREE1, TREE2, TREE3           = 945, 946, 947

-- Listing a proto more than once weights it up, so common plants dominate.
local PALETTE = {
    SCRUB1, SCRUB2, SCRUB3, SCRUB4,
    SCRUB1, SCRUB2, -- weighted up: scrub is everywhere
    WEED1, WEED2, WEED3,
    PLANT1, PLANT2,
    ROCK1, ROCK2, ROCK3, ROCK4,
    TREE1, TREE2, TREE3,
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
if args.ref then
    -- Seamless mode: synthesize the whole floor from a reference map's authored blending.
    local refElevation = tonumber(args.refElevation) or 0
    local painted = api:quiltFloorRect(args.ref, refElevation, 0, 0, TILE_W - 1, TILE_H - 1)
    print(string.format("Quilted %d floor tiles from %s (elevation %d).", painted, args.ref, refElevation))
    -- Degraded synthesis is worth knowing about: repaired cells are normal in small numbers,
    -- unresolved seams are borders the reference never showed.
    local stats = api:quiltStats()
    if stats[5] > 0 or stats[6] > 0 then
        print(string.format("Quilt fidelity: %d cells repaired, %d seams unmatched in the reference.", stats[5], stats[6]))
    end
else
    -- tileId raises a clear error if Fallout 2 data isn't mounted (the run reports it); it returns
    -- -1 only when the data IS present but the name is unknown.
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
end

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
        local pid = api:proto("scenery", PALETTE[math.random(#PALETTE)])
        if api:placeProto(pid, hex, 0) then
            placed = placed + 1
        end
    end
end
print(string.format("Scattered %d / %d decorations in clumps.", placed, DENSITY))
