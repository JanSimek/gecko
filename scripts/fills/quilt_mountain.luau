-- name: Quilted Mountain
--
-- Seamless MOUNTAIN terrain for the current selection, patch-quilted from a real shipped map
-- (maps/mountn2.map) — with a structured, CONTINUOUS cliff. Shipped maps author one screen-sized
-- viewport (fenced by scroll blockers) holding distinct zones: open rocky ground and a cliff
-- band crossing the whole visible area. Quilting blindly from the whole map would scatter
-- disconnected cliff fragments, so this fill works zone by zone via api:quiltSource — ground is
-- learned only from the reference's open-ground area, and a generator-drawn cliff line across
-- the full width is quilted only from the reference's cliff band, with the standing cliff
-- scenery transplanted along it (api:quiltObjects). Seeded: the same seed reproduces exactly.
--
--   gecko-cli map generate --script scripts/fills/quilt_mountain.luau --out out.map --data <master.dat>
--
-- --arg ref           reference map to learn from (default maps/mountn2.map)
-- --arg refElevation  reference elevation (default 0)
-- --arg cliff         "off" skips the cliff band (default on when the area is tall enough)
-- --arg cliffAt       0..1 latitude of the cliff line inside the area (default 0.32)
--
-- --arg groundSrc     reference ground zone, "col0,row0,col1,row1" (default mountn2's open ground)
-- --arg cliffSrc      reference cliff band, same form (default mountn2's cliff rows)
--
-- The default zone rectangles describe mountn2's viewport (fence at tile cols 36-56, rows 39-56;
-- cliff band straddling the fence top). For another reference, read its zones from
-- `map dump-grid` (the scroll blockers frame the viewport) and pass them as args.

local REF = args.ref or "maps/mountn2.map"
local REF_ELEVATION = tonumber(args.refElevation) or 0
local CLIFF_AT = tonumber(args.cliffAt) or 0.32
local function rectArg(value, default)
    if not value then
        return default
    end
    local rect = {}
    for n in string.gmatch(value, "[^,]+") do
        rect[#rect + 1] = tonumber(n)
    end
    assert(#rect == 4, "zone rects need four numbers: col0,row0,col1,row1")
    return rect
end
local GROUND_SRC = rectArg(args.groundSrc, { 37, 42, 55, 55 }) -- the reference's open ground
local CLIFF_SRC = rectArg(args.cliffSrc, { 36, 32, 56, 42 }) -- the reference's cliff band
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

-- Invisible engine markers that verbatim transplanting must not carry (misc-type markers like
-- scroll blockers are skipped by not requesting "misc"; the mountain references' only walls are
-- see-through blockers, so walls are not transplanted at all).
local MARKERS = {
    api:proto("scenery", 344), -- Block Hex Auto Inviso
    api:proto("scenery", 67), -- Secret Blocking Hex
    api:proto("scenery", 49), -- Exit Grid Map Marker
    -- The Restoration Project's black-mask scenery (art/scenery/black00x.frm): solid black
    -- patches its maps lay over unreachable cliff tops — cover art, not cliff art.
    api:proto("scenery", 2296),
    api:proto("scenery", 2297),
}

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

-- The area's bounding box decides where the cliff line runs.
local minC, maxC, minR, maxR = TILE_W, -1, TILE_H, -1
for _, t in ipairs(tiles) do
    local c, r = api:tileCol(t), api:tileRow(t)
    minC, maxC = math.min(minC, c), math.max(maxC, c)
    minR, maxR = math.min(minR, r), math.max(maxR, r)
end

-- Draw the cliff line: one continuous horizontal band across the full width, at CLIFF_AT.
-- The band is laid by a 1-D column walk over the reference band (below), which keeps rows
-- aligned — so the line is straight; place it where you want with --arg cliffAt.
local wantCliff = args.cliff ~= "off" and (maxR - minR) >= 14
local SRC_H = CLIFF_SRC[4] - CLIFF_SRC[2] + 1
local bandTop = nil
if wantCliff then
    bandTop = math.floor(minR + (maxR - minR) * CLIFF_AT + 0.5) - math.floor(SRC_H / 2)
    bandTop = math.max(minR, bandTop)
end

-- Ground covers the WHOLE area — the cliff walk overwrites its band afterwards, and skips the
-- reference's filler cells so quilted ground shows through where the reference had black mask.
local ground = tiles

-- Pass 1: open ground, learned only from the reference's ground zone.
api:quiltSource(GROUND_SRC[1], GROUND_SRC[2], GROUND_SRC[3], GROUND_SRC[4])
local painted = api:quiltFloorTiles(REF, REF_ELEVATION, ground)
local pieces = api:quiltObjects("scenery", MARKERS)

-- Pass 2: the cliff — a 1-D column walk over the reference band. Each target column copies one
-- reference column (tiles + the scenery standing on it); the walk prefers the natural successor
-- column and, to vary, jumps only to a column whose full tile signature is IDENTICAL — every
-- horizontal seam is one the reference itself contains, so the cliff is continuous by
-- construction. (2-D quilting is the wrong tool for a linear feature: its band pool is too thin
-- and every repaired cell breaks the line.)
local restarts = 0
if wantCliff then
    local C0, R0, C1 = CLIFF_SRC[1], CLIFF_SRC[2], CLIFF_SRC[3]
    local refFloor = api:mapFloorAt(REF, REF_ELEVATION)

    local vecs, byVec = {}, {}
    for sc = C0, C1 do
        local v = {}
        for dr = 0, SRC_H - 1 do
            v[#v + 1] = refFloor[(R0 + dr) * TILE_W + sc + 1]
        end
        local key = table.concat(v, ",")
        vecs[sc] = key
        byVec[key] = byVec[key] or {}
        table.insert(byVec[key], sc)
    end

    -- The reference band's scenery, grouped by the column its hex stands on (markers skipped).
    local flat = api:mapObjectsAt(REF, REF_ELEVATION, "scenery")
    local objByCol = {}
    for i = 1, #flat, 3 do
        local pid, hex, dir = flat[i], flat[i + 1], flat[i + 2]
        local isMarker = false
        for _, m in ipairs(MARKERS) do
            if pid == m then
                isMarker = true
            end
        end
        if not isMarker then
            local st = api:hexTile(hex)
            if st >= 0 then
                local sc, sr = api:tileCol(st), api:tileRow(st)
                if sc >= C0 and sc <= C1 and sr >= R0 and sr < R0 + SRC_H then
                    objByCol[sc] = objByCol[sc] or {}
                    table.insert(objByCol[sc], { pid, hex, dir })
                end
            end
        end
    end

    local src = C0 + math.floor(api:rng() * (C1 - C0 - 1))
    for c = minC, maxC do
        -- Copy source column `src` to target column c at bandTop.
        for dr = 0, SRC_H - 1 do
            local id = refFloor[(R0 + dr) * TILE_W + src + 1]
            if id ~= 1 and id ~= filler then
                painted = painted + (api:paintFloorXY(c, bandTop + dr, id) and 1 or 0)
            end
        end
        for _, o in ipairs(objByCol[src] or {}) do
            local pid, hex, dir = o[1], o[2], o[3]
            local hc, hr = hex % 200, math.floor(hex / 200)
            local target = api:hexIndex(hc + 2 * (c - src), hr + 2 * (bandTop - R0))
            if target >= 0 and api:placeProto(pid, target, dir) then
                pieces = pieces + 1
            end
        end

        -- Advance: natural successor, with occasional identical-signature jumps for variety.
        local candidates = byVec[vecs[src]]
        local jump = nil
        if #candidates > 1 and (src >= C1 or api:rng() < 0.15) then
            local pick = candidates[1 + math.floor(api:rng() * #candidates)]
            if pick ~= src and pick < C1 then
                jump = pick
            end
        end
        if jump then
            src = jump + 1
        elseif src < C1 then
            src = src + 1
        else
            src = C0 + math.floor(api:rng() * (C1 - C0 - 1)) -- no seamless option: restart (visible seam)
            restarts = restarts + 1
        end
    end
end
api:quiltSource(-1, -1, -1, -1)

local stats = api:quiltStats()
print(string.format("Quilted %d floor tiles + %d standing pieces from %s (cliff %s, %d hard seams; ground pass: %d repaired, %d unmatched).",
    painted, pieces, REF, wantCliff and "on" or "off", restarts, stats[5], stats[6]))
