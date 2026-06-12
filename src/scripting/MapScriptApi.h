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

/// The single host-API façade both scripting tiers funnel through. It is **pure C++ over
/// ObjectCommandController — no scripting runtime** — so Tier 1 (prefab stamping) and
/// headless tests use it without any Lua/Luau dependency; only Tier 2 binds it into a
/// script VM (behind GECK_ENABLE_SCRIPTING).
///
/// Every mutator routes through the controller, so edits are undoable and uniform. A
/// procedural run that touches many hexes must be wrapped in beginBatch()/endBatch() (or
/// an ObjectCommandController ScopedUndoBatch) so the whole run collapses into ONE undo
/// entry instead of one-per-hex — the UndoStack has a hard command cap.
class MapScriptApi {
public:
    /// Binds to a live editing session at `elevation`. References are borrowed and must
    /// outlive the api.
    MapScriptApi(resource::GameResources& resources,
        const HexagonGrid& hexgrid,
        ObjectCommandController& controller,
        Map& map,
        int elevation);

    // --- Queries (no mutation) ---------------------------------------------------
    bool isValidHex(int hex) const;
    /// The up-to-6 on-grid hex neighbours (cube-coordinate, parity-correct).
    std::vector<int> hexNeighbors(int hex) const;
    /// Floor/roof tile id at `tileIndex` on this elevation, or EMPTY_TILE if out of range.
    uint16_t getFloor(int tileIndex) const;
    uint16_t getRoof(int tileIndex) const;

    // --- Undo batching -----------------------------------------------------------
    void beginBatch(const std::string& description);
    void endBatch();

    // --- Mutators (undoable; batch to collapse into one history entry) -----------
    /// Build and place an object at `hex`. Returns false if `hex` is off-grid or the
    /// object's art (`frmPid`) cannot be resolved/loaded (no visual to place).
    bool placeObject(uint32_t proPid, uint32_t frmPid, int hex, uint32_t direction);
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
    int _placedObjects = 0;
    int _paintedTiles = 0;
};

} // namespace geck
