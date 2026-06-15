#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace geck {

class HexagonGrid;
class Map;
class ObjectCommandController;
namespace resource {
    class GameResources;
}

/// Host API for editing the map: queries plus undoable mutators that route through
/// ObjectCommandController. Pure C++ with no scripting-runtime dependency, so it is usable
/// (and unit-testable) without Lua.
///
/// A run that touches many hexes must be wrapped in beginBatch()/endBatch() (or a
/// ScopedUndoBatch) so it collapses into ONE undo entry instead of one-per-hex — the
/// UndoStack has a hard command cap.
class MapScriptApi {
public:
    /// Binds to a live editing session at `elevation`. References are borrowed and must
    /// outlive the api.
    ///
    /// `buildSprites` selects how placed objects are recorded. The GUI leaves it true so each
    /// object also gets an SFML sprite for rendering (needs a GL context). Headless callers
    /// (gecko-cli, CI) pass false: objects are recorded as map data only — no sprite, no GL —
    /// which is all the .map format stores anyway.
    MapScriptApi(resource::GameResources& resources,
        const HexagonGrid& hexgrid,
        ObjectCommandController& controller,
        Map& map,
        int elevation,
        bool buildSprites = true);

    // --- Queries (no mutation) ---------------------------------------------------
    bool isValidHex(int hex) const;
    /// The up-to-6 on-grid hex neighbours (cube-coordinate, parity-correct).
    std::vector<int> hexNeighbors(int hex) const;
    /// Floor/roof tile id at `tileIndex` on this elevation, or EMPTY_TILE if out of range.
    uint16_t getFloor(int tileIndex) const;
    uint16_t getRoof(int tileIndex) const;
    /// Resolve a ground-tile FRM name (e.g. "edg5000" or "edg5000.frm", case-insensitive) to
    /// its index in art/tiles/tiles.lst — the value paintFloor()/paintRoof() expect. Returns -1
    /// if the tile list is unavailable or the name is unknown, so scripts address tiles by name
    /// instead of magic numbers.
    int tileId(const std::string& name) const;

    // --- Undo batching -----------------------------------------------------------
    void beginBatch(const std::string& description);
    void endBatch();

    // --- Mutators (undoable; batch to collapse into one history entry) -----------
    /// Build and place an object at `hex`. Returns false if `hex` is off-grid or the
    /// object's art (`frmPid`) cannot be resolved/loaded (no visual to place).
    bool placeObject(uint32_t proPid, uint32_t frmPid, int hex, uint32_t direction);
    /// Place a proto by PID alone, resolving its art FID from the proto header — the common
    /// case, so scripts need not also know the FRM id. Same return contract as placeObject();
    /// also returns false if the proto cannot be loaded.
    bool placeProto(uint32_t proPid, int hex, uint32_t direction);
    bool paintFloor(int tileIndex, uint16_t tileId);
    bool paintRoof(int tileIndex, uint16_t tileId);

    int placedObjects() const { return _placedObjects; }
    int paintedTiles() const { return _paintedTiles; }

private:
    bool paintTile(int tileIndex, uint16_t tileId, bool isRoof);

    resource::GameResources& _resources;
    const HexagonGrid& _hexgrid;
    ObjectCommandController& _controller;
    Map& _map;
    int _elevation;
    bool _buildSprites;
    int _placedObjects = 0;
    int _paintedTiles = 0;
};

} // namespace geck
