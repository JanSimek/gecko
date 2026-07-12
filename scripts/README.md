# Example generation scripts

Luau scripts for Gecko's **Script Console** (View → Script Console). Each edits the
currently shown elevation through the global `api` and collapses into one undo entry.

To run one: open the console, paste the script, press **Run** (Ctrl+Return). Fallout 2
data (master.dat) must be loaded so tile and proto names resolve.

Or run it headlessly with gecko-cli (a scripting-enabled build) — no editor, no GL:

```
gecko-cli map generate --script scripts/editor/terrain.luau --out out.map \
    --arg seed=42 --arg density=300 --data <master.dat>
```

This drives the same `api` in data-only mode: it paints tiles and places objects as map
data and writes a .map. (`gecko-cli map analyze` can read the result back.) Each `--arg
key=value` is exposed to the script as `args.key` (a string; use `tonumber` as needed),
so one script makes reproducible variants.

**Decorating an existing map.** Pass `--in <file.map>` (a VFS path like `maps/desert1.map`, or any
`.map` on disk) and the script edits a loaded copy of that map instead of starting empty — the way
the editor's Script Console runs against the open map. The input is never modified; the result goes
to `--out`.

**Batch generation.** `--count N` runs the script N times, each against a fresh map (empty, or a
fresh copy of `--in`), writing `<out>_1.map` … `<out>_N.map`. The runs are seeded consecutively
from a base seed — `--arg seed=N` if given, otherwise a random base the CLI prints — so the maps
differ from each other, yet the whole batch reproduces from that one seed.

**Reproducing a run.** Each run is seeded randomly unless you pass `--arg seed=N`, so re-running
gives a fresh layout. The run's seed is always available to the script as `args.seed` (the host
fills it in when you don't); `terrain.luau` prints it, so when you get a layout you like, re-run
with that `--arg seed=<value>` to recreate it exactly. The same seed drives both `math.random`
and `api:rng()`/`api:rngInt()`.

**Sanity check.** After writing the map, `generate` runs the reachability analysis on it and
prints a warning when any exit grid cannot be walked to from the player start — the usual sign
of a sealed room or a spawn placed inside solid terrain.

