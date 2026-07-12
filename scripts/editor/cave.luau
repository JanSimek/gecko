-- Cave-interior generator, learned from the shipped cave maps.
--
-- Builds a real Fallout 2 cavern the way maps/cave1..4.map elevation 1 are built:
--   * the whole area filled with the reference's dark solid-rock tile pattern,
--   * a walkable cavern carved through it, painted with the reference's pervasive cav* FIELD tiles
--     (rare border/transition tiles like cav4013 and the entrance sand are kept out of the mix),
--   * the floor/rock boundary lined with "Cave Wall" pieces chosen by LOCAL BOUNDARY GEOMETRY:
--     each walkable-side contour hex is keyed by the directions to its neighbouring rim hexes plus
--     the mask of which neighbours are rock, and the piece is sampled from what the shipped rims put
--     at that same geometry. The absolute directions pin the isometric orientation, so faces land on
--     the up-screen rim and filled lips on the near edge without hand tuning (~79% orientation match
--     on held-out cave4 vs 53% for the old compass bin),
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
local geomModel = {} -- "faceDirs|rockmask" -> weighted { {pid=, w=}, ... }: the primary key
local geomDirpair = {} -- "faceDirs"          -> weighted { ... }: coarser fallback when the exact key is unseen
local wallPieces = {} -- compass class       -> weighted { ... }: final fallback (outward-normal bin)
local wallPiecesAll = {} -- all pieces pooled: last resort

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
    local geomCounts = {} -- "faceDirs|rockmask" -> { [pid] = count }: primary geometry key
    local dirpairCounts = {} -- "faceDirs" -> { [pid] = count }: coarser fallback key
    local normalCounts = {} -- compass class -> { [pid] = count }: final-fallback bin
    local pidTotals = {} -- pid -> total count, for the last-resort pool
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
        for hex, pid in pairs(faceAt) do
            if walkableHex(hex) then
                -- Local boundary geometry at this face: the directions to its neighbouring faces (the
                -- rim tangent / turn) and a 6-bit mask of which neighbours are rock (which side the rock
                -- lies on, hence the isometric orientation). Both are absolute, so they pin the edge.
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
                    local geom = dirpair .. "|" .. rockmask
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
    foldModel(geomCounts, geomModel)
    foldModel(dirpairCounts, geomDirpair)
    foldModel(normalCounts, wallPieces)
    wallPiecesAll = weightedList(pidTotals) -- last-resort pool
    assert(#wallPiecesAll > 0, "no walls found on the walkable side of the reference maps")

    local geomKeys = 0
    for _ in pairs(geomModel) do
        geomKeys += 1
    end
    print(string.format("Learned %d wall geometries and %d cav* floor tiles from %d reference map(s).",
        geomKeys, #floorWeights, #REFS))
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

-- ============================ carve the cavern (metaball field) ===============
-- The cavern is a smooth SCALAR FIELD, not a tile stamp. Each chamber is a radial source and each
-- corridor a line source; where the summed field clears 1 is the walkable interior and its edge
-- (field == 1) is a smooth organic curve — no ovals, no stair-steps. A low-frequency noise term warps
-- the field so the edge erodes like rock. The floor is the field sampled per tile; the wall run later
-- samples the SAME field at HEX resolution, so the walls trace the true curve instead of the blocky
-- tile edge. Distances are SCREEN distances (the iso projection) so chambers read round on screen, not
-- squished. Hex coords are 2x the tile grid; the field lives in hex space.
local carved = {} -- [tileIndex] = true
local chambers = {}
local MARGIN = 22

local function clampCoord(v)
    return math.max(MARGIN, math.min(TILE_W - 1 - MARGIN, v))
end

-- Screen position (engine hex->pixel linear part) of a hex-space point.
local function hexS(c, r)
    return 16 * r - 24 * c, 12 * r + 6 * c
end

-- Chamber centres random-walk from the middle so they cluster into one cavern system. Each becomes a
-- point source (a zero-length segment); consecutive chambers are joined by a corridor line source so
-- the blobs merge into one connected cavern.
local cx, cy = 50, 50
local sources = {} -- {ac, ar, bc, br, num}; num = screen-radius^2, field term = num / screenDist^2
for _ = 1, CHAMBERS do
    table.insert(chambers, { x = cx, y = cy })
    local rad = api:rngInt(230, 330)
    table.insert(sources, { ac = 2 * cx, ar = 2 * cy, bc = 2 * cx, br = 2 * cy, num = rad * rad })
    cx = clampCoord(cx + api:rngInt(-14, 14))
    cy = clampCoord(cy + api:rngInt(-14, 14))
end
for i = 2, #chambers do
    local a, b = chambers[i - 1], chambers[i]
    table.insert(sources, { ac = 2 * a.x, ar = 2 * a.y, bc = 2 * b.x, br = 2 * b.y, num = 150 * 150 })
end

-- Squared screen distance from a hex-space point to a hex-space segment (a point source is a=b).
local function segDist2(pc, pr, s)
    local px, py = hexS(pc, pr)
    local ax, ay = hexS(s.ac, s.ar)
    local bx, by = hexS(s.bc, s.br)
    local dx, dy = bx - ax, by - ay
    local len2 = dx * dx + dy * dy
    local tt = 0.0
    if len2 > 0.0 then
        tt = ((px - ax) * dx + (py - ay) * dy) / len2
        tt = math.max(0.0, math.min(1.0, tt))
    end
    local ex, ey = px - (ax + tt * dx), py - (ay + tt * dy)
    return ex * ex + ey * ey
end

-- The field at a hex-space point, warped by low-frequency noise so the isocontour erodes organically.
local fnx, fny = api:rng() * 1000.0, api:rng() * 1000.0
local ROUGH, NF = 0.28, 0.05
local function field(c, r)
    local f = 0.0
    for _, s in ipairs(sources) do
        local d2 = segDist2(c, r, s)
        f += s.num / (d2 > 1.0 and d2 or 1.0)
    end
    return f * (1.0 + ROUGH * (api:noise2d(c * NF + fnx, r * NF + fny) - 0.5) * 2.0)
end

-- Floor: a tile is walkable where the field clears 1 at the tile's centre (hex coords 2x the tile
-- grid). MARGIN keeps the cavern off the map edge.
for row = MARGIN, TILE_H - 1 - MARGIN do
    for col = MARGIN, TILE_W - 1 - MARGIN do
        if field(2 * col + 1, 2 * row + 1) >= 1.0 then
            local t = api:tileIndex(col, row)
            if t >= 0 then
                carved[t] = true
            end
        end
    end
end

-- Keep only the floor connected to the first chamber, so a noise-spawned outlier blob cannot leave a
-- detached pocket (or strand an exit in unreachable rock).
do
    local startT = api:tileIndex(chambers[1].x, chambers[1].y)
    if carved[startT] then
        local keep = { [startT] = true }
        local stack = { startT }
        while #stack > 0 do
            local t = table.remove(stack)
            local col, row = t % TILE_W, t // TILE_W
            for _, dxy in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
                local nt = api:tileIndex(col + dxy[1], row + dxy[2])
                if nt >= 0 and carved[nt] and not keep[nt] then
                    keep[nt] = true
                    table.insert(stack, nt)
                end
            end
        end
        carved = keep
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

-- ============================ wall band (field isocontour) ====================
-- The rim is every hex that STRADDLES the field's isocontour — a hex whose inside/outside flips
-- against any neighbour — so the band is a smooth ~2-cell ribbon tracing the TRUE curve at hex
-- resolution: dense, gap-free, and unquantised by the tile grid. Each piece is classed by the field
-- GRADIENT there: the outward normal is -grad(field) (pointing down the field, into the rock), mapped
-- through the hex->pixel Jacobian to a screen vector and binned to a compass class. Because the field
-- is smooth the class turns only as the curve turns — no per-hex flicker, no stair-steps.
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

-- Inside-ness straight from the smooth field (not the tile grid), cached per hex.
local insideCache = {}
local function inside(hex)
    local v = insideCache[hex]
    if v == nil then
        v = field(api:hexCol(hex), api:hexRow(hex)) >= 1.0
        insideCache[hex] = v
    end
    return v
end

-- Outward screen normal at a hex = -grad(field) mapped through the hex->pixel Jacobian (columns
-- d/dcol = (-24, 6), d/drow = (16, 12)). Central differences a couple of hexes wide ride over the
-- noise wiggle so the class follows the chamber curve, not local grain.
local GH = 2.0
local function outwardNormal(hex)
    local c, r = api:hexCol(hex), api:hexRow(hex)
    local fc = field(c + GH, r) - field(c - GH, r)
    local fr = field(c, r + GH) - field(c, r - GH)
    return 24 * fc - 16 * fr, -6 * fc - 12 * fr
end

-- Candidate hexes: everything on the carved tiles and their immediate rock neighbours — the strip the
-- isocontour runs through — collected into a sorted list so placement order (hence the saved map) is
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

-- The rim: the walkable-side contour — an INSIDE (floor) hex with at least one rock neighbour,
-- exactly the hexes the shipped maps line with a wall face. A set + a deterministically sorted list.
local rimSet = {}
local rim = {}
for _, hex in ipairs(candidates) do
    if inside(hex) then
        for _, n in ipairs(api:hexNeighbors(hex)) do
            if not inside(n) then
                rimSet[hex] = true
                table.insert(rim, hex)
                break
            end
        end
    end
end

-- The local boundary geometry key at a generated rim hex, computed the SAME way the shipped rims
-- were learned: the sorted directions to neighbouring rim hexes (tangent/turn) plus the mask of the
-- rock neighbours (which side, hence orientation). Returns the exact key and the dirpair fallback.
local function geomKeyAt(hex)
    local rockmask, faceDirs = 0, {}
    for _, n in ipairs(api:hexNeighbors(hex)) do
        local d = api:hexDir(hex, n)
        if not inside(n) then
            rockmask += 2 ^ d
        end
        if rimSet[n] then
            table.insert(faceDirs, d)
        end
    end
    table.sort(faceDirs)
    local dirpair = table.concat(faceDirs, ",")
    return dirpair .. "|" .. rockmask, dirpair
end

-- Place a wall on every rim hex, choosing the piece by local geometry: the exact key, then the
-- coarser dirpair, then the outward-normal compass bin, then the global pool. Sampling the learned
-- distribution keeps the mappers' texture variety while the geometry key keeps the orientation
-- right. The choice is a function of the hex's own geometry alone, so a single sorted pass is
-- deterministic under the seeded draw (no ordering or neighbour dependence).
local walls = 0
local boundaryHexes = #rim
for _, hex in ipairs(rim) do
    local geom, dirpair = geomKeyAt(hex)
    local list = geomModel[geom] or geomDirpair[dirpair] or wallListFor(compassFromVec(outwardNormal(hex)))
    if api:placeProto(pickWall(list), hex, 0) then
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
