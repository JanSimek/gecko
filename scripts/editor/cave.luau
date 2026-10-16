-- Cave-interior generator, learned from the shipped cave maps.
--
-- Builds a real Fallout 2 cavern the way maps/cave1..4.map elevation 1 are built:
--   * the whole area filled with the reference's dark solid-rock tile pattern,
--   * a walkable cavern of rooms and straight corridors along the two iso axes (the only directions
--     the wall art runs) carved through it, painted with the reference's pervasive cav* FIELD tiles
--     (rare border/transition tiles like cav4013 and the entrance sand are kept out of the mix),
--   * the floor/rock boundary lined with "Cave Wall" PANELS: the wall art is a rock panorama cut into
--     16/32px strips, and the shipped rims stand them in order, each starting on the pixel after the
--     previous one ends. The rim is laid as chains left to right across the screen: a chain starts
--     with a piece for the spot's geometry (the rock-neighbour mask that pins the isometric
--     orientation, so faces land on the up-screen rim and filled lips on the near edge) and grows
--     with the strips the shipped rims put next to it, wherever one lands on the rim — so the
--     texture runs on across the seams the way the hand-made caves' does — and opens and closes
--     with the pieces the shipped rims draw as a run's ends,
--   * invisible blocking hexes sealing the rock side so the player can't walk off the floor,
--   * floor decoration (rocks, stalagmites, dirt, weeds) at the density the shipped caves have at
--     each distance from the walls — rocks heaped at their foot, the open floor nearly clear —
--     never cutting a passage,
--   * a scroll-blocker rectangle ringing the cavern so the camera stays inside,
--   * an exit-grid patch on the floor (worldmap by default) and the player start.
--
--   gecko-cli map generate --script scripts/editor/cave.luau --out cave.map \
--       --arg seed=42 --data <master.dat> [--data <critter.dat>]
--
-- Tunables: --arg chambers=N (default 5), --arg bays=N (per chamber, default 1),
-- --arg roomMin=/roomMax= (chamber half-size in 3-tile cells, default 1/2),
-- --arg refs=maps/cave1.map,maps/cave2.map,...
-- (the learning corpus, elevation 1), --arg exit=worldmap|none, --arg decor=<density scale: 1 = the references', default 0.5, 0 = none>.
-- Batch with --count N.
-- generate warns after writing if any exit grid is unreachable from the player start.

local REFS = {}
for path in string.gmatch(args.refs or "maps/cave1.map,maps/cave2.map,maps/cave3.map,maps/cave4.map", "[^,]+") do
    table.insert(REFS, path)
end
local REF_ELEVATION = tonumber(args.refElevation) or 1
local CHAMBERS = tonumber(args.chambers) or 5
local EXIT = args.exit or "worldmap"

local TILE_W, TILE_H = 100, 100
local EMPTY = 1 -- Map::EMPTY_TILE
-- Engine-verified blocker proto (pro_scenery.msg name):
local SECRET_BLOCKER = api:proto("scenery", 67) -- "Secret Blocking Hex"
local SCROLL_BLOCKER = api:proto("misc", 12) -- "Scroll Blocker"

print("Seed " .. args.seed .. " — re-run with --arg seed=" .. args.seed .. " to reproduce this cave.")

-- ============================ learn from the references =======================
-- Rock pattern: the caves fill unwalkable ground with a small repeating tile pattern
-- (cave1: cav4009/cav4010 on a 2x2 lattice). Recover it as the most common tile per
-- (row%2, col%2) parity cell; everything non-empty outside that pattern is walkable floor.
--
-- Wall pieces: which "Cave Wall" art a boundary hex takes is fixed by its OUTWARD-NORMAL COMPASS
-- CLASS — the screen direction the solid rock lies in — because the isometric view shows different
-- things on different sides. On the north/east rim the rock rises behind the floor and you see its
-- FACE (ca002/ca022/ca027-style rock walls); on the south/west rim you look over the near lip and
-- see the flat FILLED TOP of the rock (ca055/ca060/ca062-style). So we bin each hex's rock direction
-- into one of eight compass classes and learn a palette per class from the shipped contours. Within
-- a class the mappers use many interchangeable variants for texture, so we keep the whole per-class
-- distribution and sample it with the seeded rng rather than collapsing to the modal piece (which
-- reads as a monotonous repeat). At placement the class comes from the boundary's SMOOTHED normal
-- (see the wall-ring section), so the family only turns where the boundary does — a face run stays a
-- continuous run of faces, a top run stays tops.

local rockPattern = {} -- [parityKey 0..3] = tile id (from the FIRST reference)
local floorWeights = {} -- walkable floor: { {id=..., w=...}, ... } sorted for determinism
-- Wall pieces are chosen by LOCAL BOUNDARY GEOMETRY, learned from the shipped rims. For each
-- walkable-side contour hex, the sorted pair of directions to its neighbouring rim hexes plus a
-- 6-bit mask of which neighbours are rock fully describes the isometric orientation AND the turn
-- there — the absolute directions encode which diagonal the boundary faces. On held-out cave4 this
-- key predicts the shipped piece's orientation family 79% of the time (vs 53% for the old
-- outward-normal compass bin), so the generated rim "does the same thing" the shipped rims do,
-- without per-piece hand tuning.
-- Corner pieces (ca063, ca032, ca071, ...) are told apart by how the shipped rims join them, not by
-- a hand list: see leftJoins/rightJoins below.
local geomRock = {} -- rock-neighbour mask -> weighted { {pid=, w=}, ... }: the PRIMARY key. Density-
--                     independent (rock is rock however the rim is spaced), so it transfers from the
--                     shipped rims to the generated band; learned from faces on BOTH sides of the rim.
local geomModel = {} -- "faceDirs|rockmask" -> weighted { ... }: finer, used when its key is present
local geomDirpair = {} -- "faceDirs"          -> weighted { ... }: coarser fallback
local wallPieces = {} -- compass class       -> weighted { ... }: final fallback (outward-normal bin)
local wallPiecesAll = {} -- all pieces pooled: last resort

-- Wall PANELS. A cave wall's art is a long rock panorama cut into 16/32px strips (ca059, ca060,
-- ca061, ...): it reads as one seamless face only when each strip starts on the pixel after the
-- previous one ends, in the order the artist cut them. Across cave1..4, 93% of faces have a right-hand
-- neighbour starting on exactly the next pixel and 61% of those are the next strip of the same
-- panorama, at a fixed pixel offset per pair. So the rim is laid as chains: learn, from the shipped
-- rims, which strip follows which (`successors`, with the hex step that puts it there), then grow each
-- chain left to right with a learned successor that lands on the rim. Placing a piece per hex picked
-- by local geometry abutted fine but put the next strip of the same panorama beside only 6% of
-- pieces — every seam broke the texture.
local successors = {} -- pid -> weighted { {pid=, w=}, ... }: the strips seen starting right after it
local faceUses = {} -- pid -> how many faces the references stand -- bridges and starts skip rare art
-- How many of those stand on the near side (rock down-screen): there the art is the rock's top seen
-- over its lip, elsewhere a lit face.
local nearUses = {}
local topArt = {} -- pid -> true for rock-top art, decided after learning (see isTopArt)
local function isTopArt(pid)
    return topArt[pid] == true