| Script | What it does |
|--------|--------------|
| [`editor/terrain.luau`](editor/terrain.luau) | A curated desert generator: fills the floor with wasteland sand and scatters a **hand-picked palette** of desert vegetation (scrub/weeds/rocks/trees) in **natural clumps** (a `noise2d` density field, not an even sprinkle). Curated so only sensible decorations appear — swap `PALETTE`/`BASE_TILE` to retheme. Tune with `--arg density=N` / `--arg tile=<name>`. Pass `--arg ref=maps/desert1.map` to **quilt** the floor from that map (`api:quiltFloorRect`) — seamless, hand-authored-style tile blending instead of the flat fill (`--arg refElevation=N` picks the reference elevation). |
| [`editor/scatter.luau`](editor/scatter.luau) | A **parameterized** terrain.luau: the floor tile and scenery palette come from `--arg` (`--arg tile=edg5000 --arg palette=102,103,945`), so any biome generates without editing a script. Curate the palette from `gecko-cli map analyze --json` (pick the small, common, non-`flat` scenery) and pass it in. |
| [`editor/random_desert.luau`](editor/random_desert.luau) | A **worked template**: a frequency-**weighted mix** of the desert ground tileset (`edg5000`–`edg5007`, not one flat tile) and a weighted scrub/weed/cactus palette scattered in `noise2d` clumps. Weights are the real placement counts `analyze` reports across `desert1`–`desert6`. `--arg density/coverage/scale`; random each run (`--arg seed=N` to reproduce). |
| [`editor/random_camp.luau`](editor/random_camp.luau) | `random_desert` **plus structures**: scatters the desert, then drops `--arg tents=N` tents (spaced `--arg spacing` hexes apart) from a stamp captured with `extract_pattern`, via `api:placeStamp`. The worked example of the full extract→generate loop: `--stamp tent=tent.json --arg tents=3`. |
| [`editor/quilt_sampler.luau`](editor/quilt_sampler.luau) | A **patchwork sampler**: fills the map with a grid of patches, each **quilted** (`api:quiltFloorRect`) from a different shipped reference map — see several biomes' hand-authored tile blending side by side. Prints per-patch fidelity (`repaired` / `seams unmatched`) and a final coverage tally. `--arg refs=<map,map,...>` / `--arg patch=N`. Excludes the known black filler (`bld2043`) so it also works on Restoration Project mounts. |
| [`editor/quilt_biomes.luau`](editor/quilt_biomes.luau) | A **two-biome generator**: a smooth `noise2d` field splits the floor into organic zones and each zone is quilted from its own reference (`api:quiltFloorTiles` over the irregular regions), so two grounds meet along a wandering natural boundary. `--arg refA/refB/coverage/scale`. Same `bld2043` exclusion. |
| [`fills/quilt_desert.luau`](fills/quilt_desert.luau) / [`fills/quilt_mountain.luau`](fills/quilt_mountain.luau) | **Quilting as a point-and-click fill**: select an area in the editor, Edit ▸ Fill Selection, pick *Quilted Desert* / *Quilted Mountain* — the selection's floor is synthesized from the reference map with live ghost preview and a one-Ctrl-Z apply. Headless (`generate`) they quilt the whole elevation; `--arg ref=` / `--arg refElevation=` override the reference. |
| [`editor/cave.luau`](editor/cave.luau) | A **real cave-interior generator, learned from the shipped cave maps**: fills the map with the reference's solid-rock tile pattern, carves a smooth **metaball cavern** — chambers are radial field sources joined by corridor line sources, so the walkable region is one connected blob whose edge is a smooth organic *isocontour*, not a blocky tile stamp — painted with the reference's pervasive **cav\* field tiles** (edg\* sand and rare border/transition tiles like `cav4013` excluded, so the floor never speckles). It then lines the rim with a dense **2-cell band of Cave Wall faces** — every hex that straddles the field isocontour, so the run is gap-free and traces the true curve at *hex* resolution rather than the tile edge — using only the standing faces, never the `protoFlat` rock-fill art the same family carries. Each piece is classed by the **field gradient** (the outward normal) and drawn from the per-orientation palette the mappers actually used there (`maps/cave1..4.map` via `mapFloorAt`/`mapObjectsAt`/`hexTile`/`hexDir`/`protoFlat`), so the isometric view gets rock faces on the north/east rim and filled tops on the south/west instead of one texture butted against another, and the variety matches the source. Seals the rim with invisible blocking hexes, rings the cavern with scroll blockers (`hexesOnScreenRect`), and drops a worldmap exit patch + player start. `--arg chambers/refs/exit/floorFieldFraction`; `--count N` batches distinct, reproducible caves; `generate` verifies the exits are reachable. |

## The `api` surface

