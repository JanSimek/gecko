# Example generation scripts

Luau scripts for Gecko's **Script Console** (View → Script Console). Each edits the
currently shown elevation through the global `api` and collapses into one undo entry.

To run one: open the console, paste the script, press **Run** (Ctrl+Return). Fallout 2
data (master.dat) must be loaded so tile and proto names resolve.

Or run it headlessly with gecko-cli (a scripting-enabled build) — no editor, no GL:

```
gecko-cli map generate --script scripts/desert_terrain.luau --out out.map \
    --arg seed=42 --arg density=300 --data <master.dat>
```

This drives the same `api` in data-only mode: it paints tiles and places objects as map
data and writes a .map. (`gecko-cli map analyze` can read the result back.) Each `--arg
key=value` is exposed to the script as `args.key` (a string; use `tonumber` as needed),
so one script makes reproducible variants.

| Script | What it does |
|--------|--------------|
| [`desert_terrain.luau`](desert_terrain.luau) | Fills the floor with the wasteland tileset and scatters vegetation, reproducing the shipped desert-encounter palette. |

## The `api` surface

| Call | Returns | Notes |
|------|---------|-------|
| `api:isValidHex(hex)` | bool | hex grid is 200×200 (0..39999) |
| `api:hexNeighbors(hex)` | table | up-to-6 on-grid neighbours |
| `api:getFloor(tile)` / `api:getRoof(tile)` | tile id | tile grid is 100×100 (0..9999) |
| `api:tileId(name)` | int | tiles.lst index for e.g. `"edg5000"`; `-1` if unknown |
| `api:mapScenery(mapPath)` | table | the distinct scenery PIDs a reference map uses (e.g. `"maps/desert1.map"`); a curated, unique-PID palette |
| `api:paintFloor(tile, id)` / `api:paintRoof(tile, id)` | bool | |
| `api:placeObject(proPid, frmPid, hex, dir)` | bool | explicit art |
| `api:placeProto(proPid, hex, dir)` | bool | resolves the art FID from the proto |

The global `args` table holds the `--arg key=value` parameters (string values).

A proto's **PID** is its unique id; its display name is not unique. So a generator borrows a
palette by reading the actual scenery a shipped map uses (`mapScenery`) rather than guessing by
name. `gecko-cli map analyze --data <master.dat>` lists each map's floor tiles and `[Scenery]`/
`[Wall]` proto PIDs to explore what's available.
