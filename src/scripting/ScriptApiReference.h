#pragma once

#include <string>
#include <vector>

namespace geck {

/// The single source of truth for the generation-script `api:` surface. Each bound function is
/// listed once as X(name, signature, doc); the `name` is both the Lua name and the MapScriptApi
/// method (they match). LuaScriptRuntime expands this to bind the functions (addFunction), and
/// ScriptApiReference expands it to document them — so the reference can never drift from what is
/// actually bound. To add an api function, add one line here and implement the method.
#define GECK_SCRIPT_API(X)                                                                                                                                             \
    /* Queries (no mutation) */                                                                                                                                        \
    X(isValidHex, "(hex) -> bool", "Is hex an on-grid hex index (0..39999)?")                                                                                          \
    X(hexNeighbors, "(hex) -> {hex,...}", "The up-to-6 on-grid neighbour hexes.")                                                                                      \
    X(getFloor, "(tileIndex) -> tileId", "Floor tile id at a tile index (EMPTY_TILE if out of range).")                                                                \
    X(getRoof, "(tileIndex) -> tileId", "Roof tile id at a tile index.")                                                                                               \
    X(getFloorXY, "(col, row) -> tileId", "Floor tile id at a tile (col, row).")                                                                                       \
    X(getRoofXY, "(col, row) -> tileId", "Roof tile id at a tile (col, row).")                                                                                         \
    X(tileId, "(name) -> int", "Resolve a ground-tile FRM name (e.g. \"edg5000\") to its tiles.lst id; -1 if unknown.")                                                \
    X(protoName, "(pid) -> string", "The proto's engine display name (e.g. \"Scrub\"); empty if unresolved.")                                                          \
    X(proto, "(typeName, number) -> pid", "Build a PID from a type (\"scenery\"/\"wall\"/...) and the analyze `number`.")                                              \
    X(mapScenery, "(mapPath) -> {pid,...}", "Distinct scatter-eligible scenery PIDs a reference map uses (blockers filtered).")                                        \
    X(mapSceneryHistogram, "(mapPath) -> {pid=count}", "Those scenery PIDs with their placement counts (real frequency).")                                             \
    X(mapFloorTiles, "(mapPath) -> {id,...}", "A reference map's floor-tile ids, most-used first.")                                                                    \
    X(listMaps, "() -> {path,...}", "Every map file in the mounted data.")                                                                                             \
    X(noise2d, "(x, y) -> [0,1]", "Coherent value noise — a density field for natural clumps/clearings.")                                                              \
    X(noise3d, "(x, y, z) -> [0,1]", "Coherent 3D value noise; the z axis varies the field per seed/octave.")                                                          \
    X(objectAt, "(hex) -> pid", "PID of an object occupying hex in the committed map (0 if none).")                                                                    \
    /* Selection area (host-set per run; empty when no area is bound) */                                                                                               \
    X(hasArea, "() -> bool", "True if a selection area is bound to this run.")                                                                                         \
    X(areaHexes, "() -> {hex,...}", "Hex indices in the selection, ascending.")                                                                                        \
    X(areaFloorTiles, "() -> {tileIndex,...}", "Floor-tile indices in the selection, ascending.")                                                                      \
    X(areaRoofTiles, "() -> {tileIndex,...}", "Roof-tile indices in the selection, ascending.")                                                                        \
    X(areaContainsHex, "(hex) -> bool", "Is hex inside the selection?")                                                                                                \
    X(areaContainsTile, "(tileIndex) -> bool", "Is the floor tile inside the selection?")                                                                              \
    /* Deterministic seeded helpers (reproducible scatter) */                                                                                                          \
    X(rng, "() -> [0,1)", "Next draw from the seeded stream, in [0,1).")                                                                                               \
    X(rngInt, "(lo, hi) -> int", "Next seeded integer draw in [lo,hi].")                                                                                               \
    /* Coordinates (hex grid 200x200, tile grid 100x100; position = row*width + col) */                                                                                \
    X(hexIndex, "(col, row) -> hex", "Hex index from (col, row); -1 if off-grid.")                                                                                     \
    X(tileIndex, "(col, row) -> tileIndex", "Tile index from (col, row); -1 if off-grid.")                                                                             \
    X(hexCol, "(hex) -> int", "Column of a hex (or -1).")                                                                                                              \
    X(hexRow, "(hex) -> int", "Row of a hex (or -1).")                                                                                                                 \
    X(tileCol, "(tileIndex) -> int", "Column of a tile (or -1).")                                                                                                      \
    X(tileRow, "(tileIndex) -> int", "Row of a tile (or -1).")                                                                                                         \
    /* Mutators (undoable; auto-batched into one undo entry) */                                                                                                        \
    X(paintFloor, "(tileIndex, tileId) -> bool", "Paint a floor tile by index.")                                                                                       \
    X(paintRoof, "(tileIndex, tileId) -> bool", "Paint a roof tile by index.")                                                                                         \
    X(paintFloorXY, "(col, row, tileId) -> bool", "Paint a floor tile at (col, row).")                                                                                 \
    X(paintRoofXY, "(col, row, tileId) -> bool", "Paint a roof tile at (col, row).")                                                                                   \
    X(placeObject, "(proPid, frmPid, hex, dir) -> bool", "Place an object at a hex; false if off-grid or art unresolved.")                                             \
    X(placeProto, "(proPid, hex, dir) -> bool", "Place a proto (art FID resolved from the proto) at a hex.")                                                           \
    X(placeObjectXY, "(proPid, frmPid, col, row, dir) -> bool", "placeObject at a (col, row) hex.")                                                                    \
    X(placeProtoXY, "(proPid, col, row, dir) -> bool", "placeProto at a (col, row) hex.")                                                                              \
    X(placeStamp, "(name, anchorHex, variant) -> int", "Place a pre-loaded stamp (extract_pattern prefab); returns objects placed.")                                   \
    /* Map setup (spawn / exits) */                                                                                                                                    \
    X(newMap, "() -> nil", "Reset the bound map to a fresh empty Fallout 2 map (destructive, not undoable). Call first to start from a blank slate.")                  \
    X(setPlayerStart, "(hex, orientation, elevation) -> nil", "Set the player spawn in the map header (hex, orientation 0..5, elevation 0..2).")                       \
    X(placeExitGrid, "(hex, destMapId, destHex, destElevation, orientation) -> bool", "Place a map-exit grid; destMapId -2 = worldmap, -1 = town map, else a map id.") \
    X(placeExitGridRect, "(centerHex, screenHalfWidth, screenHalfHeight, destMapId, destHex, destElevation, orientation) -> int", "Place a screen-space rectangle border of exit grids around centerHex; returns markers placed.")

/// One entry of the `api:` surface — Lua name, signature, one-line description.
struct ScriptApiEntry {
    const char* name;
    const char* signature;
    const char* doc;
};

/// Every function bound on the `api` global (built from GECK_SCRIPT_API).
const std::vector<ScriptApiEntry>& scriptApiEntries();

/// The reference as Markdown: the functions plus the two non-obvious runtime behaviours (runs are
/// auto-seeded and auto-batched) and the error model. What the MCP `script_api` tool returns so an
/// agent can write a generation script without reading the C++.
std::string scriptApiReference();

} // namespace geck