end
-- A bridge or chain start picks by fit alone, so a rare oddity whose geometry happens to fit (ca078,
-- a dark pillar the four references use twice) would win far more often than the mappers used it.
-- Only art the references use this often is a candidate there; successors keep their learned odds.
local MIN_BRIDGE_USES = 5
-- How often a piece has a strip abutting on its left / right in the shipped rims. Some are run ENDS
-- (ca071, ca032: always a strip on the left, never one on the right — they cap a wall where the rim
-- turns), some run STARTS (ca064, ca036, ca072: the mirror), ca063 stands alone. Placed mid-run they
-- read as a corner standing in the middle of a wall, so they only go where the shipped rims put them.
local leftJoins, rightJoins = {}, {} -- pid -> count
local JOIN_RATE = 0.2 -- below this share of uses, a side counts as never joined
local function joinRate(counts, pid)
    return (counts[pid] or 0) / math.max(faceUses[pid] or 0, 1)
end
local function isRunEnd(pid)
    return joinRate(rightJoins, pid) < JOIN_RATE
end
local function isRunStart(pid)
    return joinRate(leftJoins, pid) < JOIN_RATE
end
local successorStep = {} -- "a>b:parity" -> { dc, dr }: hex step from a to b, by a's column parity

-- Floor decoration: the standing (non-flat) scenery the shipped caves put on open floor, learned per
-- DISTANCE BAND from the nearest wall piece or rock (1 = touches one, ..., DECOR_BANDS = that far or
-- more). Measured from the wall art, not the rock tiles: the art covers floor tiles, and the mappers
-- heap rocks against its foot. Across cave1..4 rocks stand on ~8% of the floor hexes touching a wall,
-- ~1% one step out and almost none in the open, while stalagmites and weeds spread wider. Each band
-- keeps its own density (pieces per floor hex) and weighted palette, sampled with the seeded rng.
-- Band 0 is the walkable-side wall hexes themselves (the shipped maps heap Rocks and Dirt on the
-- wall's foot); the open floor beyond DECOR_BANDS - 1 steps is pooled, since the references have few
-- such hexes and a thin sample overstates what grows in the middle of a big chamber.
local DECOR_BANDS = 4
-- Half the references' density by default: their full count (~80 pieces a cave) reads as clutter on a
-- generated cavern. --arg decor=1 restores it.
local DECOR_SCALE = tonumber(args.decor) or 0.5
local decorDensity = {} -- band (1..DECOR_BANDS, or "0"..compass for wall hexes) -> pieces per hex
local decorPalette = {} -- band -> weighted { {pid=, w=}, ... }

-- The cavern floor palette is the cav* ground tiles only; the shipped maps blend edg50xx SAND at
-- their entrance alcoves, and counting those into the mix speckles the cave floor with sand.
local cavTileIds = {}
for _, id in pairs(api:tilesByPrefix("cav")) do
    cavTileIds[id] = true
end

-- Even among cav* tiles some are transition/border tiles (e.g. cav4013, the dirt<->sand seam) the
-- mappers use only at specific edges — scattering them across open floor looks wrong. Keep only the
-- FIELD tiles the reference uses pervasively: those at or above this fraction of the most-used
-- walkable tile's count. The rare tail (borders, one-off detail) is dropped from the random mix.
local FLOOR_FIELD_FRACTION = tonumber(args.floorFieldFraction) or 0.1

-- The "Cave Wall" proto family holds two kinds of art: standing wall FACES that line the boundary,
-- and OBJECT_FLAT rock-fill textures (proto 621/622 and friends) that carpet solid ground. Only
-- faces belong on the rim; the flat fill, learned in, fills the wall run with dark rubble and
-- breaks it up. Cache the flag — protoFlat loads the proto.
local flatCache = {}
local function isFlatWall(pid)
    if flatCache[pid] == nil then
        flatCache[pid] = api:protoFlat(pid)
    end
    return flatCache[pid]
end

-- Screen unit vector (x right+, y down+) of each cube hex direction, derived from the engine's
-- HexagonGrid hex->pixel formula. The grid is anisotropic (16x12), so the six directions are NOT
-- evenly spaced in screen angle — these exact vectors are what let us read a hex's outward normal.
local DIR_VEC = {
    [0] = { -16, 12 }, [1] = { -32, 0 }, [2] = { -16, -12 },
    [3] = { 16, -12 }, [4] = { 32, 0 }, [5] = { 16, 12 },
}
local COMPASS = { "E", "NE", "N", "NW", "W", "SW", "S", "SE" } -- 45-degree bins, 0=E going CCW

-- A wall hex's OUTWARD-NORMAL compass class: the screen direction the summed rock-direction vectors
-- point (the way the solid rock lies). This is a far sharper predictor of which "Cave Wall" art the
-- mappers used than the raw rock-direction set — rock up-screen (N/NE) shows a standing FACE, rock
-- down-screen (S/SW) shows the filled TOP, so the class picks the family. Screen y is DOWN, so North
-- is -y.
local function compassOf(dirs)
    local vx, vy = 0, 0
    for _, d in ipairs(dirs) do
        vx += DIR_VEC[d][1]
        vy += DIR_VEC[d][2]
    end
    if vx == 0 and vy == 0 then
        return "-"
    end
    local ang = math.deg(math.atan2(-vy, vx))
    if ang < 0 then
        ang += 360
    end
    return COMPASS[math.floor((ang + 22.5) / 45) % 8 + 1]
end

do
    local weightById = {}
    local geomRockCounts = {} -- rockmask -> { [pid] = count }: primary key (density-independent)
    local geomCounts = {} -- "faceDirs|rockmask" -> { [pid] = count }: finer key
    local dirpairCounts = {} -- "faceDirs" -> { [pid] = count }: coarser fallback key
    local normalCounts = {} -- compass class -> { [pid] = count }: final-fallback bin
    local pidTotals = {} -- pid -> total count, for the last-resort pool
    local decorCounts = {} -- band -> { [pid] = count }: standing scenery on walkable floor
    local successorCounts = {} -- pid -> { [next pid] = count }: panel successors
    local decorHexes = {} -- band -> walkable hexes at that distance from the rock
    for refIndex, ref in ipairs(REFS) do
        local floor = api:mapFloorAt(ref, REF_ELEVATION)

        -- This reference's rock pattern: the most common tile per (row%2, col%2) parity cell.
        local parityCounts = { {}, {}, {}, {} }
        for t = 0, TILE_W * TILE_H - 1 do
            local id = floor[t + 1]
            if id ~= EMPTY then
                local parity = (t // TILE_W) % 2 * 2 + t % 2
                parityCounts[parity + 1][id] = (parityCounts[parity + 1][id] or 0) + 1
            end
        end
        local isRock = {}
        for parity = 0, 3 do
            local bestId, bestCount = EMPTY, 0
            for id, count in pairs(parityCounts[parity + 1]) do
                if count > bestCount or (count == bestCount and id < bestId) then
                    bestId, bestCount = id, count
                end
            end
            if refIndex == 1 then
                rockPattern[parity] = bestId -- the generated map fills with the first ref's rock
            end
            isRock[bestId] = true
        end

        -- Weighted walkable-floor mix: the primary reference's non-empty, non-rock, cav* tiles.
        -- The palette and the rock pattern must come from the same map or the cavern floor clashes
        -- with the rock it is carved from; the other references only contribute wall shapes.
        if refIndex == 1 then
            for t = 1, TILE_W * TILE_H do
                local id = floor[t]
                if id ~= EMPTY and cavTileIds[id] and not isRock[id] then
                    weightById[id] = (weightById[id] or 0) + 1
                end
            end
        end

        -- Learn each boundary wall by its OUTWARD-NORMAL compass class, on the walkable-side contour.
        -- The contour carries the whole rim: standing FACES where the rock is up-screen (N/E), filled
        -- TOPS where it is down-screen (S/W). OBJECT_FLAT rock-fill art (block-carpet over solid
        -- ground) is skipped — it is not a boundary piece.
        local function walkableHex(hex)
            local t = api:hexTile(hex)
            if t < 0 then
                return false
            end
            local id = floor[t + 1]
            return id ~= EMPTY and not isRock[id]
        end
        local faceAt = {}
        local walls = api:mapObjectsAt(ref, REF_ELEVATION, "wall")
        for i = 1, #walls, 3 do
            local pid, hex = walls[i], walls[i + 1]
            if not isFlatWall(pid) then
                faceAt[hex] = pid
            end
        end

        -- Which strip starts on the pixel after each face ends: the panel successors.
        local byLeft = {} -- screen left edge -> { {pid=, hex=, rect=}, ... }
        local faces = {}
        for hex, pid in pairs(faceAt) do
            faceUses[pid] = (faceUses[pid] or 0) + 1
            -- Two rings out: the shipped floor edge is jagged, and one ring misreads a lip's corners.
            local vy = 0
            for _, n in ipairs(api:hexNeighbors(hex)) do
                local d = api:hexDir(hex, n)
                for _, m in ipairs(api:hexNeighbors(n)) do
                    local dy = DIR_VEC[d][2] + DIR_VEC[api:hexDir(n, m)][2]
                    vy += walkableHex(m) and -dy or dy
                end
            end
            if vy > 0 then
                nearUses[pid] = (nearUses[pid] or 0) + 1
            end
            local face = { pid = pid, hex = hex, rect = api:artRect(pid, hex) }
            table.insert(faces, face)
            byLeft[face.rect[1]] = byLeft[face.rect[1]] or {}
            table.insert(byLeft[face.rect[1]], face)
        end
        for _, a in ipairs(faces) do
            local best, bestDy = nil, math.huge
            for _, b in ipairs(byLeft[a.rect[3] + 1] or {}) do
                local overlap = math.min(a.rect[4], b.rect[4]) - math.max(a.rect[2], b.rect[2])
                local dy = math.abs(b.rect[4] - a.rect[4])
                if overlap > 20 and (dy < bestDy or (dy == bestDy and b.hex < best.hex)) then
                    best, bestDy = b, dy
                end
            end
            if best ~= nil then
                rightJoins[a.pid] = (rightJoins[a.pid] or 0) + 1
                leftJoins[best.pid] = (leftJoins[best.pid] or 0) + 1
                successorCounts[a.pid] = successorCounts[a.pid] or {}
                successorCounts[a.pid][best.pid] = (successorCounts[a.pid][best.pid] or 0) + 1
                local key = string.format("%d>%d:%d", a.pid, best.pid, api:hexCol(a.hex) % 2)
                successorStep[key] = { api:hexCol(best.hex) - api:hexCol(a.hex), api:hexRow(best.hex) - api:hexRow(a.hex) }
            end
        end
        -- Learn from EVERY face — on the walkable side AND the rock side of the rim, as the shipped
        -- maps place them (cave1: 327 walkable + 175 rock). The local geometry of a face is the mask
        -- of which neighbours are rock (which side, hence the isometric orientation — this is the
        -- primary, density-independent key) plus the directions to neighbouring faces (a finer key).
        for hex, pid in pairs(faceAt) do
            local rockmask, rockDirs, faceDirs = 0, {}, {}
            for _, n in ipairs(api:hexNeighbors(hex)) do
                local d = api:hexDir(hex, n)
                if not walkableHex(n) then
                    rockmask += 2 ^ d
                    table.insert(rockDirs, d)
                end
                if faceAt[n] ~= nil then
                    table.insert(faceDirs, d)
                end
            end
            -- Contour hexes only (front some rock, but are not a fully-enclosed pocket).
            if #rockDirs > 0 and #rockDirs < 6 then
                table.sort(faceDirs)
                local dirpair = table.concat(faceDirs, ",")
                local rm = tostring(rockmask)
                geomRockCounts[rm] = geomRockCounts[rm] or {}
                geomRockCounts[rm][pid] = (geomRockCounts[rm][pid] or 0) + 1
                local geom = dirpair .. "|" .. rm
                geomCounts[geom] = geomCounts[geom] or {}
                geomCounts[geom][pid] = (geomCounts[geom][pid] or 0) + 1
                dirpairCounts[dirpair] = dirpairCounts[dirpair] or {}
                dirpairCounts[dirpair][pid] = (dirpairCounts[dirpair][pid] or 0) + 1
                local comp = compassOf(rockDirs)
                normalCounts[comp] = normalCounts[comp] or {}
                normalCounts[comp][pid] = (normalCounts[comp][pid] or 0) + 1
                pidTotals[pid] = (pidTotals[pid] or 0) + 1
            end
        end

        -- Floor decoration by distance from the walls: grow the distance over open floor (walkable,
        -- no wall piece) from the hexes touching a wall or rock, then count the standing scenery on
        -- each band and the band's size.
        local wallHex = {}
        for i = 1, #walls, 3 do
            wallHex[walls[i + 1]] = true
        end
        local function openHex(hex)
            return walkableHex(hex) and not wallHex[hex]
        end
        local dist, frontier = {}, {}
        for hex = 0, 39999 do
            if openHex(hex) then
                for _, n in ipairs(api:hexNeighbors(hex)) do
                    if not openHex(n) then
                        dist[hex] = 1
                        table.insert(frontier, hex)
                        break
                    end
                end
            end
        end
        for band = 2, DECOR_BANDS do
            local nextFrontier = {}
            for _, hex in ipairs(frontier) do
                for _, n in ipairs(api:hexNeighbors(hex)) do
                    if dist[n] == nil and openHex(n) then
                        dist[n] = band
                        table.insert(nextFrontier, n)
                    end
                end
            end
            frontier = nextFrontier
        end
        -- A wall hex's band is keyed by its outward-normal compass class too: rocks heap at the foot
        -- of a standing face, not on the filled top of the near lip.
        local function bandOf(hex)
            if wallHex[hex] then
                if not walkableHex(hex) then
                    return nil
                end
                local rockDirs = {}
                for _, n in ipairs(api:hexNeighbors(hex)) do
                    if not walkableHex(n) then
                        table.insert(rockDirs, api:hexDir(hex, n))
                    end
                end
                return "0" .. compassOf(rockDirs)
            end
            return openHex(hex) and (dist[hex] or DECOR_BANDS) or nil
        end
        for hex = 0, 39999 do
            local band = bandOf(hex)
            if band ~= nil then
                decorHexes[band] = (decorHexes[band] or 0) + 1
            end
        end
        local scenery = api:mapObjectsAt(ref, REF_ELEVATION, "scenery")
        for i = 1, #scenery, 3 do
            local pid, hex = scenery[i], scenery[i + 1]
            -- Blockers, lights and exit markers are flat; what stands on the floor is the decoration.
            local band = bandOf(hex)
            if band ~= nil and not isFlatWall(pid) then
                decorCounts[band] = decorCounts[band] or {}
                decorCounts[band][pid] = (decorCounts[band][pid] or 0) + 1
            end
        end
    end

    for pid, counts in pairs(successorCounts) do
        local list = {}
        for next, count in pairs(counts) do
            table.insert(list, { pid = next, w = count })
        end
        table.sort(list, function(a, b)
            return a.pid < b.pid
        end)
        successors[pid] = list
    end

    -- Rock-top art is what the references stand on the near side. A run's opening piece stands
    -- where the rim turns, so its own spots say little: it takes the family of the strips it opens.
    local function nearRate(pid)
        return (nearUses[pid] or 0) / math.max(faceUses[pid] or 0, 1)
    end
    for pid in pairs(faceUses) do
        local rate = nearRate(pid)
        if joinRate(leftJoins, pid) < JOIN_RATE and (rightJoins[pid] or 0) > 0 then
            local sum, n = 0, 0
            for next, count in pairs(successorCounts[pid] or {}) do
                sum += nearRate(next) * count
                n += count
            end
            rate = sum / n
        end
        topArt[pid] = rate > 0.5
    end

    for band, hexes in pairs(decorHexes) do
        local pieces, list = 0, {}
        for pid, count in pairs(decorCounts[band] or {}) do
            pieces += count
            table.insert(list, { pid = pid, w = count })
        end
        table.sort(list, function(a, b)
            return a.pid < b.pid
        end)
        decorPalette[band] = list
        decorDensity[band] = pieces / hexes
    end

    -- Keep only the pervasive field tiles: drop any below FLOOR_FIELD_FRACTION of the most-used
    -- walkable tile so border/transition tiles (used a handful of times) never scatter over the floor.
    local maxWeight = 0
    for _, w in pairs(weightById) do
        maxWeight = math.max(maxWeight, w)
    end
    local fieldFloor = maxWeight * FLOOR_FIELD_FRACTION
    for id, w in pairs(weightById) do
        if w >= fieldFloor then
            table.insert(floorWeights, { id = id, w = w })
        end
    end
    table.sort(floorWeights, function(a, b) return a.id < b.id end) -- pairs() order is unspecified
    assert(#floorWeights > 0, "no walkable cav* floor tiles found in the reference maps")

    -- Turn each orientation's { pid -> count } into a weighted list, sorted by pid so the seeded
    -- draw is reproducible (pairs() order is unspecified).
    local function weightedList(counts)
        local list = {}
        for pid, w in pairs(counts) do
            table.insert(list, { pid = pid, w = w })
        end
        table.sort(list, function(a, b) return a.pid < b.pid end)
        return list
    end
    -- Build the per-class palettes, applying the hand-authored overrides: `only` pieces appear only
    -- in their allowed classes, `drop`/`corner` pieces are pulled from the straight-run palettes
    -- entirely (corners are re-added as a separate turn-only set carrying their learned edges).
    -- Fold each key's { pid -> count } into a weighted list, sorted by pid so the seeded draw is
    -- reproducible (pairs() order is unspecified).
    local function foldModel(counts, into)
        for key, pids in pairs(counts) do
            into[key] = weightedList(pids)
        end
    end
    foldModel(geomRockCounts, geomRock)
    foldModel(geomCounts, geomModel)
    foldModel(dirpairCounts, geomDirpair)
    foldModel(normalCounts, wallPieces)
    wallPiecesAll = weightedList(pidTotals) -- last-resort pool
    assert(#wallPiecesAll > 0, "no walls found on the reference maps")

    local rockKeys = 0
    for _ in pairs(geomRock) do
        rockKeys += 1
    end
    print(string.format("Learned %d rock-mask wall geometries and %d cav* floor tiles from %d reference map(s).",
        rockKeys, #floorWeights, #REFS))
end

-- The wall palette for an outward-normal compass class, the global pool as a last resort.
local function wallListFor(comp)
    return wallPieces[comp] or wallPiecesAll
end

-- Weighted seeded draw from a { {pid=, w=}, ... } list (same scheme as pickFloor).
local function pickWall(list)
    local total = 0
    for _, e in ipairs(list) do
        total += e.w
    end
    local r = api:rng() * total
    for _, e in ipairs(list) do
        r -= e.w
        if r <= 0 then
            return e.pid
        end
    end
    return list[#list].pid
end

local function pickFloor()
    local total = 0
    for _, e in ipairs(floorWeights) do
        total += e.w
    end
    local r = api:rng() * total
    for _, e in ipairs(floorWeights) do
        r -= e.w
        if r <= 0 then
            return e.id
        end
    end
    return floorWeights[#floorWeights].id
end

-- ============================ carve the cavern (grid-axis rooms) ==============
-- The cavern is a union of rectangles in TILE space: each chamber a room a dozen tiles across with a
-- bay stepping its outline, consecutive chambers joined by a straight corridor. A tile
-- rectangle's sides run along the two iso axes on screen, the only directions the wall art follows:
-- shipped panels climb at most ~0.5px per px going right (the column axis) and fall ~0.75px per px
-- (the row axis), and the shipped caves are networks of straight corridors along those two axes. An
-- edge running any other way (a rounded corner, a noisy wiggle) is a staircase of one-tile jogs that
-- no strip spans, so the rim broke into loose slabs there, faced with art the shipped caves only use
-- on corner tips.
local carved = {} -- [tileIndex] = true
local chambers = {} -- centres, in tiles
local MARGIN = 22
-- The layout is drawn on a lattice of CELL x CELL tiles, so every stretch of edge between two corners
-- is at least a cell long: a one- or two-tile jog is two corners closer together than any wall run
-- can turn, and showed as a dark notch or a loose slab.
local CELL = 3
local ROOM = { tonumber(args.roomMin) or 1, tonumber(args.roomMax) or 2 } -- chamber half-size, cells
local BAYS = tonumber(args.bays) or 1 -- bays per chamber
local CELL_MIN, CELL_MAX = MARGIN // CELL + 1, (TILE_W - MARGIN) // CELL - 1

local cells = {} -- [cy * TILE_W + cx] = true
local function cellKey(x, y)
    return y * TILE_W + x
end
local function clampCell(v)
    return math.max(CELL_MIN, math.min(CELL_MAX, v))
end
local function carveCells(x0, y0, x1, y1)
    for y = clampCell(math.min(y0, y1)), clampCell(math.max(y0, y1)) do
        for x = clampCell(math.min(x0, x1)), clampCell(math.max(x0, x1)) do
            cells[cellKey(x, y)] = true
        end
    end
end

-- Chamber centres random-walk from the middle along ONE axis per step, so each corridor is straight.
local cx, cy = 50 // CELL, 50 // CELL
local centres = {}
for _ = 1, CHAMBERS do
    table.insert(centres, { x = cx, y = cy })
    local hx, hy = api:rngInt(ROOM[1], ROOM[2]), api:rngInt(ROOM[1], ROOM[2])
    carveCells(cx - hx, cy - hy, cx + hx, cy + hy)
    -- A bay: a smaller room pushed out across one side, so the outline steps.
    for _ = 1, BAYS do
        local bx, by = api:rngInt(0, 1), api:rngInt(0, 1)
        local ox, oy = api:rngInt(-hx, hx), api:rngInt(-hy, hy)
        local side = api:rng() < 0.5 and -1 or 1
        if api:rng() < 0.5 then
            ox = side * (hx + 1)
        else
            oy = side * (hy + 1)
        end
        carveCells(cx + ox - bx, cy + oy - by, cx + ox + bx, cy + oy + by)
    end
    local step = api:rngInt(3, 5) * (api:rng() < 0.5 and -1 or 1)
    if api:rng() < 0.5 then
        cx = clampCell(cx + step)
    else
        cy = clampCell(cy + step)
    end
end
for i = 2, #centres do -- one-cell corridors between consecutive chambers
    local a, b = centres[i - 1], centres[i]
    carveCells(a.x, a.y, b.x, b.y)
end
-- A one-cell sliver of rock between two stretches of floor reads as a box standing in the cavern,
-- faced on one side and topped on the other; the shipped caves keep their rock thick. Open it.
local opened = true
while opened do
    opened = false
    for y = CELL_MIN, CELL_MAX do
        for x = CELL_MIN, CELL_MAX do
            local k = cellKey(x, y)
            if not cells[k] and ((cells[cellKey(x - 1, y)] and cells[cellKey(x + 1, y)]) or (cells[cellKey(x, y - 1)] and cells[cellKey(x, y + 1)])) then
                cells[k] = true
                opened = true
            end
        end
    end
end
-- Two cells meeting only at a corner pinch the floor to a point; open one of the other two.
for y = CELL_MIN, CELL_MAX - 1 do
    for x = CELL_MIN, CELL_MAX - 1 do
        local a, b = cells[cellKey(x, y)], cells[cellKey(x + 1, y + 1)]
        local c, d = cells[cellKey(x + 1, y)], cells[cellKey(x, y + 1)]
        if (a and b and not c and not d) or (c and d and not a and not b) then
            cells[cellKey(x + 1, y)] = true
        end
    end
end
for key in pairs(cells) do
    local x, y = key % TILE_W, key // TILE_W
    for row = y * CELL, y * CELL + CELL - 1 do
        for col = x * CELL, x * CELL + CELL - 1 do
            carved[api:tileIndex(col, row)] = true
        end
    end
end
for _, c in ipairs(centres) do
    table.insert(chambers, { x = c.x * CELL + 1, y = c.y * CELL + 1 })
end

-- Paint: rock pattern everywhere, then the weighted walkable mix over the carved set.
for t = 0, TILE_W * TILE_H - 1 do
    if carved[t] then
        api:paintFloor(t, pickFloor())
    else
        local parity = (t // TILE_W) % 2 * 2 + t % 2
        api:paintFloor(t, rockPattern[parity])
    end
end
local carvedCount = 0
for _ in pairs(carved) do
    carvedCount += 1
end
print(string.format("Carved %d chambers (%d walkable tiles) into the rock.", #chambers, carvedCount))

-- ============================ wall band ======================================
-- The rim is the 2-hex band straddling the floor's edge, where walls stand.
local hexTileCache = {}
local function carvedHex(hex)
    local t = hexTileCache[hex]
    if t == nil then
        t = api:hexTile(hex)
        hexTileCache[hex] = t
    end
    return t >= 0 and carved[t] == true
end

-- Compass bin of a screen vector (screen y is DOWN, so North is -y).
local function compassFromVec(nx, ny)
    if nx == 0 and ny == 0 then
        return "-"
    end
    local ang = math.deg(math.atan2(-ny, nx))
    if ang < 0 then
        ang += 360
    end
    return COMPASS[math.floor((ang + 22.5) / 45) % 8 + 1]
end

-- Outward screen normal at a hex: the way the rock lies around it, summed over two rings of
-- neighbours so it follows the side of the room rather than one hex's corner.
local function outwardNormal(hex)
    local nx, ny = 0, 0
    for _, n in ipairs(api:hexNeighbors(hex)) do
        local d = DIR_VEC[api:hexDir(hex, n)]
        for _, m in ipairs(api:hexNeighbors(n)) do
            if not carvedHex(m) then
                local e = DIR_VEC[api:hexDir(n, m)]
                nx += d[1] + e[1]
                ny += d[2] + e[2]
            end
        end
    end
    return nx, ny
end

-- Candidate hexes: everything on the carved tiles and their immediate rock neighbours — the strip the
-- edge runs through — collected into a sorted list so placement order (hence the saved map) is
-- deterministic.
local candSet = {}
for t in pairs(carved) do
    for _, hex in ipairs(api:tileHexes(t)) do
        candSet[hex] = true
    end
    local col, row = t % TILE_W, t // TILE_W
    for dr = -1, 1 do
        for dc = -1, 1 do
            local nt = api:tileIndex(col + dc, row + dr)
            if nt >= 0 and not carved[nt] then
                for _, hex in ipairs(api:tileHexes(nt)) do
                    candSet[hex] = true
                end
            end
        end
    end
end
local candidates = {}
for hex in pairs(candSet) do
    table.insert(candidates, hex)
end
table.sort(candidates)

-- The rim: every hex whose floor tile is carved while a neighbour's isn't, or the other way round.
-- It follows the carved TILES: walls laid off the tile edge left a strip of rock floor (the cav4009
-- fill) between the wall's foot and the floor.
local rimSet = {}
local rim = {}
for _, hex in ipairs(candidates) do
    local me = carvedHex(hex)
    for _, n in ipairs(api:hexNeighbors(hex)) do
        if carvedHex(n) ~= me then
            rimSet[hex] = true
            table.insert(rim, hex)
            break
        end
    end
end

-- The local geometry key at a generated rim hex, the SAME features the shipped faces were learned
-- by: the rock-neighbour mask (primary — density-independent, so it transfers to the dense band) and
-- the directions to neighbouring rim hexes (finer).
local function geomKeyAt(hex)
    local rockmask, faceDirs = 0, {}
    for _, n in ipairs(api:hexNeighbors(hex)) do
        local d = api:hexDir(hex, n)
        if not carvedHex(n) then
            rockmask += 2 ^ d
        end
        if rimSet[n] then
            table.insert(faceDirs, d)
        end
    end
    table.sort(faceDirs)
    local dirpair = table.concat(faceDirs, ",")
    local rm = tostring(rockmask)
    return rm, dirpair .. "|" .. rm, dirpair
end

-- Screen position of a hex, as the engine projects it (tile.cc tileToScreenXY), the origin of the
-- frame api:artRect measures in.
local function hexScreenX(hex)
    local col = 199 - api:hexCol(hex)
    return 48 * (col // 2) + 16 * api:hexRow(hex) + (col % 2 == 1 and 32 or 0)
end
local function hexScreenY(hex)
    return 12 * api:hexRow(hex) - 12 * ((199 - api:hexCol(hex)) // 2)
end

-- Lay the rim as panel chains. Walk the band left to right on screen; at the first hex no chain covers
-- yet, start a chain with a piece for that spot's geometry (the learned per-geometry palettes), then
-- keep appending a learned successor — the strip that starts on the next pixel — as long as one lands
-- on the band. A placed strip claims its hex and the band hexes around it, so the 2-hex band gets one
-- chain, not two side by side; where no successor fits, the chain ends and the walk moves on.
local walls = 0
local wallAt = {} -- hex -> the rim piece standing there
local wallRects = {} -- { rect = engine art rect, near = faces the viewer } per placed piece
-- Does the rock at a rim hex lie down-screen of the floor (the near side, drawn as the rock's top)?
local function nearSide(hex)
    local vy = 0
    for _, n in ipairs(api:hexNeighbors(hex)) do
        local dy = DIR_VEC[api:hexDir(hex, n)][2]
        vy += carvedHex(n) and -dy or dy
    end
    return vy > 0
end
local claimedBy = {} -- band hex -> the chain that covers it
local boundaryHexes = #rim
local chains, links = 0, 0
local order = table.clone(rim)
table.sort(order, function(a, b)
    local ax, bx = hexScreenX(a), hexScreenX(b)
    if ax ~= bx then
        return ax < bx
    end
    return a < b
end)

-- The learned palette for a rim hex's geometry, most specific key first.
local function paletteAt(hex)
    local rm, geom, dirpair = geomKeyAt(hex)
    return geomRock[rm] or geomModel[geom] or geomDirpair[dirpair] or wallListFor(compassFromVec(outwardNormal(hex)))
end

local function place(pid, hex, chain)
    if not api:placeProto(pid, hex, 0) then
        return false
    end
    walls += 1
    wallAt[hex] = pid
    claimedBy[hex] = chain
    table.insert(wallRects, { rect = api:artRect(pid, hex), near = nearSide(hex) })
    for _, n in ipairs(api:hexNeighbors(hex)) do
        if rimSet[n] and claimedBy[n] == nil then
            claimedBy[n] = chain
        end
    end
    return true
end

-- The successors of `pid` that, placed after it at `hex`, land on the band and aren't another
-- chain's, each with its hex.
local function fittingSuccessors(pid, hex, chain)
    local fits, hexOf = {}, {}
    local col, row = api:hexCol(hex), api:hexRow(hex)
    for _, e in ipairs(successors[pid] or {}) do
        local step = successorStep[string.format("%d>%d:%d", pid, e.pid, col % 2)]
        local next = step and api:hexIndex(col + step[1], row + step[2]) or -1
        if next >= 0 and rimSet[next] and not wallAt[next] and (claimedBy[next] == nil or claimedBy[next] == chain) then
            table.insert(fits, e)
            hexOf[e.pid] = next
        end
    end
    -- A run-end piece caps the wall only where nothing else can carry it on (where the rim turns); a
    -- run-start piece opens a wall, so it never continues one.
    local carryOn, caps = {}, {}
    for _, e in ipairs(fits) do
        if isRunEnd(e.pid) then
            table.insert(caps, e)
        elseif not isRunStart(e.pid) then
            table.insert(carryOn, e)
        end
    end
    return #carryOn > 0 and carryOn or caps, hexOf
end

-- Pieces fit to join a run in its middle: common enough, and neither a run end nor a run start.
local function midRun(pid)
    return (faceUses[pid] or 0) >= MIN_BRIDGE_USES and not isRunEnd(pid) and not isRunStart(pid)
end

-- When no learned successor fits, keep the rim pixel-continuous anyway: any piece from a nearby band
-- hex's geometry palette whose art starts on the next pixel, its base closest to this strip's. The
-- texture seam there is like the shipped rims' own joins between panoramas.
local function placeable(hex, chain)
    return rimSet[hex] and not wallAt[hex] and (claimedBy[hex] == nil or claimedBy[hex] == chain)
end

-- Drop candidates repeating one of the chain's last two pieces, when anything else fits: on a slope
-- only a few pieces follow, and drawing among them by usage alone settled into a comb (one piece after
-- itself, or two alternating) that the shipped rims never show (4 of 1238 joins repeat a piece).
local function freshFits(fits, recent)
    local fresh = {}
    for _, f in ipairs(fits) do
        if not recent[f.pid] then
            table.insert(fresh, f)
        end
    end
    return #fresh > 0 and fresh or fits
end

-- Among fits {pid=, dy=, w=, ...}, those whose base is within a few pixels of the best, drawn by how
-- often the references use them: the closest fit alone would repeat one piece all along a run.
local FIT_SLACK = 3
-- How far a bridge looks for the next strip, and how far apart two joined strips' bases may be (the
-- shipped rims' own joins step by up to 16px).
local BRIDGE_RINGS = tonumber(args.bridgeRings) or 2
local MAX_JOIN_DY = tonumber(args.maxJoinDy) or 16
-- Where no strip starts on the next pixel, one overlapping the last by up to this much still hides the
-- rock behind; a gap would show it as a dark slit.
local MAX_OVERLAP = tonumber(args.maxOverlap) or 16
local function closeFit(fits)
    local bestDy = math.huge
    for _, f in ipairs(fits) do
        bestDy = math.min(bestDy, f.dy)
    end
    local near = {}
    for _, f in ipairs(fits) do
        if f.dy <= bestDy + FIT_SLACK then
            table.insert(near, f)
        end
    end
    if #near == 0 then
        return nil
    end
    local total = 0
    for _, f in ipairs(near) do
        total += f.w
    end
    local r = api:rng() * total
    for _, f in ipairs(near) do
        r -= f.w
        if r <= 0 then
            return f
        end
    end
    return near[#near]
end

local function bridge(pid, hex, chain, recent)
    local rect = api:artRect(pid, hex)
    local seen, nearby, frontier = { [hex] = true }, {}, { hex }
    for _ = 1, BRIDGE_RINGS do
        local nextFrontier = {}
        for _, f in ipairs(frontier) do
            for _, m in ipairs(api:hexNeighbors(f)) do
                if not seen[m] then
                    seen[m] = true
                    table.insert(nearby, m)
                    table.insert(nextFrontier, m)
                end
            end
        end
        frontier = nextFrontier
    end
    table.sort(nearby)
    local fits = {} -- { pid=, hex=, dy=, w= }: common pieces starting on the next pixel, bases <= 16px apart
    for _, n in ipairs(nearby) do
        if placeable(n, chain) then
            for _, e in ipairs(paletteAt(n)) do
                if midRun(e.pid) then
                    local r = api:artRect(e.pid, n)
                    local dy = math.abs(r[4] - rect[4])
                    local overlap = rect[3] + 1 - r[1] -- 0 = starts on the next pixel
                    if overlap >= 0 and overlap <= MAX_OVERLAP and r[3] > rect[3] and dy <= MAX_JOIN_DY then
                        -- An exact join beats any overlap; among overlaps, the smaller the better.
                        table.insert(fits, { pid = e.pid, hex = n, dy = dy + overlap * 4, w = faceUses[e.pid], overlap = overlap })
                    end
                end
            end
        end
    end
    local choice = closeFit(freshFits(fits, recent or {}))
    if choice == nil then
        return nil, nil
    end
    return choice.pid, choice.hex
end

-- A chain's first strip: where a strip already stands just left of `start`, a palette piece that
-- starts on the pixel after it (closest base), so a new chain continues the rim instead of opening a
-- slit; otherwise a piece for the spot's geometry.
local function startPiece(start)
    local palette = paletteAt(start)
    local seen, lefts = { [start] = true }, {}
    for _, n in ipairs(api:hexNeighbors(start)) do
        for _, m in ipairs({ n, table.unpack(api:hexNeighbors(n)) }) do
            if not seen[m] and wallAt[m] ~= nil then
                seen[m] = true
                table.insert(lefts, api:artRect(wallAt[m], m))
            end
        end
    end
    -- Continuing a strip to the left takes a mid-run piece; a chain with nothing to its left may open
    -- with a run-start piece, never with a run end.
    local continuing, opening = {}, {}
    for _, e in ipairs(palette) do
        if midRun(e.pid) then
            table.insert(continuing, e)
        end
        if (faceUses[e.pid] or 0) >= MIN_BRIDGE_USES and not isRunEnd(e.pid) then
            table.insert(opening, e)
        end
    end
    local fits = {}
    for _, e in ipairs(continuing) do
        local r = api:artRect(e.pid, start)
        for _, l in ipairs(lefts) do
            local dy = math.abs(r[4] - l[4])
            if r[1] == l[3] + 1 and dy <= 16 then
                table.insert(fits, { pid = e.pid, dy = dy, w = faceUses[e.pid] })
            end
        end
    end
    local choice = closeFit(fits)
    if choice ~= nil then
        return choice.pid, true
    end
    -- An opening piece butting against a strip on its left would show its start edge mid-wall.
    local clear = {}
    for _, e in ipairs(opening) do
        local butts = false
        if isRunStart(e.pid) then
            local r = api:artRect(e.pid, start)
            for _, l in ipairs(lefts) do
                butts = butts or (r[1] == l[3] + 1 and math.abs(r[4] - l[4]) <= 16)
            end
        end
        if not butts then
            table.insert(clear, e)
        end
    end
    return pickWall(#clear > 0 and clear or #opening > 0 and opening or palette), false
end

-- The cap candidates: common pieces the shipped rims use only to open or close a run.
local capPieces = {}
for pid, uses in pairs(faceUses) do
    if uses >= MIN_BRIDGE_USES and (isRunEnd(pid) or isRunStart(pid)) then
        table.insert(capPieces, { pid = pid, w = uses })
    end
end
table.sort(capPieces, function(a, b)
    return a.pid < b.pid
end)

-- A free spot for a run's opening or closing strip: on the rim band, or for a rock top's rounded end
-- (ca063, ca064) on the rock beyond it, where it blends in. A face cap out there stood as a post.
local function capSpot(hex, chain, top)
    return hex >= 0 and not wallAt[hex] and (claimedBy[hex] == nil or claimedBy[hex] == chain) and (rimSet[hex] or top and not carvedHex(hex))
end

-- Run caps: the shipped rims close about half their runs with a piece drawn as an end (ca063, ca032,
-- ca071: never a strip on their right) and open them with one drawn as a start (ca063, ca064,
-- ca072, ca036), and use those pieces nowhere else. A run that stops on a mid-run strip shows that
-- strip's straight cut edge instead. A cap goes on a nearby band or rock hex where its art meets
-- the run's end strip on the next pixel or overlaps it a little, the same art family (rock top or
-- face), base closest.
local function capRun(pid, hex, chain, closing)
    local rect = api:artRect(pid, hex)
    local top = isTopArt(pid)
    local seen, nearby, frontier = { [hex] = true }, {}, { hex }
    for _ = 1, BRIDGE_RINGS do
        local nextFrontier = {}
        for _, f in ipairs(frontier) do
            for _, m in ipairs(api:hexNeighbors(f)) do
                if not seen[m] then
                    seen[m] = true
                    table.insert(nearby, m)
                    table.insert(nextFrontier, m)
                end
            end
        end
        frontier = nextFrontier
    end
    table.sort(nearby)
    local fits = {}
    for _, n in ipairs(nearby) do
        if capSpot(n, chain, top) then
            for _, e in ipairs(capPieces) do
                if (closing and isRunEnd(e.pid) or not closing and isRunStart(e.pid)) and isTopArt(e.pid) == top then
                    local r = api:artRect(e.pid, n)
                    -- 0 = meets the run on the next pixel; a cap may also overlap its end strip.
                    local overlap = closing and rect[3] + 1 - r[1] or r[3] + 1 - rect[1]
                    local beyond = closing and r[3] > rect[3] or not closing and r[1] < rect[1]
                    local dy = math.abs(r[4] - rect[4])
                    if overlap >= 0 and overlap <= MAX_OVERLAP and beyond and dy <= MAX_JOIN_DY then
                        table.insert(fits, { pid = e.pid, hex = n, dy = dy + overlap * 4, w = e.w })
                    end
                end
            end
        end
    end
    local choice = closeFit(fits)
    if choice == nil then
        return false
    end
    return place(choice.pid, choice.hex, chain)
end

local bridges, caps = 0, 0
for _, start in ipairs(order) do
    if claimedBy[start] == nil then
        chains += 1
        local pid, continuing = startPiece(start)
        local hex = start
        local before = nil -- the piece before `pid` in this chain
        if place(pid, hex, chains) then
            if not continuing and not isRunStart(pid) and capRun(pid, hex, chains, false) then
                caps += 1
            end
            for _ = 1, #rim do -- a chain can't be longer than the band
                local recent = { [pid] = true }
                if before ~= nil then
                    recent[before] = true
                end
                local fits, hexOf = fittingSuccessors(pid, hex, chains)
                local next, nextHex
                if #fits > 0 then
                    next = pickWall(freshFits(fits, recent))
                    nextHex = hexOf[next]
                    links += 1
                else
                    next, nextHex = bridge(pid, hex, chains, recent)
                    if next == nil then
                        break
                    end
                    bridges += 1
                end
                if not place(next, nextHex, chains) then
                    break
                end
                before, pid, hex = pid, next, nextHex
                if isRunEnd(pid) then
                    break -- it caps the wall: nothing joins on its right
                end
            end
            if not isRunEnd(pid) and capRun(pid, hex, chains, true) then
                caps += 1
            end
        else
            claimedBy[start] = chains
        end
    end
end
print(string.format("Laid the rim as %d wall chains: %d of %d strips continue a panel from the strip before, %d more join it on the next pixel, %d open or close a run.",
    chains, links, walls, bridges, caps))

-- Seal the rock side: every rock hex bordering a walkable hex gets an invisible blocker. The wall
-- art stands on the walkable side and does not by itself stop the engine walking onto the rock —
-- the shipped caves carry hundreds of Secret Blocking Hexes for exactly this. Without the seal the
-- player strolls off the floor into solid rock (and reachability floods the whole grid).
local sealed = {}
local blockers = 0
for t in pairs(carved) do
    for _, hex in ipairs(api:tileHexes(t)) do
        for _, n in ipairs(api:hexNeighbors(hex)) do
            if not sealed[n] and not carvedHex(n) then
                if api:placeProto(SECRET_BLOCKER, n, 0) then
                    sealed[n] = true
                    blockers += 1
                end
            end
        end
    end
end
print(string.format("Lined the rim with %d walls (%d boundary hexes) and sealed the rock side with %d blocking hexes.",
    walls, boundaryHexes, blockers))

-- ============================ scroll blockers =================================
-- Ring the cavern with a screen-space rectangle of scroll blockers, the way every shipped map
-- bounds camera scrolling. Screen extents come from the floor-tile projection (x steps 48/32 px
-- per column/row, y steps 12/24 — the renderer's TILE_* constants).
do
    local minx, maxx, miny, maxy = math.huge, -math.huge, math.huge, -math.huge
    local best, bestDist = nil, math.huge
    for t in pairs(carved) do
        local col, row = t % TILE_W, t // TILE_W
        local sx = (TILE_W - col - 1) * 48 + row * 32
        local sy = row * 24 + col * 12
        minx, maxx = math.min(minx, sx), math.max(maxx, sx)
        miny, maxy = math.min(miny, sy), math.max(maxy, sy)
    end
    local ccx, ccy = (minx + maxx) / 2, (miny + maxy) / 2
    for t in pairs(carved) do -- the carved tile nearest the screen centre anchors the ring
        local col, row = t % TILE_W, t // TILE_W
        local sx = (TILE_W - col - 1) * 48 + row * 32
        local sy = row * 24 + col * 12
        local d = (sx - ccx) ^ 2 + (sy - ccy) ^ 2
        if d < bestDist then
            best, bestDist = t, d
        end
    end
    local centerHex = api:tileHexes(best)[1]
    local ring = api:hexesOnScreenRect(centerHex, math.floor((maxx - minx) / 2) + 220, math.floor((maxy - miny) / 2) + 130)
    local placed = 0
    for _, hex in ipairs(ring) do
        if api:placeProto(SCROLL_BLOCKER, hex, 0) then
            placed += 1
        end
    end
    print(string.format("Scroll-blocker ring: %d markers.", placed))
end

-- ============================ exits + player start ============================
local startTile = api:tileIndex(chambers[1].x, chambers[1].y)
api:setPlayerStart(api:tileHexes(startTile)[1], 0, 0)

if EXIT ~= "none" then
    -- An exit patch on the floor of the last chamber: every hex of a couple of walkable tiles
    -- beside its centre becomes a worldmap exit grid (dest -2), like the encounter maps.
    local exitChamber = chambers[#chambers]
    local placed = 0
    for _, offset in ipairs({ 0, 1 }) do
        local t = api:tileIndex(exitChamber.x + offset, exitChamber.y)
        if t >= 0 and carved[t] then
            for _, hex in ipairs(api:tileHexes(t)) do
                if api:placeExitGrid(hex, -2, 0, 0, 0) then
                    placed += 1
                end
            end
        end
    end
    print(string.format("Exit patch: %d worldmap exit grids in the last chamber.", placed))
end
print(string.format("Player start in the first chamber, tile (%d, %d).", chambers[1].x, chambers[1].y))

-- ============================ floor decoration ================================
-- Rocks, stalagmites, dirt and weeds at the references' density for each distance from the walls.
-- A piece that blocks movement goes only where its walkable neighbours form one unbroken run around
-- it: consecutive hex neighbours touch, so the floor stays connected without that hex and the piece
-- can never cut a passage (or wall off the exit). The start and the exit patch keep a clear ring.
if DECOR_SCALE > 0 then
    local reserved = {}
    local function reserve(t)
        if t >= 0 then
            for _, hex in ipairs(api:tileHexes(t)) do
                reserved[hex] = true
                for _, n in ipairs(api:hexNeighbors(hex)) do
                    reserved[n] = true
                end
            end
        end
    end
    reserve(startTile)
    reserve(api:tileIndex(chambers[#chambers].x, chambers[#chambers].y))
    reserve(api:tileIndex(chambers[#chambers].x + 1, chambers[#chambers].y))

    -- A near-side rim piece is rock seen from above, drawn up over the floor behind its foot: a piece
    -- standing under that art, or just in front of its foot, reads as a rock lying on the rock. A far
    -- wall only hides what stands well behind its foot; rocks at its foot are what the references have.
    local FOOT_MARGIN = 12 -- one hex row
    local function underWallArt(pid, hex)
        local art = api:artRect(pid, hex)
        local fx, fy = (art[1] + art[3]) // 2, art[4] -- where the piece's art meets the ground
        for _, w in ipairs(wallRects) do
            local r = w.rect
            if fx >= r[1] and fx <= r[3] and fy >= r[2] and fy <= r[4] + (w.near and FOOT_MARGIN or -FOOT_MARGIN) then
                return true
            end
        end
        return false
    end

    local blocked = {} -- hex -> true once a blocking decoration stands there
    local function walkableNow(hex)
        return carvedHex(hex) and not wallAt[hex] and not blocked[hex]
    end
    -- Does placing a blocker on `hex` keep its walkable neighbours connected to each other?
    local function keepsPassage(hex)
        local around = {}
        for _, n in ipairs(api:hexNeighbors(hex)) do
            around[api:hexDir(hex, n)] = walkableNow(n)
        end
        local runs = 0
        for d = 0, 5 do
            if around[d] and not around[(d + 5) % 6] then
                runs += 1
            end
        end
        return runs <= 1
    end

    -- Distance from the walls over the open floor, as learned (1 = touches a wall piece or rock).
    local function openHex(hex)
        return carvedHex(hex) and not wallAt[hex]
    end
    local floorHexes = {} -- the open floor, plus the wall hexes standing on it (band 0)
    for t in pairs(carved) do
        for _, hex in ipairs(api:tileHexes(t)) do
            table.insert(floorHexes, hex)
        end
    end
    table.sort(floorHexes) -- pairs() order is not deterministic; the .map must be
    local dist, frontier = {}, {}
    for _, hex in ipairs(floorHexes) do
        if wallAt[hex] then
            dist[hex] = 0
        end
    end
    for _, hex in ipairs(floorHexes) do
        for _, n in ipairs(api:hexNeighbors(hex)) do
            if openHex(hex) and not openHex(n) then
                dist[hex] = 1
                table.insert(frontier, hex)
                break
            end
        end
    end
    for band = 2, DECOR_BANDS do
        local nextFrontier = {}
        for _, hex in ipairs(frontier) do
            for _, n in ipairs(api:hexNeighbors(hex)) do
                if dist[n] == nil and openHex(n) then
                    dist[n] = band
                    table.insert(nextFrontier, n)
                end
            end
        end
        frontier = nextFrontier
    end

    local blocksCache = {}
    local placed = 0
    for _, hex in ipairs(floorHexes) do
        local onWall = wallAt[hex] ~= nil -- already blocked: decoration there can't cut a passage
        local band = onWall and "0" .. compassFromVec(outwardNormal(hex)) or dist[hex] or DECOR_BANDS
        local palette = decorPalette[band] or {}
        if not reserved[hex] and (onWall or walkableNow(hex)) and #palette > 0
            and api:rng() < decorDensity[band] * DECOR_SCALE then
            local pid = pickWall(palette)
            if blocksCache[pid] == nil then
                blocksCache[pid] = api:protoBlocks(pid)
            end
            if (onWall or not blocksCache[pid] or keepsPassage(hex)) and not underWallArt(pid, hex) then
                if api:placeProto(pid, hex, 0) then
                    placed += 1
                    if blocksCache[pid] then
                        blocked[hex] = true
                    end
                end
            end
        end
    end
    print(string.format("Decorated the floor with %d rocks, stalagmites and the like.", placed))
end
