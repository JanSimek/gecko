-- Patchwork sampler: fills a new map with a grid of quilted patches, each learned from a
-- DIFFERENT shipped reference map -- a literal quilt of quilts. A quick way to see, side by
-- side, how api:quiltFloorRect reproduces each biome's hand-authored tile blending.
--
--   gecko-cli map generate --script scripts/editor/quilt_sampler.luau --out sampler.map \
--       --arg seed=42 --data <master.dat>
--
-- Where two patches of different biomes touch, their tile vocabularies don't overlap, so the
-- synthesizer falls back along its repair ladder at the border -- watch the per-patch stats
-- report those repairs/unmatched seams. Inside each patch the ground is seamless.
--
-- Choosing references: the quilt reproduces whatever the reference's floor actually holds, so
-- pick maps whose ground IS the ground you want. Beware invisible-by-design filler tiles that
-- shipped maps hide under other art: cave maps are mostly the near-black solid-rock tiles
-- (cav4009/cav4010) that sit under wall sprites, CITY maps carpet building footprints with the
-- solid-black bld2043 that sits under roof sprites, and coast maps are mostly genuinely empty
-- ocean. Quilted bare, all of those read as holes.
--
-- args:
--   refs   comma-separated reference maps, cycled across the grid
--          (default: two desert sands, mountain, scrubland -- all open walkable ground)
--   patch  patch edge in tiles (default 25)
--   seed   reproduces the layout exactly

local REFS = {}
for ref in string.gmatch(args.refs or "maps/desert1.map,maps/klagraz.map,maps/mountn2.map,maps/desert5.map", "[^,]+") do
    REFS[#REFS + 1] = ref
end
if #REFS == 0 then
    error("--arg refs= must name at least one reference map")
end
local PATCH = tonumber(args.patch) or 25
if PATCH < 1 then
    error("--arg patch= must be at least 1")
end
local TILE_W, TILE_H = 100, 100 -- floor grid (see CLAUDE.md: tiles are 100x100)

print("Seed " .. args.seed .. " — re-run with --arg seed=" .. args.seed .. " to reproduce.")

-- Exclude known invisible-by-design filler so quilting learns only the visible ground even from
-- references that carpet non-play areas with it (e.g. the Restoration Project's encounter maps
-- carpet most of their floors with the solid-black bld2043 that hides under building roofs).
-- Best-effort: tileId needs tiles.lst; without it the quilt is still valid.
local ok, filler = pcall(function()
    return api:tileId("bld2043")
end)
if ok and filler >= 0 then
    api:quiltExclude({ filler })
end

local patchIndex = 0
local totalPainted = 0
for row0 = 0, TILE_H - 1, PATCH do
    for col0 = 0, TILE_W - 1, PATCH do
        local ref = REFS[patchIndex % #REFS + 1]
        local col1 = math.min(col0 + PATCH - 1, TILE_W - 1)
        local row1 = math.min(row0 + PATCH - 1, TILE_H - 1)
        -- pcall so one unreadable reference skips ITS patch instead of aborting the whole run.
        local ok, painted = pcall(function()
            return api:quiltFloorRect(ref, 0, col0, row0, col1, row1)
        end)
        if ok then
            totalPainted = totalPainted + painted
            local stats = api:quiltStats()
            print(string.format("patch (%2d,%2d) <- %-18s %4d tiles, %3d repaired, %3d seams unmatched",
                col0, row0, ref, painted, stats[5], stats[6]))
        else
            print(string.format("patch (%2d,%2d) <- %-18s FAILED: %s", col0, row0, ref, tostring(painted)))
        end
        patchIndex = patchIndex + 1
    end
end
print(string.format("%d patches from %d references; painted %d of %d floor tiles.",
    patchIndex, #REFS, totalPainted, TILE_W * TILE_H))
