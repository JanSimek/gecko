-- Cave-interior generator, learned from the shipped cave maps.
--
-- Builds a real Fallout 2 cavern the way maps/cave1..4.map elevation 1 are built:
--   * the whole area filled with the reference's dark solid-rock tile pattern,
--   * a walkable cavern carved through it, painted with the reference's pervasive cav* FIELD tiles
--     (rare border/transition tiles like cav4013 and the entrance sand are kept out of the mix),
--   * the floor/rock boundary lined with a gap-free run of "Cave Wall" faces — a wall on every
--     contour hex, each chosen by the boundary's SMOOTHED outward normal so the isometric view gets
--     standing rock faces on the north/east rim and filled tops on the south/west, without a
--     filled-top piece flickering into the middle of a face run where the 1-hex rim wobbles,
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
local wallPieces = {} -- outward-normal key ("d,d,..") -> weighted { {pid=, w=}, ... }, sorted for determinism
local wallPiecesAll = {} -- the same weights pooled across every orientation: the fallback palette

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
    local normalCounts = {} -- outward-normal key -> { [pid] = count }
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
                local rockDirs = {}
                for _, n in ipairs(api:hexNeighbors(hex)) do
                    if not walkableHex(n) then
                        table.insert(rockDirs, api:hexDir(hex, n))
                    end
                end
                -- Contour hexes only (front some rock, but are not a fully-enclosed pocket).
                if #rockDirs > 0 and #rockDirs < 6 then
                    local key = compassOf(rockDirs)
                    normalCounts[key] = normalCounts[key] or {}
                    normalCounts[key][pid] = (normalCounts[key][pid] or 0) + 1
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
    local orientations = 0
    for key, pids in pairs(normalCounts) do
        wallPieces[key] = weightedList(pids)
        orientations += 1
    end
    wallPiecesAll = weightedList(pidTotals) -- the fallback palette for orientations the corpus never showed
    assert(#wallPiecesAll > 0, "no walls found on the walkable side of the reference maps")
    print(string.format("Learned %d wall classes and %d cav* floor tiles from %d reference map(s).",
        orientations, #floorWeights, #REFS))
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

-- Cellular-automata boundary smoothing: fill a rock tile that is mostly surrounded by floor and
-- erode a floor spike that is mostly surrounded by rock. A few passes round the blocky tile-carve
-- edges — the noise ellipses and L-corridors leave a stair-stepped, nubbly rim — into the smooth
-- curves the shipped caverns have, so the wall run laid on the contour reads as a smooth rock wall
-- instead of a jagged, gap-toothed line. Chamber cores (8 carved neighbours) and 3-wide corridors
-- (an interior corridor tile keeps >= 4) are stable under the rule, so the cavern stays connected.
for _ = 1, 5 do
    local flips = {}
    for row = MARGIN - 2, TILE_H - MARGIN + 2 do
        for col = MARGIN - 2, TILE_W - MARGIN + 2 do
            local t = api:tileIndex(col, row)
            if t >= 0 then
                local c = 0
                for dr = -1, 1 do
                    for dc = -1, 1 do
                        local t2 = api:tileIndex(col + dc, row + dr)
                        if t2 >= 0 and t2 ~= t and carved[t2] then
                            c += 1
                        end
                    end
                end
                if not carved[t] and c >= 5 then
                    flips[t] = true
                elseif carved[t] and c <= 2 then
                    flips[t] = false
                end
            end
        end
    end
    for t, v in pairs(flips) do
        carved[t] = v or nil
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

-- ============================ wall ring (ordered boundary walk) ===============
-- The rim is laid by walking the cavern boundary as an ordered loop and placing a wall on EVERY
-- contour hex, so the run is gap-free by construction. Each hex's piece is chosen by the boundary's
-- SMOOTHED outward normal at that point — the compass direction of the rock over a short window of
-- the walk, not the single hex's own jagged rock-direction set. Smoothing is what stops a lone hex
-- whose local normal wobbles from grabbing a filled-top piece in the middle of a rock-face run (or
-- vice versa): a face run stays faces and a top run stays tops because the class only turns where
-- the boundary itself turns.
local hexTileCache = {}
local function carvedHex(hex)
    local t = hexTileCache[hex]
    if t == nil then
        t = api:hexTile(hex)
        hexTileCache[hex] = t
    end
    return t >= 0 and carved[t] == true
end

-- dir -> neighbour hex, cached; a missing direction is off-grid, which counts as rock (boundary).
local nbrCache = {}
local function dirNeighbor(hex, d)
    local m = nbrCache[hex]
    if m == nil then
        m = {}
        for _, n in ipairs(api:hexNeighbors(hex)) do
            m[api:hexDir(hex, n)] = n
        end
        nbrCache[hex] = m
    end
    return m[d]
end
local function rockAt(hex, d)
    local n = dirNeighbor(hex, d)
    return n == nil or not carvedHex(n)
end
local function rockDirsAt(hex)
    local r = {}
    for d = 0, 5 do
        if rockAt(hex, d) then
            table.insert(r, d)
        end
    end
    return r
end

-- Hex screen position (engine HexagonGrid formula, constant terms dropped), for the boundary tangent.
local function hexScreen(hex)
    local col, row = api:hexCol(hex), api:hexRow(hex)
    local odd = col % 2
    return 16 * row - 24 * col - 8 * odd, 12 * row + 6 * col - 6 * odd
end

-- Compass bin of an arbitrary screen vector (screen y is DOWN, so North is -y).
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

-- The boundary's SMOOTHED outward normal at `seq[i]` as a screen vector: the perpendicular of the
-- tangent across a short window of the walk, oriented to agree with the hex's own rock side. Reading
-- placement off this instead of the single hex's jagged rock-direction set keeps a face run from
-- flickering into a filled-top piece (and back) where the 1-hex rim wobbles. Returns nil, nil for a
-- degenerate window (caller falls back to the per-hex rock directions).
local SMOOTH_WINDOW = 3
local function smoothedNormal(seq, i)
    local n = #seq
    if n < 3 then
        return nil, nil
    end
    local function at(j) return seq[math.max(1, math.min(n, j))] end
    local ax, ay = hexScreen(at(i - SMOOTH_WINDOW))
    local bx, by = hexScreen(at(i + SMOOTH_WINDOW))
    local tx, ty = bx - ax, by - ay
    if tx == 0 and ty == 0 then
        return nil, nil
    end
    local rx, ry = 0, 0
    for _, d in ipairs(rockDirsAt(seq[i])) do
        rx += DIR_VEC[d][1]
        ry += DIR_VEC[d][2]
    end
    local nx, ny = ty, -tx
    if nx * rx + ny * ry < 0 then
        nx, ny = -nx, -ny
    end
    return nx, ny
end

-- Next directed boundary edge, keeping the walkable interior on the left so one walk closes the loop
-- (rotate the outward normal CCW; if that side is still rock the boundary bends around this hex,
-- otherwise step into the walkable neighbour there). Verified to close and cover 100% of a shipped
-- cavern's contour.
local function nextEdge(h, d)
    local nd = (d + 5) % 6
    if rockAt(h, nd) then
        return h, nd
    end
    return dirNeighbor(h, nd), (d + 1) % 6
end

-- One seed boundary edge per contour hex (enough to reach every loop); deterministic order.
local seedByHex = {}
local seeds = {}
for t in pairs(carved) do
    for _, hex in ipairs(api:tileHexes(t)) do
        if seedByHex[hex] == nil then
            local rd = rockDirsAt(hex)
            if #rd > 0 and #rd < 6 then
                seedByHex[hex] = rd[1]
                table.insert(seeds, hex)
            end
        end
    end
end
table.sort(seeds)

local visitedEdge = {}
local function edgeKey(h, d)
    return h * 6 + d
end

local walls = 0
local placedHex = {}
local function placeWall(hex, comp)
    if hex ~= nil and not placedHex[hex] then
        placedHex[hex] = true
        if api:placeProto(pickWall(wallListFor(comp)), hex, 0) then
            walls += 1
        end
    end
end

-- The rock hex one step behind `hex` along the outward normal (nx, ny) — nil if that side is not rock.
-- Backing the contour with a second cell here doubles the run's screen density (the two rows interleave
-- half a hex apart, closing the ~8px gaps a single row of narrow faces leaves) and gives the wall body,
-- the way the shipped caves carry walls on both sides of the boundary.
local function backingHex(hex, nx, ny)
    local best, bestDot = nil, 0.0
    for d = 0, 5 do
        local nb = dirNeighbor(hex, d)
        if nb ~= nil and not carvedHex(nb) then
            local dot = DIR_VEC[d][1] * nx + DIR_VEC[d][2] * ny
            if dot > bestDot then
                best, bestDot = nb, dot
            end
        end
    end
    return best
end

for _, startHex in ipairs(seeds) do
    if not visitedEdge[edgeKey(startHex, seedByHex[startHex])] then
        -- Trace this loop into an ordered, consecutive-deduped hex sequence.
        local seq = {}
        local h, d = startHex, seedByHex[startHex]
        local guard = 0
        repeat
            local k = edgeKey(h, d)
            if visitedEdge[k] then
                break
            end
            visitedEdge[k] = true
            if seq[#seq] ~= h then
                table.insert(seq, h)
            end
            h, d = nextEdge(h, d)
            guard += 1
        until h == nil or not carvedHex(h) or guard > 200000

        -- A wall on every hex of the loop plus a backing cell behind it, both classed by the smoothed
        -- outward normal so the whole band is one texture family that turns only with the boundary.
        for i = 1, #seq do
            local nx, ny = smoothedNormal(seq, i)
            local comp = nx and compassFromVec(nx, ny) or compassOf(rockDirsAt(seq[i]))
            placeWall(seq[i], comp)
            if nx then
                placeWall(backingHex(seq[i], nx, ny), comp)
            end
        end
    end
end
-- Safety net against gaps: any contour hex the walk did not thread onto a loop still gets a wall,
-- classed by its own rock directions. (The walk covers whole loops, but a one-hex spur can be missed.)
for _, hex in ipairs(seeds) do
    placeWall(hex, compassOf(rockDirsAt(hex)))
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
    walls, #seeds, blockers))

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
