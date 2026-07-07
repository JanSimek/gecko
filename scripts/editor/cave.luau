-- Cave-interior generator, learned from the shipped cave maps.
--
-- Builds a real Fallout 2 cavern the way maps/cave1..4.map elevation 1 are built:
--   * the whole area filled with the reference's dark solid-rock tile pattern,
--   * a walkable cavern carved through it, painted with the reference's weighted floor mix,
--   * every rock/floor boundary lined with the correctly-oriented "Cave Wall" proto variant —
--     LEARNED from the reference maps (boundary-shape mask -> wall pid; the orientation is in
--     the pid, shipped walls all use direction 0),
--   * invisible blocking hexes sealing boundary gaps the wall art doesn't cover,
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
-- Engine-verified blocker protos (pro_scenery.msg / pro_misc.msg names):
local SECRET_BLOCKER = api:proto("scenery", 67) -- "Secret Blocking Hex"
local SCROLL_BLOCKER = api:proto("misc", 12) -- "Scroll Blocker"

print("Seed " .. args.seed .. " — re-run with --arg seed=" .. args.seed .. " to reproduce this cave.")

-- ============================ learn from the references =======================
-- Rock pattern: the caves fill unwalkable ground with a small repeating tile pattern
-- (cave1: cav4009/cav4010 on a 2x2 lattice). Recover it as the most common tile per
-- (row%2, col%2) parity cell; everything non-empty outside that pattern is walkable floor.
-- The tile-level boundary-shape key: which of a tile's 8 neighbours are walkable, whether the
-- tile itself is, and its grid parity (the hex layout inside a tile depends on it). The learned
-- value is the mappers' whole wall ARRANGEMENT across that tile's hexes — placing arrangements
-- per tile is what reproduces contiguous wall runs; independent per-hex picks read as rubble.
local NEIGHBORHOOD = {
    { -1, -1 }, { 0, -1 }, { 1, -1 },
    { -1, 0 }, { 1, 0 },
    { -1, 1 }, { 0, 1 }, { 1, 1 },
}

local rockPattern = {} -- [parityKey 0..3] = tile id (from the FIRST reference)
local floorWeights = {} -- walkable floor: { {id=..., w=...}, ... } sorted for determinism
local wallArrangement = {} -- shape key -> { [hexSlot] = wall pid } (possibly empty = "no walls here")

-- A stable identity for each hex within its tile: the offset from the tile's naive 2x2 hex
-- anchor (offsets stay in [-1..2] around it). Indexing arrangements by tileHexes position
-- would NOT be stable — tiles carry 3..6 hexes, so a list index means different geometry on
-- different tiles and the learning votes smear.
local function hexSlot(hex, tcol, trow)
    local dx = hex % 200 - 2 * tcol
    local dy = hex // 200 - 2 * trow
    return (dx + 1) * 4 + (dy + 1)
