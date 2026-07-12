-- name: Quilted Desert
--
-- Seamless DESERT ground for the current selection, patch-quilted from a real shipped map
-- (maps/desert1.map) via api:quiltFloorTiles — the hand-authored edg* edge/corner blending,
-- not a per-tile random mix. Selection-scoped like the other fills (Fill Selection previews it
-- as ghosts and applies as one undo entry); over a whole elevation (gecko-cli generate) it
-- quilts the entire floor. Seeded: the same seed reproduces the layout exactly.
--
--   gecko-cli map generate --script scripts/fills/quilt_desert.luau --out out.map --data <master.dat>
--
-- --arg ref           reference map to learn from (default maps/desert1.map)
-- --arg refElevation  reference elevation (default 0)

local REF = args.ref or "maps/desert1.map"
local REF_ELEVATION = tonumber(args.refElevation) or 0
local TILE_W, TILE_H = 100, 100

-- Exclude known invisible-by-design filler: the solid-black bld2043 normally hides under city
-- building roofs, and the Restoration Project's encounter maps carpet most of their floors with
-- it. Best-effort — tileId needs tiles.lst; without it the quilt is still valid on vanilla data.
local ok, filler = pcall(function()
    return api:tileId("bld2043")
end)
if ok and filler >= 0 then
    api:quiltExclude({ filler })
end

-- Target: the selection when one is bound (Fill Selection), else the whole elevation (generate).
local tiles
if api:hasArea() then
    tiles = api:areaFloorTiles()
else
    tiles = {}
    for i = 0, TILE_W * TILE_H - 1 do
        tiles[#tiles + 1] = i
    end
end

local painted = api:quiltFloorTiles(REF, REF_ELEVATION, tiles)
local stats = api:quiltStats()
print(string.format("Quilted %d floor tiles from %s (%d repaired, %d seams unmatched).",
    painted, REF, stats[5], stats[6]))
