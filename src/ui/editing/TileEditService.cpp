#include "TileEditService.h"

#include "editor/TileChange.h"
#include "format/map/Map.h"
#include "format/map/Tile.h"
#include "ui/editing/UndoBatcher.h"
#include "util/TileUtils.h"
#include "util/UndoStack.h"

#include <cstddef>
#include <unordered_map>

namespace geck {

namespace {

    void applyChangesToTiles(std::vector<Tile>& tiles, const std::vector<const TileChange*>& changes, bool applyAfterState) {
        for (const auto* change : changes) {
            if (change->tileIndex < 0 || change->tileIndex >= static_cast<int>(tiles.size())) {
                continue;
            }
            uint16_t value = applyAfterState ? change->after : change->before;
            if (change->isRoof) {
                tiles[change->tileIndex].setRoof(value);
            } else {
                tiles[change->tileIndex].setFloor(value);
            }
        }
    }

    void refreshChangedSprites(const std::vector<const TileChange*>& changes, int elevation, std::size_t tileCount,
        const std::function<void(int, bool, int)>& updateTileSprite) {
        for (const auto* change : changes) {
            if (change->tileIndex >= 0 && change->tileIndex < static_cast<int>(tileCount)) {
                updateTileSprite(tileIndexToHexIndex(change->tileIndex), change->isRoof, elevation);
            }
        }
    }

} // namespace

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
        applyChangesToTiles(elevationTiles, elevChanges, applyAfterState);

        if (elevation == _getCurrentElevation()) {
            refreshChangedSprites(elevChanges, elevation, elevationTiles.size(), _updateTileSprite);
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
