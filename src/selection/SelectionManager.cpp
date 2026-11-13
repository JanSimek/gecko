#include "SelectionManager.h"
#include "SelectionDataProvider.h"
#include "viewport/ViewportController.h"
#include "format/map/MapObject.h"
#include "util/Constants.h"
#include "util/TileUtils.h"
#include "editor/HexagonGrid.h"
#include "editor/HexGeometry.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <spdlog/spdlog.h>

namespace geck::selection {

SelectionManager::SelectionManager(SelectionDataProvider& provider)
    : _provider(provider) {
}

SelectionResult SelectionManager::selectAtPosition(sf::Vector2f worldPos, SelectionMode mode) {
    switch (mode) {
        case SelectionMode::ALL:
            return cycleThroughItemsAtPosition(worldPos);

        case SelectionMode::OBJECTS:
        case SelectionMode::FLOOR_TILES:
        case SelectionMode::ROOF_TILES:
        case SelectionMode::ROOF_TILES_ALL:
        case SelectionMode::HEXES:
            return selectSingleAtPosition(worldPos, mode);

        default:
            return SelectionResult::createError("Invalid selection mode");
    }
}

void SelectionManager::appendTilesInArea(std::vector<SelectedItem>& items, const sf::FloatRect& area, bool roof, bool includeEmpty) const {
    const auto tiles = includeEmpty ? getTilesInAreaIncludingEmpty(area, roof)
                                    : getTilesInArea(area, roof);
    const SelectionType type = roof ? SelectionType::ROOF_TILE : SelectionType::FLOOR_TILE;
    for (int tileIndex : tiles) {
        items.emplace_back(type, tileIndex);
    }
}

void SelectionManager::appendObjectsInArea(std::vector<SelectedItem>& items, const sf::FloatRect& area) const {
    for (auto& object : getObjectsInArea(area)) {
        items.emplace_back(SelectionType::OBJECT, object);
    }
}

void SelectionManager::appendHexesInArea(std::vector<SelectedItem>& items, const sf::FloatRect& area) const {
    for (int hexIndex : getHexesInArea(area)) {
        items.emplace_back(SelectionType::HEX, hexIndex);
    }
}

std::vector<SelectedItem> SelectionManager::collectItemsInArea(const sf::FloatRect& area, SelectionMode mode) const {
    std::vector<SelectedItem> items;

    switch (mode) {
        case SelectionMode::FLOOR_TILES:
            appendTilesInArea(items, area, false, false);
            break;

        case SelectionMode::ROOF_TILES:
            appendTilesInArea(items, area, true, false);
            break;

        case SelectionMode::ROOF_TILES_ALL:
            appendTilesInArea(items, area, true, true);
            break;

        case SelectionMode::OBJECTS:
            appendObjectsInArea(items, area);
            break;

        case SelectionMode::ALL:
            // Mixed mode selects the user-enabled layers (the toolbar's combinable checkboxes).
            if (_layers.objects) {
                appendObjectsInArea(items, area);
            }
            if (_layers.floorTiles) {
                appendTilesInArea(items, area, false, false);
            }
            // Roofs: only when the layer is enabled AND you can see them. The explicit ROOF_TILES
            // modes above still select roofs regardless of the layer toggle / visibility.
            if (_layers.roofTiles && _provider.isRoofVisible()) {
                appendTilesInArea(items, area, true, false);
            }
            break;

        case SelectionMode::HEXES:
            appendHexesInArea(items, area);
            break;

        default:
            break;
    }

    return items;
}

SelectionResult SelectionManager::selectArea(const sf::FloatRect& area, SelectionMode mode) {
    clearSelection();
    for (const auto& item : collectItemsInArea(area, mode)) {
        addItemToSelection(item);
    }
    notifySelectionChanged();
    return SelectionResult::createSuccess("");
}

SelectionResult SelectionManager::addArea(const sf::FloatRect& area, SelectionMode mode) {
    // Alt+drag: add the covered items to the current selection without clearing it.
    // addItemToSelection ignores items that are already selected, so overlaps are harmless.
    for (const auto& item : collectItemsInArea(area, mode)) {
        addItemToSelection(item);
    }
    notifySelectionChanged();
    return SelectionResult::createSuccess("");
}

std::vector<SelectedItem> SelectionManager::itemsToDeselectInArea(const sf::FloatRect& area, SelectionMode mode) const {
    // What a Ctrl+drag would remove: selected and on a visible layer. A roof you cannot see must never
    // be silently deselected; hidden objects are already excluded upstream.
    const bool roofVisible = _provider.isRoofVisible();
    std::vector<SelectedItem> toRemove;
    for (const auto& item : collectItemsInArea(area, mode)) {
        if (item.type == SelectionType::ROOF_TILE && !roofVisible) {
            continue;
        }
        if (isItemSelected(item)) {
            toRemove.push_back(item);
        }
    }
    return toRemove;
}

SelectionResult SelectionManager::deselectArea(const sf::FloatRect& area, SelectionMode mode) {
    // Ctrl+drag: remove the covered items that are already selected; never add.
    for (const auto& item : itemsToDeselectInArea(area, mode)) {
        removeItemFromSelection(item);
    }
    notifySelectionChanged();
    return SelectionResult::createSuccess("");
}

SelectionResult SelectionManager::addToSelection(sf::Vector2f worldPos, SelectionMode mode) {
    switch (mode) {
        case SelectionMode::FLOOR_TILES: {
            auto tileIndex = getFloorTileAtPosition(worldPos);
            if (tileIndex) {
                SelectedItem item{ SelectionType::FLOOR_TILE, tileIndex.value() };
                addItemToSelection(item);
                notifySelectionChanged();
                return SelectionResult::createSuccess("");
            }
            break;
        }

        case SelectionMode::ROOF_TILES: {
            auto tileIndex = getRoofTileAtPosition(worldPos);
            if (tileIndex) {
                SelectedItem item{ SelectionType::ROOF_TILE, tileIndex.value() };
                addItemToSelection(item);
                notifySelectionChanged();
                return SelectionResult::createSuccess("");
            }
            break;
        }

        case SelectionMode::ROOF_TILES_ALL: {
            auto tileIndex = getRoofTileAtPositionIncludingEmpty(worldPos);
            if (tileIndex) {
                SelectedItem item{ SelectionType::ROOF_TILE, tileIndex.value() };
                addItemToSelection(item);
                notifySelectionChanged();
                return SelectionResult::createSuccess("");
            }
            break;
        }

        case SelectionMode::OBJECTS: {
            auto objects = getObjectsAtPosition(worldPos);
            if (!objects.empty()) {
                SelectedItem item{ SelectionType::OBJECT, objects[0] };
                addItemToSelection(item);
                notifySelectionChanged();
                return SelectionResult::createSuccess();
            }
            break;
        }

        case SelectionMode::ALL:
            // ALL mode adds the first item in priority order without cycling, restricted to enabled layers. A
            // hidden roof is skipped so it cannot shadow the object beneath.
            if (_layers.roofTiles && _provider.isRoofVisible()) {
                auto tileIndex = getRoofTileAtPosition(worldPos);
                if (tileIndex) {
                    SelectedItem item{ SelectionType::ROOF_TILE, tileIndex.value() };
                    addItemToSelection(item);
                    notifySelectionChanged();
                    return SelectionResult::createSuccess("");
                }
            }
            if (_layers.objects) {
                auto objects = getObjectsAtPosition(worldPos);
                if (!objects.empty()) {
                    SelectedItem item{ SelectionType::OBJECT, objects[0] };
                    addItemToSelection(item);
                    notifySelectionChanged();
                    return SelectionResult::createSuccess("");
                }
            }
            if (_layers.floorTiles) {
                auto tileIndex = getFloorTileAtPosition(worldPos);
                if (tileIndex) {
                    SelectedItem item{ SelectionType::FLOOR_TILE, tileIndex.value() };
                    addItemToSelection(item);
                    notifySelectionChanged();
                    return SelectionResult::createSuccess();
                }
            }
            break;

        default:
            return SelectionResult::createError("Invalid selection mode");
    }

    return SelectionResult::createNoChange();
}

SelectionManager::PickResult SelectionManager::pickAt(sf::Vector2f worldPos) const {
    // Same priority as an ALL-mode click (roof-when-shown → object → floor), but read-only: it never
    // mutates the selection. A hidden roof is skipped so it can't shadow the object beneath it.
    PickResult result;
    if (_provider.isRoofVisible()) {
        if (auto roof = getRoofTileAtPosition(worldPos)) {
            result.roofTile = roof;
            return result;
        }
    }
    if (auto objects = getObjectsAtPosition(worldPos); !objects.empty()) {
        result.object = objects.front(); // objects[0] is the topmost, as click-selection uses
        return result;
    }
    if (auto floor = getFloorTileAtPosition(worldPos)) {
        result.floorTile = floor;
    }
    return result;
}

namespace {
    // Appends a tile candidate when the hit-test found one at the position.
    void appendTileCandidate(std::vector<SelectedItem>& candidates, std::optional<int> tileIndex, SelectionType type) {
        if (tileIndex) {
            candidates.emplace_back(type, tileIndex.value());
        }
    }
} // namespace

void SelectionManager::appendRoofCandidate(std::vector<SelectedItem>& candidates, std::optional<int> tileIndex) const {
    // A hidden roof must not be deselectable, so it is never offered as a candidate.
    if (_provider.isRoofVisible()) {
        appendTileCandidate(candidates, tileIndex, SelectionType::ROOF_TILE);
    }
}

void SelectionManager::appendObjectCandidates(std::vector<SelectedItem>& candidates, sf::Vector2f worldPos) const {
    for (auto& object : getObjectsAtPosition(worldPos)) {
        candidates.emplace_back(SelectionType::OBJECT, object);
    }
}

void SelectionManager::appendHexCandidate(std::vector<SelectedItem>& candidates, sf::Vector2f worldPos) const {
    auto* viewport = _provider.getViewportController();
    const auto* hexGrid = _provider.getHexagonGrid();
    if (viewport && hexGrid) {
        int hexIndex = viewport->worldPosToHexIndex(worldPos);
        if (hexgrid::isValidPosition(hexIndex)) {
            candidates.emplace_back(SelectionType::HEX, hexIndex);
        }
    }
}

std::vector<SelectedItem> SelectionManager::collectDeselectableAtPosition(sf::Vector2f worldPos, SelectionMode mode) const {
    std::vector<SelectedItem> candidates;

    switch (mode) {
        case SelectionMode::FLOOR_TILES:
            appendTileCandidate(candidates, getFloorTileAtPosition(worldPos), SelectionType::FLOOR_TILE);
            break;

        case SelectionMode::ROOF_TILES:
            appendRoofCandidate(candidates, getRoofTileAtPosition(worldPos));
            break;

        case SelectionMode::ROOF_TILES_ALL:
            appendRoofCandidate(candidates, getRoofTileAtPositionIncludingEmpty(worldPos));
            break;

        case SelectionMode::OBJECTS:
            appendObjectCandidates(candidates, worldPos);
            break;

        case SelectionMode::ALL:
            // Priority order matches single-click selection: roof -> objects -> floor. Only the
            // user-enabled layers are candidates (a disabled layer is treated as absent).
            if (_layers.roofTiles) {
                appendRoofCandidate(candidates, getRoofTileAtPosition(worldPos));
            }
            if (_layers.objects) {
                appendObjectCandidates(candidates, worldPos);
            }
            if (_layers.floorTiles) {
                appendTileCandidate(candidates, getFloorTileAtPosition(worldPos), SelectionType::FLOOR_TILE);
            }
            break;

        case SelectionMode::HEXES:
            appendHexCandidate(candidates, worldPos);
            break;

        default:
            break;
    }

    return candidates;
}

SelectionResult SelectionManager::deselectAtPosition(sf::Vector2f worldPos, SelectionMode mode) {
    // Ctrl+click is deselect-only: remove the topmost visible selected layer, never add, and leave the
    // selection alone when clicking outside it.
    for (const auto& candidate : collectDeselectableAtPosition(worldPos, mode)) {
        if (isItemSelected(candidate)) {
            removeItemFromSelection(candidate);
            notifySelectionChanged();
            return SelectionResult::createSuccess("Item removed from selection");
        }
    }

    return SelectionResult::createNoChange();
}

bool SelectionManager::isPointOnSelection(sf::Vector2f worldPos) const {
    // The same candidates Ctrl+click deselect considers; if any is selected, the point is a valid grab
    // handle for moving the selection.
    const auto candidates = collectDeselectableAtPosition(worldPos, SelectionMode::ALL);
    return std::ranges::any_of(candidates, [this](const SelectedItem& candidate) { return isItemSelected(candidate); });
}

void SelectionManager::clearSelection() {
    bool hadSelection = !_state.isEmpty();
    _state.clear();

    if (hadSelection) {
        notifySelectionChanged();
    }
}

void SelectionManager::setSelectedItems(std::vector<SelectedItem> items) {
    _state.items = std::move(items);
    notifySelectionChanged();
}

void SelectionManager::selectAll(SelectionMode mode, int currentElevation) {
    clearSelection();

    switch (mode) {
        case SelectionMode::FLOOR_TILES:
            for (int i = 0; i < static_cast<int>(Map::TILES_PER_ELEVATION); ++i) {
                SelectedItem item{ SelectionType::FLOOR_TILE, i };
                addItemToSelection(item);
            }
            break;

        case SelectionMode::ROOF_TILES:
            for (int i = 0; i < static_cast<int>(Map::TILES_PER_ELEVATION); ++i) {
                auto tile = _provider.getMapFile().tiles.at(currentElevation).at(i);
                if (tile.getRoof() != Map::EMPTY_TILE) {
                    SelectedItem item{ SelectionType::ROOF_TILE, i };
                    addItemToSelection(item);
                }
            }
            break;

        case SelectionMode::ROOF_TILES_ALL:
            for (int i = 0; i < static_cast<int>(Map::TILES_PER_ELEVATION); ++i) {
                // Include all roof tile positions, regardless of whether they have textures
                SelectedItem item{ SelectionType::ROOF_TILE, i };
                addItemToSelection(item);
            }
            break;

        case SelectionMode::OBJECTS:
            for (auto& object : _provider.getObjects()) {
                SelectedItem item{ SelectionType::OBJECT, object };
                addItemToSelection(item);
            }
            break;

        case SelectionMode::HEXES:
            if (_provider.getHexagonGrid() != nullptr) {
                for (int position = 0; position < HexagonGrid::POSITION_COUNT; ++position) {
                    addItemToSelection(SelectedItem{ SelectionType::HEX, position });
                }
            }
            break;

        case SelectionMode::SCROLL_BLOCKER_RECTANGLE:
            // Rectangle mode is a drawing tool, not a persistent selection set.
            spdlog::debug("SelectionManager::selectAll ignored in scroll blocker rectangle mode");
            break;

        case SelectionMode::ALL:
            // Inlined rather than recursing to avoid the clearSelection() conflicts that nested
            // calls would cause. Select-all honours the user-enabled layers.
            if (_layers.floorTiles) {
                for (int i = 0; i < static_cast<int>(Map::TILES_PER_ELEVATION); ++i) {
                    SelectedItem item{ SelectionType::FLOOR_TILE, i };
                    addItemToSelection(item);
                }
            }

            // Roof tiles: only those with textures
            if (_layers.roofTiles) {
                for (int i = 0; i < static_cast<int>(Map::TILES_PER_ELEVATION); ++i) {
                    auto tile = _provider.getMapFile().tiles.at(currentElevation).at(i);
                    if (tile.getRoof() != Map::EMPTY_TILE) {
                        SelectedItem item{ SelectionType::ROOF_TILE, i };
                        addItemToSelection(item);
                    }
                }
            }

            if (_layers.objects) {
                for (auto& object : _provider.getObjects()) {
                    SelectedItem item{ SelectionType::OBJECT, object };
                    addItemToSelection(item);
                }
            }
            break;

        case SelectionMode::NUM_SELECTION_TYPES:
            // This should not happen - it's just an enum count
            break;
    }

    notifySelectionChanged();
}

std::vector<std::shared_ptr<Object>> SelectionManager::getObjectsAtPosition(sf::Vector2f worldPos) const {
    return _provider.getObjectsAtPosition(worldPos);
}

std::optional<int> SelectionManager::getRoofTileAtPosition(sf::Vector2f worldPos) const {
    return _provider.getTileAtPosition(worldPos, true); // true for roof
}

std::optional<int> SelectionManager::getRoofTileAtPositionIncludingEmpty(sf::Vector2f worldPos) const {
    return _provider.getRoofTileAtPositionIncludingEmpty(worldPos);
}

std::optional<int> SelectionManager::getFloorTileAtPosition(sf::Vector2f worldPos) const {
    return _provider.getTileAtPosition(worldPos, false); // false for floor
}

std::vector<int> SelectionManager::getTilesInArea(const sf::FloatRect& area, bool roof) const {
    auto spatialResult = _spatialIndex.getTilesInArea(area, roof);
    if (!roof) {
        return spatialResult; // floor tiles always have content
    }

    // The spatial index doesn't track empty tiles, so filter roof results by tile content.
    std::vector<int> result;
    result.reserve(spatialResult.size());
    const auto& elevationTiles = _provider.getMapFile().tiles.at(_provider.getCurrentElevation());
    for (int tileIndex : spatialResult) {
        if (elevationTiles.at(tileIndex).getRoof() != Map::EMPTY_TILE) {
            result.push_back(tileIndex);
        }
    }
    return result;
}

std::vector<int> SelectionManager::getTilesInAreaIncludingEmpty(const sf::FloatRect& area, bool roof) const {
    // Include all tiles intersecting the area, even if currently empty.
    // We intentionally skip the spatial index here because it only tracks placed tiles.
    std::vector<int> result;
    result.reserve(1000);

    const auto& floorSprites = _provider.getFloorSprites();
    const auto& roofSprites = _provider.getRoofSprites();

    for (int i = 0; i < static_cast<int>(Map::TILES_PER_ELEVATION); ++i) {
        sf::FloatRect tileBounds = roof ? roofSprites.at(i).getGlobalBounds() : floorSprites.at(i).getGlobalBounds();

        // Empty tiles have no sprite bounds, so synthesize bounds from the tile index using the same positioning as sprites.
        if (tileBounds.size.x == 0 || tileBounds.size.y == 0) {
            auto screenPos = indexToScreenPosition(i, roof);
            tileBounds = sf::FloatRect(
                sf::Vector2f(static_cast<float>(screenPos.x), static_cast<float>(screenPos.y)),
                sf::Vector2f(static_cast<float>(TILE_WIDTH), static_cast<float>(TILE_HEIGHT)));
        }

        if (area.findIntersection(tileBounds)) {
            result.push_back(i); // Include all tiles, regardless of content
        }
    }

    return result;
}

std::vector<std::shared_ptr<Object>> SelectionManager::getObjectsInArea(const sf::FloatRect& area) const {
    // TileSpatialIndex only handles tiles, so objects use a linear search.
    std::vector<std::shared_ptr<Object>> result;
    result.reserve(100);

    const auto& allObjects = _provider.getObjects();

    for (const auto& object : allObjects) {
        // Only visible objects are selectable, matching the point-pick rule - otherwise an area drag
        // selects objects on a hidden layer the user cannot see.
        if (!_provider.isObjectSelectable(object)) {
            continue;
        }

        const auto& sprite = object->getSprite();
        sf::FloatRect objectBounds = sprite.getGlobalBounds();

        // Bounds intersection is much faster than full hit detection.
        if (area.findIntersection(objectBounds)) {
            result.push_back(object);
        }
    }

    return result;
}

SelectionResult SelectionManager::selectSingleAtPosition(sf::Vector2f worldPos, SelectionMode mode) {
    clearSelection();

    switch (mode) {
        case SelectionMode::FLOOR_TILES: {
            auto tileIndex = getFloorTileAtPosition(worldPos);
            if (tileIndex) {
                SelectedItem item{ SelectionType::FLOOR_TILE, tileIndex.value() };
                addItemToSelection(item);
                notifySelectionChanged();
                return SelectionResult::createSuccess();
            }
            break;
        }

        case SelectionMode::ROOF_TILES: {
            auto tileIndex = getRoofTileAtPosition(worldPos);
            if (tileIndex) {
                SelectedItem item{ SelectionType::ROOF_TILE, tileIndex.value() };
                addItemToSelection(item);
                notifySelectionChanged();
                return SelectionResult::createSuccess();
            }
            break;
        }

        case SelectionMode::ROOF_TILES_ALL: {
            auto tileIndex = getRoofTileAtPositionIncludingEmpty(worldPos);
            if (tileIndex) {
                SelectedItem item{ SelectionType::ROOF_TILE, tileIndex.value() };
                addItemToSelection(item);
                notifySelectionChanged();
                return SelectionResult::createSuccess();
            }
            break;
        }

        case SelectionMode::OBJECTS: {
            auto objects = getObjectsAtPosition(worldPos);
            if (!objects.empty()) {
                SelectedItem item{ SelectionType::OBJECT, objects[0] }; // Select first (topmost)
                addItemToSelection(item);
                notifySelectionChanged();
                return SelectionResult::createSuccess();
            }
            break;
        }

        case SelectionMode::HEXES: {
            int hexIndex = _provider.getViewportController()->worldPosToHexIndex(worldPos);
            const auto* hexGrid = _provider.getHexagonGrid();
            if (hexGrid && hexgrid::isValidPosition(hexIndex)) {
                SelectedItem item{ SelectionType::HEX, hexIndex };
                addItemToSelection(item);
                notifySelectionChanged();
                return SelectionResult::createSuccess();
            }
            break;
        }

        default:
            return SelectionResult::createError("Invalid selection mode for single selection");
    }

    return SelectionResult::createNoChange();
}

SelectionResult SelectionManager::cycleThroughItemsAtPosition(sf::Vector2f worldPos) {
    // A disabled layer is treated as absent here, so the roof -> object -> floor cycle skips it
    // and only walks the user-enabled layers.
    auto objectsAtPos = _layers.objects ? getObjectsAtPosition(worldPos)
                                        : std::vector<std::shared_ptr<Object>>{};
    // A roof you cannot see must never shadow a click beneath it. Gating only on the layer flag meant
    // hiding the roof still selected its tile; this mirrors the area-select path.
    auto roofTileIndex = (_layers.roofTiles && _provider.isRoofVisible())
        ? getRoofTileAtPosition(worldPos)
        : std::nullopt;
    auto floorTileIndex = _layers.floorTiles ? getFloorTileAtPosition(worldPos) : std::nullopt;

    // Cycle order: roof -> objects (all) -> floor -> deselect. The current position is the first
    // selected candidate in that order; the click moves to the next one.
    std::vector<SelectedItem> candidates;
    if (roofTileIndex) {
        candidates.push_back({ SelectionType::ROOF_TILE, roofTileIndex.value() });
    }
    for (const auto& object : objectsAtPos) {
        candidates.push_back({ SelectionType::OBJECT, object });
    }
    if (floorTileIndex) {
        candidates.push_back({ SelectionType::FLOOR_TILE, floorTileIndex.value() });
    }

    auto isSelected = [this](const SelectedItem& candidate) {
        return std::find(_state.items.begin(), _state.items.end(), candidate) != _state.items.end();
    };
    const auto current = std::find_if(candidates.begin(), candidates.end(), isSelected);
    const auto next = current == candidates.end() ? candidates.begin() : std::next(current);

    clearSelection();
    if (next == candidates.end()) {
        return SelectionResult::createSuccess();
    }
    addItemToSelection(*next);
    notifySelectionChanged();
    return SelectionResult::createSuccess();
}

void SelectionManager::notifySelectionChanged() {
    if (_selectionCallback) {
        _selectionCallback(_state);
    }
}

void SelectionManager::addItemToSelection(const SelectedItem& item) {
    _state.addItem(item);
}

void SelectionManager::removeItemFromSelection(const SelectedItem& item) {
    _state.removeItem(item);
}

bool SelectionManager::isItemSelected(const SelectedItem& item) const {
    return _state.hasItem(item);
}

bool SelectionManager::appendTileLayerMove(std::vector<TileChange>& out, bool roof, int deltaRow, int deltaColumn) const {
    const int elevation = _provider.getCurrentElevation();
    const auto& tiles = _provider.getMapFile().tiles.at(elevation);
    const SelectionType layer = roof ? SelectionType::ROOF_TILE : SelectionType::FLOOR_TILE;
    const auto valueAt = [&](int index) { return roof ? tiles.at(index).getRoof() : tiles.at(index).getFloor(); };

    // Map each selected source tile of this layer to its target; reject if any leaves the map.
    std::vector<std::pair<int, int>> moves; // (source, target)
    for (const auto& item : _state.items) {
        if (item.type != layer) {
            continue;
        }
        const int source = item.getTileIndex();
        const auto coords = indexToCoordinates(source);
        const int newRow = static_cast<int>(coords.x) + deltaRow;
        const int newColumn = static_cast<int>(coords.y) + deltaColumn;
        if (!isTileRowColInGrid(newRow, newColumn)) {
            return false; // moving the block off the map is rejected as a whole
        }
        moves.emplace_back(source,
            coordinatesToIndex(TileCoordinates(static_cast<unsigned int>(newRow), static_cast<unsigned int>(newColumn))));
    }

    // Block-safe: vacate every source, then fill every target. A tile that is both a source and a
    // target keeps the moved-in value, because the target write runs after the vacate.
    std::unordered_map<int, uint16_t> after;
    for (const auto& [source, target] : moves) {
        after[source] = Map::EMPTY_TILE;
    }
    for (const auto& [source, target] : moves) {
        after[target] = valueAt(source);
    }

    for (const auto& [tileIndex, newValue] : after) {
        const uint16_t before = valueAt(tileIndex);
        if (before != newValue) {
            out.push_back({ elevation, tileIndex, roof, before, newValue });
        }
    }
    return true;
}

std::vector<TileChange> SelectionManager::planSelectionTileMove(int deltaRow, int deltaColumn) const {
    std::vector<TileChange> result;
    if (deltaRow == 0 && deltaColumn == 0) {
        return result; // no movement
    }
    // Floor and roof move with identical semantics (EMPTY_TILE is the empty value for both). If
    // either layer would leave the map, reject the whole move so the region stays consistent.
    if (!appendTileLayerMove(result, /*roof*/ false, deltaRow, deltaColumn)
        || !appendTileLayerMove(result, /*roof*/ true, deltaRow, deltaColumn)) {
        return {};
    }
    return result;
}

std::optional<std::pair<int, bool>> SelectionManager::selectionTileReference() const {
    // Prefer a floor tile (floors have no roof offset); fall back to a roof tile.
    int roofReference = -1;
    for (const auto& item : _state.items) {
        if (item.type == SelectionType::FLOOR_TILE) {
            return std::pair<int, bool>{ item.getTileIndex(), false };
        }
        if (item.type == SelectionType::ROOF_TILE && roofReference < 0) {
            roofReference = item.getTileIndex();
        }
    }
    if (roofReference < 0) {
        return std::nullopt; // no tiles selected
    }
    return std::pair<int, bool>{ roofReference, true };
}

std::optional<std::pair<int, int>> SelectionManager::selectionTileDelta(sf::Vector2f worldTranslation) const {
    // A tile centre round-trips through the hit-test, so translating and snapping it yields a delta
    // aligned with the dragged objects - deriving it from the click points double-rounds.
    const auto reference = selectionTileReference();
    if (!reference.has_value()) {
        return std::nullopt;
    }
    const auto [referenceIndex, roof] = *reference;
    const auto referenceCoords = indexToCoordinates(referenceIndex);
    const auto referenceScreen = coordinatesToScreenPosition(referenceCoords, roof);
    const sf::Vector2f referenceCentre(
        static_cast<float>(referenceScreen.x) + TILE_WIDTH / 2.0f,
        static_cast<float>(referenceScreen.y) + TILE_HEIGHT / 2.0f);

    const sf::Vector2f targetPos = referenceCentre + worldTranslation;
    const auto targetTile = roof ? _provider.getRoofTileAtPositionIncludingEmpty(targetPos)
                                 : _provider.getTileAtPosition(targetPos, /*isRoof*/ false);
    if (!targetTile.has_value()) {
        return std::nullopt;
    }
    const auto targetCoords = indexToCoordinates(*targetTile);
    return std::pair<int, int>{ static_cast<int>(targetCoords.x) - static_cast<int>(referenceCoords.x),
        static_cast<int>(targetCoords.y) - static_cast<int>(referenceCoords.y) };
}

std::optional<sf::Vector2f> SelectionManager::tileAlignedTranslation(sf::Vector2f rawTranslation) const {
    const auto reference = selectionTileReference();
    const auto delta = selectionTileDelta(rawTranslation);
    if (!reference.has_value() || !delta.has_value()) {
        return std::nullopt;
    }
    // World vector of moving the reference tile by the whole-tile delta (floor space — a pure
    // translation carries no roof offset).
    const auto referenceCoords = indexToCoordinates(reference->first);
    const TileCoordinates movedCoords(static_cast<unsigned int>(static_cast<int>(referenceCoords.x) + delta->first),
        static_cast<unsigned int>(static_cast<int>(referenceCoords.y) + delta->second));
    const auto from = coordinatesToScreenPosition(referenceCoords, /*isRoof*/ false);
    const auto to = coordinatesToScreenPosition(movedCoords, /*isRoof*/ false);
    return sf::Vector2f(static_cast<float>(to.x) - static_cast<float>(from.x),
        static_cast<float>(to.y) - static_cast<float>(from.y));
}

std::vector<TileChange> SelectionManager::planSelectionMoveForTranslation(sf::Vector2f worldTranslation) const {
    const auto delta = selectionTileDelta(worldTranslation);
    if (!delta.has_value()) {
        return {};
    }
    return planSelectionTileMove(delta->first, delta->second);
}

std::vector<int> SelectionManager::getHexesInArea(const sf::FloatRect& area) const {
    std::vector<int> result;
    result.reserve(1000); // Reserve space for typical selection

    auto hexGrid = _provider.getHexagonGrid();
    if (!hexGrid) {
        return result;
    }

    for (int hexIndex = 0; hexIndex < static_cast<int>(hexGrid->size()); ++hexIndex) {
        if (auto hex = hexGrid->getHexByPosition(static_cast<uint32_t>(hexIndex)); hex.has_value()) {
            sf::Vector2f hexPos(static_cast<float>(hex->get().x()), static_cast<float>(hex->get().y()));

            // Small hex-sized bounds around the center give a better selection feel.
            sf::FloatRect hexBounds(sf::Vector2f(hexPos.x - 16, hexPos.y - 8), sf::Vector2f(32, 16));
            if (area.findIntersection(hexBounds)) {
                result.push_back(hexIndex);
            }
        }
    }

    return result;
}

void SelectionManager::initializeSpatialIndex() {
    const auto& floorSprites = _provider.getFloorSprites();
    const auto& roofSprites = _provider.getRoofSprites();

    _spatialIndex.buildIndex(floorSprites, roofSprites);

    spdlog::info("Spatial index initialized with {} indexed tiles",
        _spatialIndex.getIndexedTileCount());
}

} // namespace geck::selection
