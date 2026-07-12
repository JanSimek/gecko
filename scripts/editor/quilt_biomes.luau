-- Two-biome generator: partitions the floor with a smooth noise field and quilts each zone
-- from its own reference map (api:quiltFloorTiles over the irregular regions), so both
-- grounds keep their hand-authored blending and meet along an organic, wandering boundary
-- instead of a straight seam.
--
--   gecko-cli map generate --script scripts/editor/quilt_biomes.luau --out biomes.map \
--       --arg seed=7 --data <master.dat>
--
-- The zone quilted second blends against the first zone's committed edge; where the two
-- vocabularies don't overlap the synthesizer repairs along the boundary and reports it
-- (that one-cell frontier is exactly where a hand-authored transition set would go).
--
-- Choosing references: the quilt reproduces whatever the reference's floor actually holds.
-- Beware invisible-by-design filler tiles the shipped maps hide under other art: cave maps are
-- mostly near-black solid rock (cav4009/cav4010, normally under wall sprites), CITY maps carpet
-- building footprints with the solid-black bld2043 (normally under roof sprites), and coast maps
-- are mostly empty ocean. Prefer open walkable ground: desert1-9, mountn1-6, klagraz, modbrah.
--
-- args:
--   refA / refB   the two reference maps (default desert + mountain)
--   coverage      0..1 share of the map given to biome A (default 0.55)
--   scale         boundary wiggliness -- smaller = larger, smoother blobs (default 0.045)
--   seed          reproduces the layout exactly

local REF_A = args.refA or "maps/desert1.map"
local REF_B = args.refB or "maps/mountn2.map"
local COVERAGE = tonumber(args.coverage) or 0.55
local SCALE = tonumber(args.scale) or 0.045
local TILE_W, TILE_H = 100, 100 -- floor grid (see CLAUDE.md: tiles are 100x100)

print("Seed " .. args.seed .. " — re-run with --arg seed=" .. args.seed .. " to reproduce.")

-- Exclude known invisible-by-design filler so quilting learns only the visible ground even from
-- references that carpet non-play areas with it (the Restoration Project's encounter maps carpet
-- most of their floors with the solid-black bld2043 that normally hides under building roofs).
local filler = api:tileId("bld2043")
if filler >= 0 then
    api:quiltExclude({ filler })
end

-- Offset the noise field per run so each seed draws a different boundary.
local ox, oy = api:rng() * 1000.0, api:rng() * 1000.0

-- Threshold the smooth field into two irregular tile regions: a few large connected blobs,
-- not speckle. (noise2d is deterministic; the offsets carry the per-run variation.)
local zoneA, zoneB = {}, {}
for t = 0, TILE_W * TILE_H - 1 do
    local field = api:noise2d(api:tileCol(t) * SCALE + ox, api:tileRow(t) * SCALE + oy)
    if field < COVERAGE then
        zoneA[#zoneA + 1] = t
    else
        zoneB[#zoneB + 1] = t
    end
end

local totalPainted = 0
local function quiltZone(name, ref, zone)
    if #zone == 0 then
        print(name .. ": empty zone (coverage at an extreme), skipped.")
        return
    end
    -- pcall so an unreadable reference skips ITS zone instead of aborting the whole run.
    local ok, painted = pcall(function()
        return api:quiltFloorTiles(ref, 0, zone)
    end)
    if not ok then
        print(string.format("%s <- %-18s FAILED: %s", name, ref, tostring(painted)))
        return
    end
    totalPainted = totalPainted + painted
    local stats = api:quiltStats()
    print(string.format("%s <- %-18s %4d tiles, %d repaired, %d seams unmatched",
        name, ref, painted, stats[5], stats[6]))
end

quiltZone("biome A", REF_A, zoneA) -- quilted first, so B blends against its committed edge
quiltZone("biome B", REF_B, zoneB)
print(string.format("painted %d of %d floor tiles.", totalPainted, TILE_W * TILE_H))