| Call | Returns | Notes |
|------|---------|-------|
| `api:isValidHex(hex)` | bool | hex grid is 200×200 (0..39999) |
| `api:hexNeighbors(hex)` | table | up-to-6 on-grid neighbours |
| `api:hexDir(fromHex, toHex)` | int | direction `0..5` of the step onto an adjacent hex (parity-independent; `hexNeighbors`' order); `-1` if not neighbours — key a wall/edge chain on how it turns |
| `api:getFloor(tile)` / `api:getRoof(tile)` | tile id | tile grid is 100×100 (0..9999) |
| `api:tileId(name)` | int | tiles.lst index for e.g. `"edg5000"`; `-1` if the name is unknown (raises if no data) |
| `api:tilesByPrefix(prefix)` | table | `{ [name] = id }` for every tiles.lst entry starting with `prefix` (case-insensitive) — pick from a tile family (`"cav"`, `"edg"`) without hardcoding ids; raises if no data |
| `api:proto(type, number)` | PID | build a proto PID from a type name (`"scenery"`, `"item"`, `"critter"`, `"wall"`, `"tile"`, `"misc"`) and the id `map analyze` reports — `api:proto("scenery", 102)` == `0x02000066`, no opaque hex |
| `api:noise2d(x, y)` | number | coherent value noise in `[0,1]`; sample as a density field for clumped, non-uniform placement |
| `api:protoName(pid)` | string | the proto's engine display name (e.g. `"Scrub"`); lets you tell a decoration from a structural feature |
| `api:mapScenery(mapPath)` | table | the distinct scenery PIDs a reference map uses (e.g. `"maps/desert1.map"`); upright decorations only (blockers excluded) |
| `api:mapSceneryHistogram(mapPath)` | table | `{ [pid] = count }` — each scenery PID with how many times the reference places it |
| `api:mapFloorTiles(mapPath)` | table | the floor-tile ids a reference map uses, most-used first |
| `api:listMaps()` | table | every map path in the mounted data (empty if none) |
| `api:paintFloor(tile, id)` / `api:paintRoof(tile, id)` | bool | |
| `api:placeObject(proPid, frmPid, hex, dir)` | bool | explicit art; `false` skips the hex (art missing / off-grid) |
| `api:placeProto(proPid, hex, dir)` | bool | resolves the art FID from the proto; `false` skips the hex |
| `api:hexIndex(col, row)` / `api:tileIndex(col, row)` | int | `(col, row)` → linear index (`row*width+col`); `-1` if off-grid. Hexes 200×200, tiles 100×100 |
| `api:hexCol(hex)` / `api:hexRow(hex)` / `api:tileCol(t)` / `api:tileRow(t)` | int | the inverse — index → column / row |
| `api:paintFloorXY(col,row,id)` / `api:paintRoofXY(...)` | bool | `(col,row)` form of the painters (tile grid) |
| `api:getFloorXY(col,row)` / `api:getRoofXY(col,row)` | tile id | `(col,row)` form of the readers |
| `api:tilesInRect(col0,row0,col1,row1)` | table | tile indices in the inclusive rectangle, ascending; corners in any order, clamped to the grid |
| `api:hexTile(hex)` / `api:tileHexes(tile)` | int / table | the **exact tile↔hex bridge** (screen geometry, not naive halving): the floor tile under a hex, and the hexes standing on a tile — what lets walls/blockers follow a floor boundary precisely |
| `api:hexesOnScreenRect(centerHex, halfW, halfH)` | table | the hex border of a screen-space rectangle (the `placeExitGridRect` walk as a query) — ring an area with scroll blockers or anything else |
| `api:mapFloorAt(mapPath, elev)` | table | a reference map's **full floor grid** (10000 ids, tile-index order) — learn exact layouts (rock patterns, walkable regions) |
| `api:mapObjectsAt(mapPath, elev, type)` | table | a reference map's objects of one type as flat `(pid, hex, direction)` triples — learn engine-authored placement (e.g. which cave-wall variants line a given rim connection shape) |
| `api:protoBlocks(pid)` | bool | does the proto block movement (NO_BLOCK clear)? raises if the proto can't load |
| `api:protoFlat(pid)` | bool | is the proto `OBJECT_FLAT` (ground-hugging fill/rubble, not a standing wall face)? tells a boundary wall from the flat rock texture that carpets solid ground; raises if the proto can't load |
| `api:protoFid(pid)` | fid | the proto's art FID (what `placeProto` resolves and stores) — decode with `resolve_fid` / render it to identify a proto's art instead of guessing from the number; raises if the proto can't load |
| `api:setElevation(e)` | nil | switch which elevation (0..2) subsequent queries/edits target — author multi-level maps in one run; raises if the map lacks it |
| `api:fillFloorRect(col0,row0,col1,row1,id)` / `api:fillRoofRect(...)` | int | paint every tile in the rectangle; returns tiles painted |
| `api:fillRegion(col,row,id)` | int | flood-fill (paint-bucket): repaint the 4-connected region of the tile id found at `(col,row)`; returns tiles painted |
| `api:quiltFloorRect(mapPath,refElev,col0,row0,col1,row1)` | int | **seamless floor synthesis**: paint the rectangle by patch-quilting the reference map's floor grid, so the edge/corner tile families transition the way the hand-authored map does (the seamless form of `fillFloorRect`); blends into non-empty tiles at the border. Seeded and reproducible; returns tiles painted; raises on an unreadable map / missing elevation |
| `api:quiltFloorTiles(mapPath,refElev,{tile,...})` | int | `quiltFloorRect` for an explicit set of floor-tile indices — retexture an irregular region (e.g. a carved cavern) seamlessly |
| `api:quiltStats()` | table | the previous quilt's fidelity, flattened: `{painted, blocks, perfect, mismatched, repaired, unresolvedSeams}` — repaired cells are normal in small numbers; `unresolvedSeams` count borders the reference never showed |
| `api:quiltObjects(type, {excludePid,...})` | int | **transplant the reference objects standing on the cells the last quilt copied** — pid/art/direction verbatim, positions translated exactly, so a quilted mountain brings the cliff-face scenery its floor art belongs with. `excludePid` skips invisible engine markers (scroll blockers, block hexes); returns objects placed |
| `api:rng()` / `api:rngInt(lo,hi)` | number / int | draws from the run's seeded stream — cross-platform-deterministic (unlike `math.random`'s float path), so a seed reproduces exactly |
| `api:placeProtoXY(pid,col,row,dir)` / `api:placeObjectXY(pid,frm,col,row,dir)` | bool | `(col,row)` form of the placers (hex grid); off-grid is a no-op |
| `api:placeStamp(name, anchorHex, variant)` | int | place a pre-loaded stamp (a prefab captured by `extract_pattern`) so its anchor lands near `anchorHex`; returns objects placed. Load the stamp with `--stamp name=file.json` (CLI) / the `stamps` arg (MCP); in the editor's **Script Console** stamps are auto-registered from the bundled examples (`resources/scripts/stamps/`, which ships a `tent`) and from your saved patterns, so no loading step is needed. Raises on an unknown `name`/`variant`. |
| `api:newMap()` | nil | reset the bound map to a fresh empty Fallout 2 map — no objects, empty floor/roof on all three elevations, default header. Lets a generator start from a blank slate (in the **Script Console** this clears the open map). **Destructive and not undoable** (like File ▸ New), so call it *first*, before any placement. The CLI/MCP `generate` already starts empty, so it's a no-op there. |
| `api:setPlayerStart(hex, orientation, elevation)` | nil | set the player spawn in the map header (where the engine drops the player on load). `orientation` 0..5, `elevation` 0..2. Raises on an out-of-range value. Header state, so it is not part of the undo batch. |
| `api:placeExitGrid(hex, destMapId, destHex, destElevation, orientation)` | bool | place a map-exit grid at `hex`; stepping onto it sends the player to `destMapId` at `destHex` (`destElevation`, facing `orientation`). `destMapId` **-2** = worldmap, **-1** = town map, else a map id (a world/town exit ignores `destHex`). Returns false only if the exit-grid art can't load (GUI); raises on an off-grid `hex` or out-of-range destination. Without an exit a generated map has no way out. |
| `api:placeExitGridRect(centerHex, screenHalfWidth, screenHalfHeight, destMapId, destHex, destElevation, orientation)` | int | place a border of exit grids forming a **rectangle on screen**, centred on `centerHex`, `screenHalfWidth`/`Height` pixels to each side (engine iso projection — the hex border staircases). Each edge uses its matching directional exit-grid art, like shipped maps (e.g. `bhrnddst.map`); all markers share the destination (same args as `placeExitGrid`). Returns markers placed. Frames the playable area so walking off any edge exits. |

