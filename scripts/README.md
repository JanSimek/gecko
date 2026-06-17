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
Add `--json` for a machine-readable report (per-map floor/scenery with names, counts, and a `flat`
structural-vs-decoration flag) — the form an MCP agent reads to pick a biome and curate a palette.

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
| `analyze` | The `analyze --json` report (omit `maps` for all, or scope it), incl. the `flat` flag for palette curation. |
| `proto_info` | Resolve a PID to its type, engine display name and `flat` flag. |
| `generate` | Run a generation script (`script`, `out`, optional `elevation`, optional `args` map) and write a `.map`. Needs a scripting-enabled build. |

Built when `GECK_BUILD_MCP` is on (default; requires `GECK_BUILD_CLI`). To register it with an MCP
client, point the client at the `gecko-mcp` binary with the `--data` arguments for your install.
