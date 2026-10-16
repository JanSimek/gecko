-- cave.luau — a cave interior copied from the shipped game.
--
-- Cave walls only look right as the mappers laid them: the art is a rock panorama cut into strips
-- that must abut pixel-exactly in order, run along the two iso axes, and turn corners with pieces
-- drawn for it. So instead of synthesising a layout, this picks one of the shipped cave interiors
-- (every map elevation lined mostly with "Cave Wall" art on cav* floor, entered through an exit
-- grid: cave1..7, the raider and Klamath caves, ...) and copies it verbatim:
--   * the floor grid,
--   * every wall (faces, rock tops and flat fills) with its art and direction,
--   * the generic scenery: invisible blockers sealing the rock, rocks, stalagmites, debris,
--   * the scroll blockers that bound the camera,
-- all in the source's own order, leaving out what leads somewhere the copy doesn't have — doors,
-- stairs, ladders, elevators — and every critter and item. The source's exit grids lead to the world
-- map instead, keeping their shape; only those the player can walk to are kept, and the player
-- starts inside the cave beside one.
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

-- A way in: an exit line, where the mappers put the cave's mouth. An elevation reached only by
-- ladder, stairs or elevator would need an exit standing in the middle of a tunnel, and one with
-- no way in at all is set dressing nobody visits.
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
-- The exit lines of a map elevation: its exit-grid hexes, grouped where they touch (within two
-- hexes), keeping the groups of MIN_EXIT_LINE or more. A lone marker or a pair is a scripted spot
-- or a stray, not a cave mouth.
local MIN_EXIT_LINE = 3
local function exitLines(path, elev)
    local misc = api:mapObjectsAt(path, elev, "misc")
    local isExit, all = {}, {}
    for i = 1, #misc, 3 do
        if isExitGrid(misc[i]) and not isExit[misc[i + 1]] then
            isExit[misc[i + 1]] = true
            table.insert(all, misc[i + 1])
        end
    end
    table.sort(all)
    local seen, kept = {}, {}
    for _, first in ipairs(all) do
        if not seen[first] then
            local group, stack = {}, { first }
            seen[first] = true
            while #stack > 0 do
                local hex = table.remove(stack)
                table.insert(group, hex)
                for _, n in ipairs(api:hexNeighbors(hex)) do
                    for _, m in ipairs({ n, table.unpack(api:hexNeighbors(n)) }) do
                        if isExit[m] and not seen[m] then
                            seen[m] = true
                            table.insert(stack, m)
                        end
                    end
                end
            end
            if #group >= MIN_EXIT_LINE then
                for _, hex in ipairs(group) do
                    table.insert(kept, hex)
                end
            end
        end
    end
    table.sort(kept)
    return kept
end
local function hasWayIn(path, elev)
    return #exitLines(path, elev) > 0
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

-- Objects, in one pass so they keep the source's order (objects sharing a hex draw in list order):
-- * walls, all of them;
-- * scenery, everything generic. Doors, stairs, ladders and elevators lead to maps and elevations
--   the copy doesn't have;
-- * misc, only the scroll blockers. The exit grids are copied below, retargeted to the world map.
local skip, leftOut = {}, 0
local scenery = api:mapObjectsAt(source.path, source.elev, "scenery")
for i = 1, #scenery, 3 do
    local pid = scenery[i]
    if sceneryType(pid) ~= "Generic" then
        skip[pid] = true
        leftOut += 1
    end
end
local misc = api:mapObjectsAt(source.path, source.elev, "misc")
local exitGrids = exitLines(source.path, source.elev)
for i = 1, #misc, 3 do
    local pid = misc[i]
    if pid ~= SCROLL_BLOCKER then
        skip[pid] = true
    end
end
local skipList = {}
for pid in pairs(skip) do
    table.insert(skipList, pid)
end
table.sort(skipList)
local copied = api:copyObjects(source.path, source.elev, { "wall", "scenery", "misc" }, skipList)
print(string.format("Copied %d walls, scenery pieces and scroll blockers; left out %d doors/ladders/stairs/elevators.",
    copied, leftOut))

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

-- The source's exit grids lead to the world map instead. Ones the shipped maps put under a blocker
-- are dropped: the player can't step there, and the rest of the line is still there.
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
-- The exit grids keep their own markers (each one's direction and offset), so the exit line keeps
-- the mappers' shape.
local reachable = {}
for _, hex in ipairs(candidates) do
    if reach[hex] then
        table.insert(reachable, hex)
    end
end
local exits = api:copyExitGrids(source.path, source.elev, reachable, -2, 0, 0, 0)
print(string.format("%d worldmap exit grids (%d cut off from the start left out); player start at hex %d.",
    exits, #candidates - exits, start))
