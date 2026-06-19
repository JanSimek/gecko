#include "TileEditService.h"

#include "editor/TileChange.h"
#include "format/map/Map.h"
#include "format/map/Tile.h"
#include "ui/editing/UndoBatcher.h"
#include "util/TileUtils.h"
#include "util/UndoStack.h"

#include <unordered_map>

namespace geck {

TileEditService::TileEditService(std::unique_ptr<Map>& map,
    UndoBatcher& batcher,
    std::function<std::vector<Tile>&(int)> ensureElevationTiles,
    std::function<int()> getCurrentElevation,
    std::function<void(int, bool, int)> updateTileSprite)
    : _map(map)
    , _batcher(batcher)
    , _ensureElevationTiles(std::move(ensureElevationTiles))
    , _getCurrentElevation(std::move(getCurrentElevation))
    , _updateTileSprite(std::move(updateTileSprite)) {
}

void TileEditService::applyTileChanges(const std::vector<TileChange>& changes, bool applyAfterState) {
    if (!_map) {
        return;
    }

    std::unordered_map<int, std::vector<const TileChange*>> changesByElevation;
    for (const auto& change : changes) {
        changesByElevation[change.elevation].push_back(&change);
    }

    // Apply all changes first, then update sprites in batch.
    for (const auto& [elevation, elevChanges] : changesByElevation) {
        auto& elevationTiles = _ensureElevationTiles(elevation);

        for (const auto* change : elevChanges) {
            if (change->tileIndex < 0 || change->tileIndex >= static_cast<int>(elevationTiles.size())) {
                continue;
            }

            uint16_t value = applyAfterState ? change->after : change->before;
            if (change->isRoof) {
                elevationTiles[change->tileIndex].setRoof(value);
            } else {
                elevationTiles[change->tileIndex].setFloor(value);
            }
        }

        if (elevation == _getCurrentElevation()) {
            for (const auto* change : elevChanges) {
                if (change->tileIndex >= 0 && change->tileIndex < static_cast<int>(elevationTiles.size())) {
                    int hexIndex = tileIndexToHexIndex(change->tileIndex);
                    _updateTileSprite(hexIndex, change->isRoof, elevation);
                }
            }
        }
    }
}

void TileEditService::registerTileEdit(const std::string& description, const std::vector<TileChange>& changes) {
    if (changes.empty()) {
        return;
    }

    UndoCommand cmd;
    cmd.description = description;
    cmd.undo = [this, changes]() {
        applyTileChanges(changes, false);
    };
    cmd.redo = [this, changes]() {
        applyTileChanges(changes, true);
    };
    _batcher.push(std::move(cmd));
}

void TileEditService::applyTileEdit(const std::string& description, const std::vector<TileChange>& changes) {
    if (changes.empty()) {
        return;
    }
    applyTileChanges(changes, true); // apply the move now and refresh the affected tile sprites
    registerTileEdit(description, changes);
}

} // namespace geck
