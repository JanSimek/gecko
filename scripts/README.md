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

**Reproducing a run.** Each run is seeded randomly unless you pass `--arg seed=N`, so re-running
gives a fresh layout. The run's seed is always available to the script as `args.seed` (the host
fills it in when you don't); `terrain.luau` prints it, so when you get a layout you like, re-run
with that `--arg seed=<value>` to recreate it exactly.

| Script | What it does |
|--------|--------------|
| [`editor/terrain.luau`](editor/terrain.luau) | A curated desert generator: fills the floor with wasteland sand and scatters a **hand-picked palette** of desert vegetation (scrub/weeds/rocks/trees) in **natural clumps** (a `noise2d` density field, not an even sprinkle). Curated so only sensible decorations appear — swap `PALETTE`/`BASE_TILE` to retheme. Tune with `--arg density=N` / `--arg tile=<name>`. |
| [`editor/scatter.luau`](editor/scatter.luau) | A **parameterized** terrain.luau: the floor tile and scenery palette come from `--arg` (`--arg tile=edg5000 --arg palette=102,103,945`), so any biome generates without editing a script. Curate the palette from `gecko-cli map analyze --json` (pick the small, common, non-`flat` scenery) and pass it in. |
| [`editor/random_desert.luau`](editor/random_desert.luau) | A **worked template**: a frequency-**weighted mix** of the desert ground tileset (`edg5000`–`edg5007`, not one flat tile) and a weighted scrub/weed/cactus palette scattered in `noise2d` clumps. Weights are the real placement counts `analyze` reports across `desert1`–`desert6`. `--arg density/coverage/scale`; random each run (`--arg seed=N` to reproduce). |
| [`editor/random_camp.luau`](editor/random_camp.luau) | `random_desert` **plus structures**: scatters the desert, then drops `--arg tents=N` tents (spaced `--arg spacing` hexes apart) from a stamp captured with `extract_pattern`, via `api:placeStamp`. The worked example of the full extract→generate loop: `--stamp tent=tent.json --arg tents=3`. |

## The `api` surface

| Call | Returns | Notes |
|------|---------|-------|
| `api:isValidHex(hex)` | bool | hex grid is 200×200 (0..39999) |
| `api:hexNeighbors(hex)` | table | up-to-6 on-grid neighbours |
| `api:getFloor(tile)` / `api:getRoof(tile)` | tile id | tile grid is 100×100 (0..9999) |
| `api:tileId(name)` | int | tiles.lst index for e.g. `"edg5000"`; `-1` if the name is unknown (raises if no data) |
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
| `api:placeProtoXY(pid,col,row,dir)` / `api:placeObjectXY(pid,frm,col,row,dir)` | bool | `(col,row)` form of the placers (hex grid); off-grid is a no-op |
| `api:placeStamp(name, anchorHex, variant)` | int | place a pre-loaded stamp (a prefab captured by `extract_pattern`) so its anchor lands near `anchorHex`; returns objects placed. Load the stamp with `--stamp name=file.json` (CLI) / the `stamps` arg (MCP). Raises on an unknown `name`/`variant`. |

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
floor-tile borders that reveal a tileset's transitions — and per-map `clusters` grouping nearby
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
| `analyze` | The full `analyze --json` report (omit `maps` for all, or scope it): per-map and aggregate floor tiles, objects (each with `number` = the `api:proto` id, plus the `flat` palette-curation flag), `adjacency` — the floor-tile borders (which tile sits next to which different tile), i.e. the transitions to curate for seamless terrain — and per-map `clusters`: nearby objects grouped into structures (tents, buildings), each with a `centerHex`, bounding box and member PIDs, so an agent can locate one and feed its anchor/PIDs to `extract_pattern`. |
| `palette` | Just the **weighted generation palette** for the given maps, aggregated: `{ floor:[{id,name,weight}], scenery:[{pid,number,name,weight}] }`. The small input a generator needs — `id` for `api:paintFloor`, `number` for `api:proto`, `weight` = real placement count — without the full (large) `analyze` report. Scenery is scatter-eligible only (scenery type, non-flat). |
| `proto_info` | Resolve a PID to its type, engine display name and `flat` flag. |
| `generate` | Run a generation script (`script`, `out`, optional `elevation`, optional `args` map, optional `stamps` name→path map) and write a `.map`. Stamps are pre-loaded so the script places them with `api:placeStamp(name, anchorHex, variant)`. Needs a scripting-enabled build. |
| `render_map` | Render a map to a PNG (`map`, `out`, optional `elevation`, `maxDimension`, `showRoof`, `schematic`, `objects`, `showBlockers`) so the agent can *see* it. `schematic: true` flat-colours floor tiles by id and marks objects by category, returning a colour legend (id/type → colour → count) — match it to `analyze` and read the transitions. `objects: true` instead mutes the floor to grey so the object markers pop (for checking scatter/clumping). FLAT objects hidden unless `showBlockers`. `map`/`out` are filesystem paths — `out` is written there, and `map` may be a VFS path or any file on disk (e.g. one `generate` just wrote). Needs an off-screen GL context. |
| `extract_pattern` | Capture a structure from a real map into a reusable **pattern stamp** (`map`, `out`, `name`, optional `elevation`, `pids`, `anchorHex`, `radius`, `includeFloor`). Locate it with `pids` (the structure's proto PIDs from `analyze`) — their bounding box grown by `radius` hexes is the capture region, so immediate props nearby come along — or pass `anchorHex`. Objects captured verbatim; `includeFloor: true` also captures the floor/roof under the region (for structures whose floor is integral). The stamp JSON loads in the editor's pattern library and can be placed by `generate`. |

On `gecko-cli`: `map analyze [--json|--palette]`, `map generate ... [--stamp name=file.json ...]`, and
`map render --map <f.map> --out <f.png> [--elevation N] [--max-dim N] [--roof] [--schematic|--objects]
[--show-blockers]`. Pattern *extraction* is the MCP `extract_pattern` tool.

**Stamps end to end:** `analyze` a reference map → its `clusters` locate a structure (e.g. a tent:
a `Wall` + furniture cluster) → `extract_pattern` captures it to a `.json` → `generate --stamp
tent=tent.json` runs a script that calls `api:placeStamp("tent", api:hexIndex(col,row), 0)`
(`editor/random_camp.luau` is the worked example) and drops it into the new map. So a generator
scatters the *real* tents the desert maps use, not approximations.

The **schematic** render is the bridge between the JSON and the image: a raw render shows what the
map looks like, but the agent can't tell which pixels are tile `220`. In schematic mode the colours
*are* the ids (via the legend), so a colour region = a tile id, and a border between two colours =
an `adjacency` pair — the same transitions the `analyze` report lists.

Built when `GECK_BUILD_MCP` is on (default; requires `GECK_BUILD_CLI`). To register it with an MCP
client, point the client at the `gecko-mcp` binary with the `--data` arguments for your install.
