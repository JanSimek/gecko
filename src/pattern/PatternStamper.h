#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "pattern/Pattern.h"

namespace geck {
class HexagonGrid;
class Map;
class Object;
struct MapObject;
class ObjectCommandController;
namespace resource {
    class GameResources;
}
} // namespace geck

namespace geck::pattern {

struct FillPlan;

/// Places a pattern variant onto the map. Resolution (where each entry lands when the
/// variant's anchor is dropped on a target hex) is a pure, dependency-free static step;
/// building the objects/tiles (planInto) is non-mutating; committing (stamp) routes the
/// built plan through ObjectCommandController as a single undo entry via PlacementBatch.
/// Entries are placed verbatim (no rotation) — orientation is chosen by selecting a
/// variant, since Fallout 2 object art is direction-specific.
class PatternStamper {
public:
    struct ObjectPlacement {
        int hex = 0;
        uint32_t proPid = 0;
        uint32_t frmPid = 0;
        uint32_t direction = 0;
        uint32_t flags = 0;
    };
    struct TilePlacement {
        int tileIndex = 0;
        bool isRoof = false;
        uint16_t tileId = 0;
    };
    struct Plan {
        std::vector<ObjectPlacement> objects;
        std::vector<TilePlacement> tiles;
        int objectsDropped = 0; ///< Objects whose target hex falls off the grid.
        int tilesDropped = 0;   ///< Tiles whose target falls off the tile grid.
    };
    struct Result {
        int objectsPlaced = 0;
        int objectsFailed = 0; ///< Resolved on-grid, but their art could not be loaded.
        int tilesPainted = 0;
        int dropped = 0; ///< Objects/tiles whose target fell off the grid.
        bool success = false;
    };

    /// Resolve where each entry of `variant` lands when its anchor is stamped near
    /// `targetHex`. Objects place at hex precision but tiles only at tile (2-hex)
    /// precision, so `targetHex` is first **snapped to the anchor's column/row parity**
    /// (placement is therefore tile-granular — the actual anchor may be up to one hex
    /// from `targetHex`); this keeps floor/roof tiles locked to the objects. Object
    /// offsets then translate in cube space, tile offsets in tile space; off-grid entries
    /// are dropped and counted. Pure.
    static Plan plan(const PatternVariant& variant, int targetHex);

    /// `buildSprites` mirrors MapScriptApi: the GUI leaves it true so each placed object also gets an
    /// SFML sprite (needs resolvable art + a GL context). Headless callers (gecko-cli / the MCP)
    /// pass false — objects are recorded as map data only, so a stamp's objects land in the .map even
    /// with no art or GL (the .map stores only the ids the engine resolves on load).
    PatternStamper(resource::GameResources& resources,
        const HexagonGrid& hexgrid,
        ObjectCommandController& controller,
        Map& map,
        bool buildSprites = true);

    /// Apply `variant` near `targetHex` on `elevation` as one undo entry. `targetHex` is
    /// snapped to the anchor's parity (see plan()), so placement is tile-granular.
    Result stamp(const PatternVariant& variant, int targetHex, int elevation);

    /// Resolve and BUILD `variant` near `targetHex` on `elevation` into `out` without committing:
    /// each resolved object becomes a MapObject (and, when buildSprites is true, its visual Object)
    /// and each tile a TileChange (before captured from the current map), appended to the plan.
    /// Mutates nothing — PlacementBatch::replay(out) commits it. This is the capture path the
    /// fill plan-sink uses so a stamp placed inside a fill is previewed, not applied. Appends, so a
    /// caller can build one plan from several stamps; counts off-grid drops into `out.dropped`.
    void planInto(FillPlan& out, const PatternVariant& variant, int targetHex, int elevation) const;

private:
    std::shared_ptr<Object> buildObject(const std::shared_ptr<MapObject>& mapObject, uint32_t frmPid) const;
    // Build the MapObject for a resolved placement (position/flags/ids verbatim) on `elevation`.
    std::shared_ptr<MapObject> makeMapObject(const ObjectPlacement& placement, int elevation) const;
    // Resolve the plan's tiles into TileChanges (before-value read from the current map) and append
    // them to `out`; builds nothing on the map (PlacementBatch applies them later).
    void appendTiles(FillPlan& out, const std::vector<TilePlacement>& tiles, int elevation) const;

    resource::GameResources& _resources;
    const HexagonGrid& _hexgrid;
    ObjectCommandController& _controller;
    Map& _map;
    bool _buildSprites;
};

} // namespace geck::pattern
