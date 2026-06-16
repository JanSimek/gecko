# Example generation scripts

Luau scripts for Gecko's **Script Console** (View → Script Console). Each edits the
currently shown elevation through the global `api` and collapses into one undo entry.

To run one: open the console, paste the script, press **Run** (Ctrl+Return). Fallout 2
data (master.dat) must be loaded so tile and proto names resolve.

Or run it headlessly with gecko-cli (a scripting-enabled build) — no editor, no GL:

```
gecko-cli map generate --script scripts/terrain.luau --out out.map \
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
| [`terrain.luau`](terrain.luau) | Reproduces a shipped map's terrain — fills the floor with its dominant ground tile and scatters its scenery **weighted by how often the reference uses each proto** (common vegetation dominates; rare features stay rare), matching the reference's object count by default. Pick the reference with `--arg reference=maps/cave0.map` (or omit for a random map), the count with `--arg density=N`. |

## The `api` surface

| Call | Returns | Notes |
|------|---------|-------|
| `api:isValidHex(hex)` | bool | hex grid is 200×200 (0..39999) |
| `api:hexNeighbors(hex)` | table | up-to-6 on-grid neighbours |
| `api:getFloor(tile)` / `api:getRoof(tile)` | tile id | tile grid is 100×100 (0..9999) |
| `api:tileId(name)` | int | tiles.lst index for e.g. `"edg5000"`; `-1` if unknown |
| `api:mapScenery(mapPath)` | table | the distinct scenery PIDs a reference map uses (e.g. `"maps/desert1.map"`); upright decorations only (blockers excluded) |
| `api:mapSceneryHistogram(mapPath)` | table | `{ [pid] = count }` — each scenery PID with how many times the reference places it; scatter proportional to these to match the real mix |
| `api:mapFloorTiles(mapPath)` | table | the floor-tile ids a reference map uses, most-used first |
| `api:listMaps()` | table | every map path in the mounted data (for a random reference) |
| `api:paintFloor(tile, id)` / `api:paintRoof(tile, id)` | bool | |
| `api:placeObject(proPid, frmPid, hex, dir)` | bool | explicit art |
| `api:placeProto(proPid, hex, dir)` | bool | resolves the art FID from the proto |

The global `args` table holds the `--arg key=value` parameters (string values). `args.seed` is
always set: the value you passed, or a fresh random one the host chose for this run — seed it back
with `--arg seed=<value>` to reproduce a layout.

A proto's **PID** is its unique id; its display name is not unique. So a generator borrows a
palette by reading the actual scenery a shipped map uses (`mapScenery`) rather than guessing by
name. `gecko-cli map analyze --data <master.dat>` lists each map's floor tiles and `[Scenery]`/
`[Wall]` proto PIDs to explore what's available.
