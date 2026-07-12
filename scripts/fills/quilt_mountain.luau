-- name: Quilted Mountain
--
-- Seamless MOUNTAIN ground for the current selection, patch-quilted from a real shipped map
-- (maps/mountn2.map) via api:quiltFloorTiles — the hand-authored rock/sand blending, not a
-- per-tile random mix. Selection-scoped like the other fills (Fill Selection previews it as
-- ghosts and applies as one undo entry); over a whole elevation (gecko-cli generate) it quilts
-- the entire floor. Seeded: the same seed reproduces the layout exactly.
--
--   gecko-cli map generate --script scripts/fills/quilt_mountain.luau --out out.map --data <master.dat>
--
-- --arg ref           reference map to learn from (default maps/mountn2.map)
-- --arg refElevation  reference elevation (default 0)

local REF = args.ref or "maps/mountn2.map"
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

-- The reference's cliffs are floor art PLUS standing scenery (rock faces, rock patches, scrub,
-- trees); transplant the pieces standing on the copied cells so cliffs come out whole. Skip
-- what verbatim copying must not carry: the invisible engine markers below, misc-type markers
-- (scroll blockers / exit grids — skipped by not requesting "misc"), and walls entirely (the
-- mountain references only carry see-through blocker walls, invisible in game).
local MARKERS = {
    api:proto("scenery", 344), -- Block Hex Auto Inviso
    api:proto("scenery", 67), -- Secret Blocking Hex
    api:proto("scenery", 49), -- Exit Grid Map Marker
    -- The Restoration Project's black-mask scenery (art/scenery/black00x.frm): solid black
    -- patches its maps lay over unreachable cliff tops. Cover art, not cliff art — quilted onto
    -- open ground they read as floating black boxes. (Find others with `map analyze` + the
    -- unnamed protos' art via `frm render`.)
    api:proto("scenery", 2296),
    api:proto("scenery", 2297),
}
local pieces = api:quiltObjects("scenery", MARKERS)

local stats = api:quiltStats()
print(string.format("Quilted %d floor tiles + %d standing pieces from %s (%d repaired, %d seams unmatched).",
    painted, pieces, REF, stats[5], stats[6]))
