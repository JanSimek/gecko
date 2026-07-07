-- Cave-interior generator, learned from the shipped cave maps.
--
-- Builds a real Fallout 2 cavern the way maps/cave1..4.map elevation 1 are built:
--   * the whole area filled with the reference's dark solid-rock tile pattern,
--   * a walkable cavern carved through it, painted with the reference's pervasive cav* FIELD tiles
--     (rare border/transition tiles like cav4013 and the entrance sand are kept out of the mix),
--   * the floor/rock rim lined with a CONTINUOUS run of "Cave Wall" FACE variants — only the
--     standing faces, never the OBJECT_FLAT rock-fill art the same family carries — each chosen by
--     which neighbouring rim hexes it connects to, LEARNED from the shipped walls, so adjacent
--     pieces meet along a shared edge instead of butting a horizontal against a vertical,
--   * invisible blocking hexes sealing the rock side so the player can't walk off the floor,
--   * a scroll-blocker rectangle ringing the cavern so the camera stays inside,
--   * an exit-grid patch on the floor (worldmap by default) and the player start.
--
--   gecko-cli map generate --script scripts/editor/cave.luau --out cave.map \
--       --arg seed=42 --data <master.dat> [--data <critter.dat>]
--
-- Tunables: --arg chambers=N (default 5), --arg refs=maps/cave1.map,maps/cave2.map,...
-- (the learning corpus, elevation 1), --arg exit=worldmap|none. Batch with --count N.
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
-- Wall pieces: shipped cave walls stand in near-continuous runs along the floor/rock rim. A piece's
-- CONNECTIVITY is fixed by which of its up-to-six neighbouring hexes also carry a wall, so we key on
-- the sorted set of those directions — taken ONLY from walls on the walkable side of the boundary
-- (the side we build the rim on) so every learned piece shares a consistent facing. But within one
-- connection shape the mappers use MANY interchangeable rock-mound variants for texture (a straight
-- run draws on 10-20 different pids), so we keep the whole per-shape distribution and sample it with
-- the seeded rng rather than collapsing to the single modal piece — collapsing reads as a monotonous
-- row of identical posts. Placing by shape still makes adjacent rim walls attach: each connects along
-- the exact edge they share (direction d for one hex is d+3 for its neighbour), and every variant of
-- a shape is one the mappers actually placed in that same context.

local rockPattern = {} -- [parityKey 0..3] = tile id (from the FIRST reference)
local floorWeights = {} -- walkable floor: { {id=..., w=...}, ... } sorted for determinism
local wallPieces = {} -- dir-set key ("d,d,..") -> weighted { {pid=, w=}, ... }, sorted for determinism
local wallPiecesAll = {} -- the same weights pooled across every shape: the fallback palette

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

-- A stable key for a wall piece: the sorted directions (0..5) from its hex to the adjacent hexes
-- that also carry a wall. Sorting makes the key traversal-order-independent — a piece connecting
-- north+south is the same whether the run arrived from the north or the south.
local function dirKey(dirs)
    table.sort(dirs)
    return table.concat(dirs, ",")
end

