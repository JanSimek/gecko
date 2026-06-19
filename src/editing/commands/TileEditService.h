#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace geck {

class Map;
class Tile;
struct TileChange;
class UndoBatcher;

/**
 * @brief Undoable floor/roof tile editing.
 *
 * One of the aggregate services ObjectCommandController delegates to. Applies
 * tile changes to the map data, refreshes the affected tile sprites on the
 * current elevation, and records edits on the shared UndoBatcher.
 */
class TileEditService {
public:
    TileEditService(std::unique_ptr<Map>& map,
        UndoBatcher& batcher,
        std::function<std::vector<Tile>&(int)> ensureElevationTiles,
        std::function<int()> getCurrentElevation,
        std::function<void(int, bool, int)> updateTileSprite);

    /// Applies the before/after state of tile edits and refreshes affected sprites.
    void applyTileChanges(const std::vector<TileChange>& changes, bool applyAfterState);
    /// Records an undoable tile edit (the change was already applied by the caller).
    void registerTileEdit(const std::string& description, const std::vector<TileChange>& changes);
    /// Applies a tile edit now (refreshing sprites) and records it for undo/redo.
    void applyTileEdit(const std::string& description, const std::vector<TileChange>& changes);

private:
    std::unique_ptr<Map>& _map;
    UndoBatcher& _batcher;
    std::function<std::vector<Tile>&(int)> _ensureElevationTiles;
    std::function<int()> _getCurrentElevation;
    std::function<void(int, bool, int)> _updateTileSprite;
};

} // namespace geck