**Errors.** A genuine failure — no Fallout 2 data mounted, or a wrong `--arg reference=` path —
**raises**, so the run stops with a clear message instead of silently producing an empty map. Wrap
a call in `pcall` to handle it yourself:

```lua
local ok, tiles = pcall(function() return api:mapFloorTiles(ref) end)
if not ok then print("couldn't read " .. ref); return end
```

Things that are merely *not applicable* stay ordinary return values: `placeProto`/`placeObject`
return `false` to skip a hex, `tileId` returns `-1` for an unknown name, `listMaps` returns `{}`.

The global `args` table holds the `--arg key=value` parameters (string values). `args.seed` is
always set: the value you passed, or a fresh random one the host chose for this run — seed it back
with `--arg seed=<value>` to reproduce a layout.

A proto's **PID** is its unique id; its display name is *not* unique, so a script can't reliably
address protos by name. The next best thing is to name the **id** yourself and let `api:proto`
build the PID, so a curated palette reads in words instead of hex:

```lua
local SCRUB, TREE = 102, 945      -- ids from `gecko-cli map analyze`
api:placeProto(api:proto("scenery", SCRUB), hex, 0)
```

`gecko-cli map analyze --data <master.dat>` lists each map's floor tiles and `[Scenery]`/`[Wall]`
protos (with `api:protoName(pid)` giving the engine display name), to find the ids worth naming.
Add `--json` for a machine-readable report (per-map and aggregate floor/scenery with names, counts,
each object's `number` for `api:proto`, a `flat` structural-vs-decoration flag, `adjacency` — the
**directional** floor-tile borders that reveal a tileset's transitions (`dir` `"E"`/`"S"`: b sits
east/south of a; the reverse direction is the swapped pair) — and per-map `clusters` grouping nearby
objects into structures) — the form an MCP agent reads to pick a biome, curate a palette, and locate
structures to extract as stamps. For *just* the weighted generation palette, use `--palette` (the
MCP `palette` tool): `{ floor:[{id,name,weight}], scenery:[{pid,number,name,weight}] }`, aggregated
across the maps — exactly the input a generator script needs, without the full report.