do
    local weightById = {}
    local connCounts = {} -- dir-set key -> { [pid] = count }
    local pidTotals = {} -- pid -> total count, for the fallback piece
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

        -- Wall FACES on the walkable side: learn each piece's connection shape -> pid. Flat fill
        -- pieces are skipped entirely (they are not part of the boundary run), and the connection
        -- shape is measured against neighbouring FACES only, so a face's key reflects the face chain
        -- it belongs to, not stray fill that happens to sit next to it.
        local function walkableTile(t)
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
        for hex, pid in pairs(faceAt) do
            if walkableTile(api:hexTile(hex)) then
                local dirs = {}
                for _, n in ipairs(api:hexNeighbors(hex)) do
                    if faceAt[n] then
                        table.insert(dirs, api:hexDir(hex, n))
                    end
                end
                local key = dirKey(dirs)
                connCounts[key] = connCounts[key] or {}
                connCounts[key][pid] = (connCounts[key][pid] or 0) + 1
                pidTotals[pid] = (pidTotals[pid] or 0) + 1
            end
        end
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

    -- Turn each shape's { pid -> count } into a weighted list, sorted by pid so the seeded draw is
    -- reproducible (pairs() order is unspecified).
    local function weightedList(counts)
        local list = {}
        for pid, w in pairs(counts) do
            table.insert(list, { pid = pid, w = w })
        end
        table.sort(list, function(a, b) return a.pid < b.pid end)
        return list
    end
    local shapes = 0
    for key, pids in pairs(connCounts) do
        wallPieces[key] = weightedList(pids)
        shapes += 1
    end
    wallPiecesAll = weightedList(pidTotals) -- the fallback palette for shapes the corpus never showed
    assert(#wallPiecesAll > 0, "no walls found on the walkable side of the reference maps")
    print(string.format("Learned %d wall connection shapes and %d cav* floor tiles from %d reference map(s).",
        shapes, #floorWeights, #REFS))
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

-- ============================ carve the cavern ================================
-- Chambers are noise-roughened ellipses connected by 2-tile-wide L corridors; carving only
-- ever ADDS to the walkable set and consecutive chambers share a corridor, so the cavern is
-- connected by construction.
local carved = {} -- [tileIndex] = true
local chambers = {}
local MARGIN = 22

local function carveTile(col, row)
    local t = api:tileIndex(col, row)
    if t >= 0 then
        carved[t] = true
    end
end

local nox, noy = api:rng() * 1000.0, api:rng() * 1000.0
local function clampCoord(v)
    return math.max(MARGIN, math.min(TILE_W - 1 - MARGIN, v))
end

-- Chamber centres random-walk from the middle so they cluster into one cavern system instead
-- of scattering across the map.
local cx, cy = 50, 50
for _ = 1, CHAMBERS do
    local a = api:rngInt(6, 10) -- ellipse half-extents, in tiles
    local b = api:rngInt(6, 10)
    for row = cy - b - 2, cy + b + 2 do
        for col = cx - a - 2, cx + a + 2 do
            local dx, dy = (col - cx) / a, (row - cy) / b
            -- The noise term roughens the rim so chambers read as caverns, not stamped ovals.
            if dx * dx + dy * dy <= 0.72 + 0.55 * api:noise2d(col * 0.23 + nox, row * 0.23 + noy) then
                carveTile(col, row)
            end
        end
    end
    table.insert(chambers, { x = cx, y = cy })
    cx = clampCoord(cx + api:rngInt(-16, 16))
    cy = clampCoord(cy + api:rngInt(-16, 16))
end

-- Corridors: a noise-wobbled 3-wide L between consecutive chambers, so passages meander like
-- eroded rock rather than running as straight trenches.
local function carveSpan(col, row, wobbleAxis)
    local w = math.floor(api:noise2d(col * 0.31 + nox, row * 0.31 + noy) * 3.0 - 1.5 + 0.5)
    for spread = 0, 2 do
        if wobbleAxis == "row" then
            carveTile(col, row + w + spread)
        else
            carveTile(col + w + spread, row)
        end
    end
end
for i = 2, #chambers do
    local a, b = chambers[i - 1], chambers[i]
    for col = math.min(a.x, b.x), math.max(a.x, b.x) do
        carveSpan(col, a.y, "row")
    end
    for row = math.min(a.y, b.y), math.max(a.y, b.y) do
        carveSpan(b.x, row, "col")
    end
end

-- Smoothing: swallow one-tile rock nubs and pockets the corridors leave behind; they generate
-- boundary shapes the shipped maps never contain (and read as debris, not cavern).
for _ = 1, 2 do
    local grow = {}
    for row = MARGIN - 2, TILE_H - MARGIN + 2 do
        for col = MARGIN - 2, TILE_W - MARGIN + 2 do
            local t = api:tileIndex(col, row)
            if t >= 0 and not carved[t] then
                local carvedNeighbors = 0
                for dr = -1, 1 do
                    for dc = -1, 1 do
                        local t2 = api:tileIndex(col + dc, row + dr)
                        if t2 >= 0 and t2 ~= t and carved[t2] then
                            carvedNeighbors += 1
                        end
                    end
                end
                if carvedNeighbors >= 5 then
                    table.insert(grow, t)
                end
            end
        end
    end
    for _, t in ipairs(grow) do
        carved[t] = true
    end
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

-- ============================ wall ring =======================================
-- The wall ring sits on the floor-side contour: every walkable hex that touches rock. Each hex's
-- piece is chosen by the set of directions to its neighbouring contour hexes, so the whole rim is
-- one continuous run of matched pieces (no gaps, no horizontal-butting-a-vertical joins).
local hexTileCache = {}
local function carvedHex(hex)
    local t = hexTileCache[hex]
    if t == nil then
        t = api:hexTile(hex)
        hexTileCache[hex] = t
    end
    return t >= 0 and carved[t] == true
end

-- Floor-side contour: walkable hexes with at least one non-walkable neighbour.
local contour = {}
local contourList = {}
for t in pairs(carved) do
    for _, hex in ipairs(api:tileHexes(t)) do
        if contour[hex] == nil then
            local onRim = false
            for _, n in ipairs(api:hexNeighbors(hex)) do
                if not carvedHex(n) then
                    onRim = true
                    break
                end
            end
            if onRim then
                contour[hex] = true
                table.insert(contourList, hex)
            end
        end
    end
end
table.sort(contourList) -- deterministic placement order

local walls = 0
for _, hex in ipairs(contourList) do
    local dirs = {}
    for _, n in ipairs(api:hexNeighbors(hex)) do
        if contour[n] then
            table.insert(dirs, api:hexDir(hex, n))
        end
    end
    local pid = pickWall(wallPieces[dirKey(dirs)] or wallPiecesAll)
    if api:placeProto(pid, hex, 0) then
        walls += 1
    end
end

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
print(string.format("Lined the rim with %d walls (%d contour hexes) and sealed the rock side with %d blocking hexes.",
    walls, #contourList, blockers))

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