end
do
    -- Each cave map uses its OWN rock tile pair (cave1 fills with cav4009/cav4010, others with
    -- different ids), so rock/walkable must be classified per reference — a shared set would
    -- count one map's rock as another map's floor and poison both the palette and the masks.
    local weightById = {}
    local tally = {} -- key -> { [encoded arrangement] = count }
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

        -- Weighted walkable-floor mix: every non-empty, non-rock tile the PRIMARY reference
        -- uses. The palette and the rock pattern must come from the same map or the cavern
        -- floor clashes with the rock it is carved from; the other references only add wall
        -- shapes.
        if refIndex == 1 then
            for t = 1, TILE_W * TILE_H do
                local id = floor[t]
                if id ~= EMPTY and not isRock[id] then
                    weightById[id] = (weightById[id] or 0) + 1
                end
            end
        end

        -- Wall arrangements: for every boundary tile (walkable or rock with at least one
        -- walkable 8-neighbour), key its shape and record the exact wall pids the mappers put
        -- on that tile's hexes — including "none", so shapes that usually carry no walls learn
        -- to stay empty.
        local function walkableTile(t)
            if t < 0 then
                return false
            end
            local id = floor[t + 1]
            return id ~= EMPTY and not isRock[id]
        end

        local wallPidAt = {}
        local walls = api:mapObjectsAt(ref, REF_ELEVATION, "wall")
        for i = 1, #walls, 3 do
            wallPidAt[walls[i + 1]] = walls[i]
        end

        for t = 0, TILE_W * TILE_H - 1 do
            if floor[t + 1] ~= EMPTY then
                local col, row = t % TILE_W, t // TILE_W
                local selfWalkable = walkableTile(t)
                local mask, walkableNeighbors = 0, 0
                for n, offset in ipairs(NEIGHBORHOOD) do
                    if walkableTile(api:tileIndex(col + offset[1], row + offset[2])) then
                        mask += 2 ^ n
                        walkableNeighbors += 1
                    end
                end
                local boundary = selfWalkable and walkableNeighbors < 8 or (not selfWalkable and walkableNeighbors > 0)
                if boundary then
                    local key = string.format("%d:%d:%d:%d", selfWalkable and 1 or 0, mask, col % 2, row % 2)
                    local entry = tally[key]
                    if not entry then
                        entry = { total = 0, slots = {} }
                        tally[key] = entry
                    end
                    entry.total += 1
                    for _, hex in ipairs(api:tileHexes(t)) do
                        local pid = wallPidAt[hex]
                        if pid then
                            local slot = hexSlot(hex, col, row)
                            entry.slots[slot] = entry.slots[slot] or {}
                            entry.slots[slot][pid] = (entry.slots[slot][pid] or 0) + 1
                        end
                    end
                end
            end
        end
    end

    for id, w in pairs(weightById) do
        table.insert(floorWeights, { id = id, w = w })
    end
    table.sort(floorWeights, function(a, b) return a.id < b.id end) -- pairs() order is unspecified
    assert(#floorWeights > 0, "no walkable floor tiles found in the reference maps")

    -- Per-slot statistics: how often the mappers put a wall on this slot of this shape, and
    -- which variant (the modal pid). Generation replays the probabilities with the seeded rng,
    -- so wall density and texture match the references by construction — the mappers' own
    -- placement was part deliberate, part taste, and a hard threshold reproduces neither.
    local shapes, walled = 0, 0
    for key, entry in pairs(tally) do
        local arrangement = {}
        local any = false
        for slot, pids in pairs(entry.slots) do
            local bestPid, bestCount, slotCount = nil, 0, 0
            for pid, count in pairs(pids) do
                slotCount += count
                if count > bestCount or (count == bestCount and pid < bestPid) then
                    bestPid, bestCount = pid, count
                end
            end
            arrangement[slot] = { pid = bestPid, p = slotCount / entry.total }
            any = true
        end
        wallArrangement[key] = arrangement
        shapes += 1
        if any then
            walled += 1
        end
    end
    print(string.format("Learned %d boundary shapes (%d carrying walls) and %d floor tiles from %d reference map(s).",
        shapes, walled, #floorWeights, #REFS))
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

-- ============================ walls + blockers ================================
-- Walk every boundary tile of the carved cavern, key its shape exactly like the learning pass,
-- and replay the mappers' wall arrangement onto the same hex slots. A rock-side boundary tile
-- whose shape was never seen gets invisible blocking hexes instead, like the shipped maps'
-- Secret Blocking Hex seals — sealed is safe, bare is a hole in the cave wall.
local boundaryTiles = {} -- ascending, for deterministic placement
do
    local seen = {}
    for t in pairs(carved) do
        local col, row = t % TILE_W, t // TILE_W
        for row2 = row - 1, row + 1 do
            for col2 = col - 1, col + 1 do
                local t2 = api:tileIndex(col2, row2)
                if t2 >= 0 and not seen[t2] then
                    seen[t2] = true
                    table.insert(boundaryTiles, t2)
                end
            end
        end
    end
    table.sort(boundaryTiles)
end

local pidBlocksCache = {}
local function wallBlocks(pid)
    if pidBlocksCache[pid] == nil then
        pidBlocksCache[pid] = api:protoBlocks(pid)
    end
    return pidBlocksCache[pid]
end

local walls, unmatched = 0, 0
local blockedHex = {} -- hexes already sealed by a movement-blocking wall
for _, t in ipairs(boundaryTiles) do
    local col, row = t % TILE_W, t // TILE_W
    local selfWalkable = carved[t] == true
    local mask, walkableNeighbors = 0, 0
    for n, offset in ipairs(NEIGHBORHOOD) do
        local t2 = api:tileIndex(col + offset[1], row + offset[2])
        if t2 >= 0 and carved[t2] then
            mask += 2 ^ n
            walkableNeighbors += 1
        end
    end
    local boundary = selfWalkable and walkableNeighbors < 8 or (not selfWalkable and walkableNeighbors > 0)
    if boundary then
        local key = string.format("%d:%d:%d:%d", selfWalkable and 1 or 0, mask, col % 2, row % 2)
        local arrangement = wallArrangement[key]
        if arrangement then
            for _, hex in ipairs(api:tileHexes(t)) do
                local slot = arrangement[hexSlot(hex, col, row)]
                if slot and api:rng() < slot.p and api:placeProto(slot.pid, hex, 0) then
                    walls += 1
                    if wallBlocks(slot.pid) then
                        blockedHex[hex] = true
                    end
                end
            end
        elseif not selfWalkable then
            unmatched += 1
        end
    end
end

-- Seal the cavern: every rock hex bordering a walkable hex gets an invisible blocker unless a
-- blocking wall already stands there. The wall art alone leaves walk-through gaps (the shipped
-- caves carry hundreds of Secret Blocking Hexes for exactly this reason) — without the seal the
-- player strolls off the floor into the solid rock.
local hexTileCache = {}
local function carvedHex(hex)
    local t = hexTileCache[hex]
    if t == nil then
        t = api:hexTile(hex)
        hexTileCache[hex] = t
    end
    return t >= 0 and carved[t] == true
end
local blockers = 0
for _, t in ipairs(boundaryTiles) do
    if not carved[t] then
        for _, hex in ipairs(api:tileHexes(t)) do
            if not blockedHex[hex] then
                local touchesFloor = false
                for _, n in ipairs(api:hexNeighbors(hex)) do
                    if carvedHex(n) then
                        touchesFloor = true
                        break
                    end
                end
                if touchesFloor and api:placeProto(SECRET_BLOCKER, hex, 0) then
                    blockedHex[hex] = true
                    blockers += 1
                end
            end
        end
    end
end
print(string.format("Placed %d walls (%d boundary shapes unseen) and sealed the rim with %d blocking hexes.",
    walls, unmatched, blockers))

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
