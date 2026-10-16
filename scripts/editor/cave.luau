-- cave.luau — a cave interior copied from the shipped game.
--
-- Cave walls only look right as the mappers laid them: the art is a rock panorama cut into strips
-- that must abut pixel-exactly in order, run along the two iso axes, and turn corners with pieces
-- drawn for it. So instead of synthesising a layout, this picks one of the shipped cave interiors
-- (every map elevation lined mostly with "Cave Wall" art on cav* floor, with a way in: cave1..7,
-- the raider and Klamath caves, Broken Hills' mine, ...) and copies it verbatim:
--   * the floor grid,
--   * every wall (faces, rock tops and flat fills) with its art and direction,
--   * the generic scenery: invisible blockers sealing the rock, rocks, stalagmites, debris,
--   * the scroll blockers that bound the camera,
-- leaving out what leads somewhere the copy doesn't have — doors, stairs, ladders, elevators, the
-- original exit grids — and every critter and item. The original exit grids become worldmap exits
-- on the same hexes (where the source has none, the spots its ladders/stairs/elevators stood), and
-- the player starts beside the first of them.
--
--   gecko-cli map generate --script scripts/editor/cave.luau --out cave.map \
--       --arg seed=42 --data <master.dat> [--data <critter.dat>]
--
-- --arg source=maps/cave3.map:1   copy that map elevation instead of a seeded pick
-- --arg list=1                    print the sources found and stop
-- Batch with --count N; generate warns if an exit is unreachable from the player start.

local EMPTY = 1 -- Map::EMPTY_TILE
local CAVE_WALL_FIRST, CAVE_WALL_LAST = 174, 243 -- the "Cave Wall" wall protos (by number)
local MIN_CAVE_WALLS = 150 -- a cave interior, not a building with a rock outcrop
local MIN_CAV_FLOOR = 0.95 -- share of the non-empty floor drawn with cav* tiles
local MAX_OTHER_WALLS = 0.3 -- share of standing walls that aren't cave walls: more is a building (a vault)
local SCROLL_BLOCKER = api:proto("misc", 12) -- "Scroll Blocker"
local function isExitGrid(pid)
    local n = pid % 0x1000000 -- misc protos 16..23 are the exit grids (MiscProtoTypeId)
    return pid // 0x1000000 == 5 and n >= 16 and n <= 23
end

print("Seed " .. args.seed .. " — re-run with --arg seed=" .. args.seed .. " to reproduce this cave.")

-- ============================ find the shipped cave interiors =====================
local cavTiles = {}
for _, id in pairs(api:tilesByPrefix("cav")) do
    cavTiles[id] = true
end

-- Cave walls, and the other standing (non-flat) walls: a vault's metal, a raider camp's fence.
local flatCache = {}
local function wallCounts(walls)
    local cave, other = 0, 0
    for i = 1, #walls, 3 do
        local pid = walls[i]
        local number = pid % 0x1000000
        if number >= CAVE_WALL_FIRST and number <= CAVE_WALL_LAST then
            cave += 1
        else
            if flatCache[pid] == nil then
                local ok, flat = pcall(function()
                    return api:protoFlat(pid)
                end)
                flatCache[pid] = ok and flat
            end
            if not flatCache[pid] then
                other += 1
            end
        end
    end
    return cave, other
end

local function cavFloorShare(floor)
    local used, cav = 0, 0
    for t = 1, #floor do
        if floor[t] ~= EMPTY then
            used += 1
            if cavTiles[floor[t]] then
                cav += 1
            end
        end
    end
    return cav / math.max(used, 1)
end

-- Somewhere the player comes in: an exit grid, or a ladder/stairs/elevator. Elevations with
-- neither are set dressing nobody visits (unsealed, no way out).
local sceneryTypes = {}
local function sceneryType(pid)
    if sceneryTypes[pid] == nil then
        local ok, kind = pcall(function()
            return api:protoSceneryType(pid)
        end)
        sceneryTypes[pid] = ok and kind or "unloadable"
    end
    return sceneryTypes[pid]
end
local function isWayOut(kind)
    return kind == "Stairs" or kind == "Elevator" or kind == "LadderUp" or kind == "LadderDown"
end
local function hasWayIn(path, elev)
    local misc = api:mapObjectsAt(path, elev, "misc")
    for i = 1, #misc, 3 do
        if isExitGrid(misc[i]) then
            return true
        end
    end
    local scenery = api:mapObjectsAt(path, elev, "scenery")
    for i = 1, #scenery, 3 do
        if isWayOut(sceneryType(scenery[i])) then
            return true
        end
    end
    return false
end

local sources = {}
local seen = {} -- layout signature -> true: some maps ship the same cave twice (cave2/coast5)
local mapPaths = api:listMaps()
table.sort(mapPaths)
for _, path in ipairs(mapPaths) do
    for elev = 0, 2 do
        local ok, walls = pcall(function()
            return api:mapObjectsAt(path, elev, "wall")
        end)
        local cave, other = 0, 0
        if ok then
            cave, other = wallCounts(walls)
        end
        if cave >= MIN_CAVE_WALLS and other <= MAX_OTHER_WALLS * (cave + other) then
            local floor = api:mapFloorAt(path, elev)
            if cavFloorShare(floor) >= MIN_CAV_FLOOR and hasWayIn(path, elev) then
                local hexSum = 0
                for i = 2, #walls, 3 do
                    hexSum += walls[i]
                end
                local signature = string.format("%d:%d", #walls, hexSum)
                if not seen[signature] then
                    seen[signature] = true
                    table.insert(sources, { path = path, elev = elev })
                end
            end
        end
    end
end
assert(#sources > 0, "no shipped cave interiors found — is master.dat mounted?")

if args.list then
    for _, s in ipairs(sources) do
        print(string.format("%s:%d", s.path, s.elev))
    end
    return
end

local source
if args.source then
    local path, elev = string.match(args.source, "^(.+):(%d)$")
    assert(path, "--arg source takes <map>:<elevation>, e.g. maps/cave3.map:1")
    source = { path = path, elev = tonumber(elev) }
else
    source = sources[api:rngInt(1, #sources)]
end
print(string.format("Copying the cave interior of %s elevation %d (one of %d shipped caves).", source.path, source.elev, #sources))

-- ============================ copy it ==============================================
local floor = api:mapFloorAt(source.path, source.elev)
for t = 0, #floor - 1 do
    if floor[t + 1] ~= EMPTY then
        api:paintFloor(t, floor[t + 1])
    end
end

local walls = api:copyObjects(source.path, source.elev, "wall", {})

-- Scenery: everything generic. Doors, stairs, ladders and elevators lead to maps and elevations the
-- copy doesn't have; their spots are where the way out was.
local exitSpots = {}
local skipScenery, leftOut = {}, 0
local scenery = api:mapObjectsAt(source.path, source.elev, "scenery")
for i = 1, #scenery, 3 do
    local pid, hex = scenery[i], scenery[i + 1]
    local kind = sceneryType(pid)
    if kind ~= "Generic" then
        skipScenery[pid] = true
        leftOut += 1
        if isWayOut(kind) then
            table.insert(exitSpots, hex)
        end
    end
end
local skipList = {}
for pid in pairs(skipScenery) do
    table.insert(skipList, pid)
end
table.sort(skipList)
local copiedScenery = api:copyObjects(source.path, source.elev, "scenery", skipList)

-- Misc: only the scroll blockers. The exit grids are rebuilt below as worldmap exits.
local misc = api:mapObjectsAt(source.path, source.elev, "misc")
local skipMisc, exitGrids = {}, {}
for i = 1, #misc, 3 do
    local pid, hex = misc[i], misc[i + 1]
    if isExitGrid(pid) then
        table.insert(exitGrids, hex)
    end
    if pid ~= SCROLL_BLOCKER then
        skipMisc[pid] = true
    end
end
local skipMiscList = {}
for pid in pairs(skipMisc) do
    table.insert(skipMiscList, pid)
end
table.sort(skipMiscList)
local scrollBlockers = api:copyObjects(source.path, source.elev, "misc", skipMiscList)
print(string.format("Copied %d walls, %d scenery pieces and %d scroll blockers; left out %d doors/ladders/stairs/elevators.",
    walls, copiedScenery, scrollBlockers, leftOut))

-- ============================ exits + player start =================================
-- What blocks movement in the copy, by the engine's own rule (each object's instance flags).
local blocked = {}
for _, hex in ipairs(api:blockedHexes()) do
    blocked[hex] = true
end

-- Floor, as opposed to the solid rock around the cave: the reference's rock is its most common tile
-- in each (row%2, col%2) cell of the grid, everything else is floor.
local parityCounts = { {}, {}, {}, {} }
for t = 0, #floor - 1 do
    local id = floor[t + 1]
    if id ~= EMPTY then
        local cell = parityCounts[(t // 100) % 2 * 2 + t % 2 + 1]
        cell[id] = (cell[id] or 0) + 1
    end
end
local rock = {}
for _, cell in ipairs(parityCounts) do
    local bestId, bestCount = EMPTY, 0
    for id, count in pairs(cell) do
        if count > bestCount or (count == bestCount and id < bestId) then
            bestId, bestCount = id, count
        end
    end
    rock[bestId] = true
end
local function floorHex(hex)
    local t = api:hexTile(hex)
    return t >= 0 and floor[t + 1] ~= EMPTY and not rock[floor[t + 1]] and not blocked[hex]
end

-- Every way out of the source becomes a worldmap exit: its exit grids, and the spots its ladders,
-- stairs and elevators stood (on an open floor neighbour where the spot itself is blocked). Exit
-- grids the shipped maps put under a blocker are dropped: the player can't step there, and the rest
-- of the line is still there.
local candidates, isCandidate = {}, {}
local function addCandidate(hex)
    if hex ~= nil and not blocked[hex] and not isCandidate[hex] then
        isCandidate[hex] = true
        table.insert(candidates, hex)
    end
end
for _, hex in ipairs(exitGrids) do
    addCandidate(hex)
end
for _, hex in ipairs(exitSpots) do
    local spot = not blocked[hex] and hex or nil
    for _, n in ipairs(api:hexNeighbors(hex)) do
        if spot == nil and floorHex(n) then
            spot = n -- a ladder stands against the rock: its way out is on the floor side
        end
    end
    addCandidate(spot)
end
local function openFloor(hex)
    return floorHex(hex) and not isCandidate[hex]
end

-- The player starts inside the cave, near an exit. An exit in a cave's mouth has open hexes on both
-- sides, the cavern on one and the ground beyond the entrance on the other (the shipped caves don't
-- seal that), so start in the biggest stretch of open floor next to an exit.
local region, regionSize = {}, {}
local function flood(seed)
    local id = #regionSize + 1
    local stack, size = { seed }, 0
    region[seed] = id
    while #stack > 0 do
        local hex = table.remove(stack)
        size += 1
        for _, n in ipairs(api:hexNeighbors(hex)) do
            if region[n] == nil and openFloor(n) then
                region[n] = id
                table.insert(stack, n)
            end
        end
    end
    regionSize[id] = size
end

local NEAR_EXIT = 3 -- steps from an exit, over any open hex, to look for floor
local start = nil
for _, exitHex in ipairs(candidates) do
    local seen, frontier = { [exitHex] = true }, { exitHex }
    for _ = 1, NEAR_EXIT do
        local nextFrontier = {}
        for _, hex in ipairs(frontier) do
            for _, n in ipairs(api:hexNeighbors(hex)) do
                if not seen[n] and not blocked[n] and not isCandidate[n] then
                    seen[n] = true
                    table.insert(nextFrontier, n)
                end
            end
        end
        table.sort(nextFrontier)
        for _, hex in ipairs(nextFrontier) do
            if openFloor(hex) then
                if region[hex] == nil then
                    flood(hex)
                end
                if start == nil or regionSize[region[hex]] > regionSize[region[start]] then
                    start = hex
                end
            end
        end
        frontier = nextFrontier
    end
end
assert(start ~= nil, "no open floor near the exits of " .. source.path)
api:setPlayerStart(start, 0, 0)

-- Exits only where the player can walk to them: a source split into areas joined through other
-- elevations (a mine's levels) leaves some ways out cut off from the start.
local reach, stack = { [start] = true }, { start }
while #stack > 0 do
    local hex = table.remove(stack)
    for _, n in ipairs(api:hexNeighbors(hex)) do
        if not reach[n] and not blocked[n] then
            reach[n] = true
            table.insert(stack, n)
        end
    end
end
local exits = 0
for _, hex in ipairs(candidates) do
    if reach[hex] and api:placeExitGrid(hex, -2, 0, 0, 0) then
        exits += 1
    end
end
print(string.format("%d worldmap exit grids (%d cut off from the start left out); player start at hex %d.",
    exits, #candidates - exits, start))