## MCP server (`gecko-mcp`)

`gecko-mcp` exposes the same headless logic to an AI agent over the [Model Context
Protocol](https://modelcontextprotocol.io): a newline-delimited JSON-RPC 2.0 server on stdio.
Mount the game data with `--data` (repeatable, dir or `.dat`); an agent can then inspect the
existing maps, curate a palette, and generate a new one — the loop the analyze/scatter workflow
above does by hand.

```jsonc
gecko-mcp --data <master.dat>
// then on stdin:
{"jsonrpc":"2.0","id":1,"method":"initialize"}
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"analyze","arguments":{}}}
```

| Tool | Purpose |
|------|---------|
| `list_maps` | Every `.map` in the mounted data. |
| `analyze` | The full `analyze --json` report (omit `maps` for all, or scope it): per-map and aggregate floor tiles, objects (each with `number` = the `api:proto` id, plus the `flat` palette-curation flag), `adjacency` — the directional floor-tile borders (which tile sits east/south of which different tile), i.e. the transitions to curate for seamless terrain — and per-map `clusters`: nearby objects grouped into structures (tents, buildings), each with a `centerHex`, bounding box and member PIDs, so an agent can locate one and feed its anchor/PIDs to `extract_pattern`; and per-map `critters`: each critter with its `hex`, `team` (group_id), effective `aiPacket`, the AI resolved via `ai.txt` (`ai`: name, aggression, disposition, run-away / best-weapon / distance), and the attached `script` (`{programIndex, name}`) — feed that `programIndex` to `describe_script` for the script's `.ssl` source and dialog, so an agent can read who is on the map, how they fight, and what they do/say. |
| `palette` | Just the **weighted generation palette** for the given maps, aggregated: `{ floor:[{id,name,weight}], scenery:[{pid,number,name,weight}] }`. The small input a generator needs — `id` for `api:paintFloor`, `number` for `api:proto`, `weight` = real placement count — without the full (large) `analyze` report. Scenery is scatter-eligible only (scenery type, non-flat). |
| `proto_info` | Resolve a PID to its type, engine display name and `flat` flag. |
| `generate` | Run a generation script (`script`, `out`, optional `in` — an existing `.map` the script decorates instead of starting empty — optional `elevation`, `count` (batch: writes `<out>_1.map`…`<out>_N.map` with consecutive seeds), `args` map, `stamps` name→path map) and write a `.map`. Stamps are pre-loaded so the script places them with `api:placeStamp(name, anchorHex, variant)`. Needs a scripting-enabled build. |
| `render_map` | Render a map to a PNG (`map`, `out`, optional `elevation`, `maxDimension`, `showRoof`, `schematic`, `objects`, `showBlockers`) so the agent can *see* it. `schematic: true` flat-colours floor tiles by id and marks objects by category, returning a colour legend (id/type → colour → count) — match it to `analyze` and read the transitions. `objects: true` instead mutes the floor to grey so the object markers pop (for checking scatter/clumping). FLAT objects hidden unless `showBlockers`. `map`/`out` are filesystem paths — `out` is written there, and `map` may be a VFS path or any file on disk (e.g. one `generate` just wrote). Needs an off-screen GL context. |
| `extract_pattern` | Capture a structure from a real map into a reusable **pattern stamp** (`map`, `out`, `name`, optional `elevation`, `pids`, `anchorHex`, `radius`, `includeFloor`, `includeRoof`). Locate it with `pids` (the structure's proto PIDs from `analyze`) — their bounding box grown by `radius` hexes is the capture region, so immediate props nearby come along — or pass `anchorHex`. Objects captured verbatim; `includeFloor: true` captures the ground and `includeRoof: true` captures the roof layer (a tent/building roof is tiles, not an object — without it the stamp is topless). The stamp JSON loads in the editor's pattern library and can be placed by `generate`. |
| `script_api` | The generation-script `api` reference (Markdown, generated from the bound surface so it can't drift): every `api:` function with its signature, plus the non-obvious runtime behaviour (runs are **auto-seeded** and **auto-batched**) and the error model. Read it before writing a script for `generate`. |

On `gecko-cli`:
- `map analyze [--json|--palette]`
- `map generate ... [--in <f.map>] [--count N] [--stamp name=file.json ...]`
- `map render --map <f.map> --out <f.png> [--elevation N] [--max-dim N] [--roof] [--schematic|--objects] [--show-blockers]`
- `map extract-pattern --map <f.map> --out <f.json> --name <n> [--pids id,...] [--anchor <hex>] [--radius N] [--include-floor] [--include-roof]` (also the MCP `extract_pattern` tool)

**Stamps end to end** — `random_camp.luau` needs a stamp; make one first, then pass it:
```
# 1. find the structure (its cluster centerHex, or its proto ids) with analyze, then capture it.
#    --include-roof grabs the tent canvas (it lives on the roof layer, not as an object — without
#    it the tent comes out topless):
gecko-cli map extract-pattern --map /maps/desert5.map --out tent.json --name tent \
    --anchor <centerHex> --radius 6 --include-roof  --data <master.dat> [--data <critter.dat>]
# 2. generate, loading the stamp so api:placeStamp can drop it:
gecko-cli map generate --script scripts/editor/random_camp.luau --out camp.map \
    --stamp tent=tent.json --arg tents=3  --data <master.dat> [--data <critter.dat>]
```
Run `random_camp.luau` **without** `--stamp` and it generates the desert but skips the tents (a
warning, not an error). So a generator scatters the *real* tents the desert maps use. The editor's
Script Console needs no `--stamp`: it ships a bundled `tent` (in `resources/scripts/stamps/`) and
also registers your saved patterns, so the worked example runs out of the box.

The **schematic** render is the bridge between the JSON and the image: a raw render shows what the
map looks like, but the agent can't tell which pixels are tile `220`. In schematic mode the colours
*are* the ids (via the legend), so a colour region = a tile id, and a border between two colours =
an `adjacency` pair — the same transitions the `analyze` report lists.

Every tool carries MCP tool annotations in `tools/list` (`readOnlyHint`, `destructiveHint: false`,
`openWorldHint: false`), so a client can tell the inspection tools from the four that write output
files (`generate`, `render_map`, `render_frm`, `extract_pattern`) without reading the descriptions.

Built when `GECK_BUILD_MCP` is on (default; requires `GECK_BUILD_CLI`). To register it with an MCP
client, point the client at the `gecko-mcp` binary with the `--data` arguments for your install.
