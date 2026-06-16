#pragma once

#include <cstdint>
#include <memory>
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
    /// The distinct scenery proto PIDs used by a reference map (e.g. "maps/desert1.map") — the
    /// curated palette that map is actually built from, ready to hand to placeProto(). A PID is the
    /// engine's unique proto identifier ((type << 24) | index), so this is exact, unlike resolving
    /// by the non-unique display name. Use it to scatter the same scenery a shipped map does.
    /// Empty if the map can't be read (unknown path or no data mounted). Scenery only — walls and
    /// the MISC markers (scroll blockers / exit grids) are excluded by type, and flat scenery
    /// (invisible movement-blockers / floor markers, which carry OBJECT_FLAT) is excluded too, so
    /// only upright decorations remain and a generator never scatters a blocker.
    std::vector<int> mapScenery(const std::string& mapPath) const;
    /// The distinct floor-tile ids a reference map uses (the values paintFloor() expects), most-used
    /// first — so a generator can fill with that map's dominant ground (e.g. cave rock for a cave,
    /// sand for a desert). Empty floors are skipped; empty if the map can't be read.
    std::vector<int> mapFloorTiles(const std::string& mapPath) const;
    /// Every map file in the mounted data (VFS paths, e.g. "maps/desert1.map"), sorted. Lets a
    /// generator pick a reference map at random when none was given.
    std::vector<std::string> listMaps() const;

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
    // Parse a reference map headlessly (GL-free) for the palette queries; nullptr if unreadable.
    std::unique_ptr<Map> loadReferenceMap(const std::string& mapPath) const;

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
