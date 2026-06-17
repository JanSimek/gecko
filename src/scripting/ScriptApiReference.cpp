#include "scripting/ScriptApiReference.h"

#include <sstream>

namespace geck {

const std::vector<ScriptApiEntry>& scriptApiEntries() {
    // Keep in sync with LuaScriptRuntime's addFunction() calls — the [scripting] drift-guard test
    // fails if a bound function is missing here (or vice versa).
    static const std::vector<ScriptApiEntry> entries = {
        // Queries (no mutation)
        { "isValidHex", "(hex) -> bool", "Is hex an on-grid hex index (0..39999)?" },
        { "hexNeighbors", "(hex) -> {hex,...}", "The up-to-6 on-grid neighbour hexes." },
        { "getFloor", "(tileIndex) -> tileId", "Floor tile id at a tile index (EMPTY_TILE if out of range)." },
        { "getRoof", "(tileIndex) -> tileId", "Roof tile id at a tile index." },
        { "getFloorXY", "(col, row) -> tileId", "Floor tile id at a tile (col, row)." },
        { "getRoofXY", "(col, row) -> tileId", "Roof tile id at a tile (col, row)." },
        { "tileId", "(name) -> int", "Resolve a ground-tile FRM name (e.g. \"edg5000\") to its tiles.lst id; -1 if unknown." },
        { "protoName", "(pid) -> string", "The proto's engine display name (e.g. \"Scrub\"); empty if unresolved." },
        { "proto", "(typeName, number) -> pid", "Build a PID from a type (\"scenery\"/\"wall\"/...) and the analyze `number`." },
        { "mapScenery", "(mapPath) -> {pid,...}", "Distinct scatter-eligible scenery PIDs a reference map uses (blockers filtered)." },
        { "mapSceneryHistogram", "(mapPath) -> {pid=count}", "Those scenery PIDs with their placement counts (real frequency)." },
        { "mapFloorTiles", "(mapPath) -> {id,...}", "A reference map's floor-tile ids, most-used first." },
        { "listMaps", "() -> {path,...}", "Every map file in the mounted data." },
        { "noise2d", "(x, y) -> [0,1]", "Coherent value noise — a density field for natural clumps/clearings." },
        // Coordinates (hex grid 200x200, tile grid 100x100; position = row*width + col)
        { "hexIndex", "(col, row) -> hex", "Hex index from (col, row); -1 if off-grid." },
        { "tileIndex", "(col, row) -> tileIndex", "Tile index from (col, row); -1 if off-grid." },
        { "hexCol", "(hex) -> int", "Column of a hex (or -1)." },
        { "hexRow", "(hex) -> int", "Row of a hex (or -1)." },
        { "tileCol", "(tileIndex) -> int", "Column of a tile (or -1)." },
        { "tileRow", "(tileIndex) -> int", "Row of a tile (or -1)." },
        // Mutators (undoable; auto-batched into one undo entry)
        { "paintFloor", "(tileIndex, tileId) -> bool", "Paint a floor tile by index." },
        { "paintRoof", "(tileIndex, tileId) -> bool", "Paint a roof tile by index." },
        { "paintFloorXY", "(col, row, tileId) -> bool", "Paint a floor tile at (col, row)." },
        { "paintRoofXY", "(col, row, tileId) -> bool", "Paint a roof tile at (col, row)." },
        { "placeObject", "(proPid, frmPid, hex, dir) -> bool", "Place an object at a hex; false if off-grid or art unresolved." },
        { "placeProto", "(proPid, hex, dir) -> bool", "Place a proto (art FID resolved from the proto) at a hex." },
        { "placeObjectXY", "(proPid, frmPid, col, row, dir) -> bool", "placeObject at a (col, row) hex." },
        { "placeProtoXY", "(proPid, col, row, dir) -> bool", "placeProto at a (col, row) hex." },
        { "placeStamp", "(name, anchorHex, variant) -> int", "Place a pre-loaded stamp (extract_pattern prefab); returns objects placed." },
    };
    return entries;
}

std::string scriptApiReference() {
    std::ostringstream out;
    out << "# Generation-script `api` reference\n\n"
        << "Functions on the global `api` inside a `generate` Luau script. Two grids, both numbered\n"
        << "`position = row * width + col`: hexes are 200x200 (objects), tiles 100x100 (floor/roof).\n\n"
        << "| Function | Signature | Description |\n|---|---|---|\n";
    for (const ScriptApiEntry& entry : scriptApiEntries()) {
        out << "| `api:" << entry.name << "` | `" << entry.signature << "` | " << entry.doc << " |\n";
    }
    out << "\n## Runtime behaviour (not obvious from the functions)\n\n"
        << "- **Auto-seeded.** Each run seeds `math.random` from `--arg seed=N`, or a fresh random seed\n"
        << "  when none is given (so a run is random unless you pin the seed). The resolved seed is in\n"
        << "  `args.seed` — print it to let a good layout be reproduced.\n"
        << "- **Auto-batched.** The whole run is collapsed into one undo entry; do **not** call\n"
        << "  begin/endBatch yourself.\n"
        << "- **Errors raise.** A genuine failure (no data mounted, a bad map path, an unknown proto\n"
        << "  type) raises a Lua error that stops the run; \"not applicable\" stays a value (e.g.\n"
        << "  `placeProto` -> false, `tileId` -> -1).\n"
        << "- **`args`** holds the `--arg key=value` parameters as strings (use `tonumber` as needed).\n";
    return out.str();
}

} // namespace geck
