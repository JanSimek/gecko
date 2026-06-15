# Example generation scripts

Luau scripts for Gecko's **Script Console** (View → Script Console). Each edits the
currently shown elevation through the global `api` and collapses into one undo entry.

To run one: open the console, paste the script, press **Run** (Ctrl+Return). Fallout 2
data (master.dat) must be loaded so tile and proto names resolve.

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
| `api:paintFloor(tile, id)` / `api:paintRoof(tile, id)` | bool | |
| `api:placeObject(proPid, frmPid, hex, dir)` | bool | explicit art |
| `api:placeProto(proPid, hex, dir)` | bool | resolves the art FID from the proto |

Discover tile and proto ids for a palette with `gecko-cli map analyze --data <master.dat>`
(the floor-tile names and the `[scenery]`/`[wall]` PIDs).
