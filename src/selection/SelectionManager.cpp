#include "SelectionManager.h"
#include "SelectionDataProvider.h"
#include "ui/viewport/ViewportController.h"
#include "format/map/MapObject.h"
#include "util/Constants.h"
#include "util/TileUtils.h"
#include "editor/HexagonGrid.h"
#include <algorithm>
#include <unordered_set>
#include <spdlog/spdlog.h>

namespace geck::selection {

SelectionManager::SelectionManager(SelectionDataProvider& provider)
    : _provider(provider) {
    _spatialIndex = std::make_unique<TileSpatialIndex>();
}

SelectionResult SelectionManager::selectAtPosition(sf::Vector2f worldPos, SelectionMode mode, int currentElevation) {
    switch (mode) {
        case SelectionMode::ALL:
            return cycleThroughItemsAtPosition(worldPos, currentElevation);

        case SelectionMode::OBJECTS:
        case SelectionMode::FLOOR_TILES:
        case SelectionMode::ROOF_TILES:
        case SelectionMode::ROOF_TILES_ALL:
        case SelectionMode::HEXES:
            return selectSingleAtPosition(worldPos, mode, currentElevation);

        default:
            return SelectionResult::createError("Invalid selection mode");
    }
}

void SelectionManager::appendTilesInArea(std::vector<SelectedItem>& items, const sf::FloatRect& area, bool roof, int elevation, bool includeEmpty) const {
    const auto tiles = includeEmpty ? getTilesInAreaIncludingEmpty(area, roof, elevation)
                                    : getTilesInArea(area, roof, elevation);
    const SelectionType type = roof ? SelectionType::ROOF_TILE : SelectionType::FLOOR_TILE;
    for (int tileIndex : tiles) {
        items.emplace_back(type, tileIndex);
    }
}

void SelectionManager::appendObjectsInArea(std::vector<SelectedItem>& items, const sf::FloatRect& area, int elevation) const {
    for (auto& object : getObjectsInArea(area, elevation)) {
        items.emplace_back(SelectionType::OBJECT, object);
    }
}

void SelectionManager::appendHexesInArea(std::vector<SelectedItem>& items, const sf::FloatRect& area) const {
    for (int hexIndex : getHexesInArea(area)) {
        items.emplace_back(SelectionType::HEX, hexIndex);
    }
}

std::vector<SelectedItem> SelectionManager::collectItemsInArea(const sf::FloatRect& area, SelectionMode mode, int elevation) const {
    std::vector<SelectedItem> items;

    switch (mode) {
        case SelectionMode::FLOOR_TILES:
            appendTilesInArea(items, area, false, elevation, false);
            break;

        case SelectionMode::ROOF_TILES:
            appendTilesInArea(items, area, true, elevation, false);
            break;

        case SelectionMode::ROOF_TILES_ALL:
            appendTilesInArea(items, area, true, elevation, true);
            break;

        case SelectionMode::OBJECTS:
            appendObjectsInArea(items, area, elevation);
            break;

        case SelectionMode::ALL:
            appendObjectsInArea(items, area, elevation);
            appendTilesInArea(items, area, false, elevation, false);
            appendTilesInArea(items, area, true, elevation, false);
            break;

        case SelectionMode::HEXES:
            appendHexesInArea(items, area);
            break;

        default:
            break;
    }

    return items;
}

SelectionResult SelectionManager::selectArea(const sf::FloatRect& area, SelectionMode mode, int currentElevation) {
    clearSelection();
    for (const auto& item : collectItemsInArea(area, mode, currentElevation)) {
        addItemToSelection(item);
    }
    _state.mode = mode;
    notifySelectionChanged();
    return SelectionResult::createSuccess("");
}

SelectionResult SelectionManager::deselectArea(const sf::FloatRect& area, SelectionMode mode, int currentElevation) {
    // Ctrl+drag: remove the covered items that are already selected; never add. Skip hidden
    // roof tiles so a roof layer you cannot see is not silently deselected.
    const bool roofVisible = _provider.isRoofVisible();
    for (const auto& item : collectItemsInArea(area, mode, currentElevation)) {
        if (item.type == SelectionType::ROOF_TILE && !roofVisible) {
            continue;
        }
        if (isItemSelected(item)) {
            removeItemFromSelection(item);
        }
    }
    _state.mode = mode;
    notifySelectionChanged();
    return SelectionResult::createSuccess("");
}

SelectionResult SelectionManager::addToSelection(sf::Vector2f worldPos, SelectionMode mode, int currentElevation) {
    switch (mode) {
        case SelectionMode::FLOOR_TILES: {
            auto tileIndex = getFloorTileAtPosition(worldPos, currentElevation);
            if (tileIndex) {
                SelectedItem item{ SelectionType::FLOOR_TILE, tileIndex.value() };
                addItemToSelection(item);
                _state.mode = mode;
                notifySelectionChanged();
                return SelectionResult::createSuccess("");
            }
            break;
        }

        case SelectionMode::ROOF_TILES: {
            auto tileIndex = getRoofTileAtPosition(worldPos, currentElevation);
            if (tileIndex) {
                SelectedItem item{ SelectionType::ROOF_TILE, tileIndex.value() };
                addItemToSelection(item);
                _state.mode = mode;
                notifySelectionChanged();
                return SelectionResult::createSuccess("");
            }
            break;
        }

        case SelectionMode::ROOF_TILES_ALL: {
            auto tileIndex = getRoofTileAtPositionIncludingEmpty(worldPos, currentElevation);
            if (tileIndex) {
                SelectedItem item{ SelectionType::ROOF_TILE, tileIndex.value() };
                addItemToSelection(item);
                _state.mode = mode;
                notifySelectionChanged();
                return SelectionResult::createSuccess("");
            }
            break;
        }

        case SelectionMode::OBJECTS: {
            auto objects = getObjectsAtPosition(worldPos, currentElevation);
            if (!objects.empty()) {
                SelectedItem item{ SelectionType::OBJECT, objects[0] };
                addItemToSelection(item);
                _state.mode = mode;
                notifySelectionChanged();
                return SelectionResult::createSuccess();
            }
            break;
        }

        case SelectionMode::ALL:
            // ALL mode adds the first available item in priority order (roof, object, floor) without cycling.
            {
                auto tileIndex = getRoofTileAtPosition(worldPos, currentElevation);
                if (tileIndex) {
                    SelectedItem item{ SelectionType::ROOF_TILE, tileIndex.value() };
                    addItemToSelection(item);
                    _state.mode = mode;
                    notifySelectionChanged();
                    return SelectionResult::createSuccess("");
                }
            }
            {
                auto objects = getObjectsAtPosition(worldPos, currentElevation);
                if (!objects.empty()) {
                    SelectedItem item{ SelectionType::OBJECT, objects[0] };
                    addItemToSelection(item);
                    _state.mode = mode;
                    notifySelectionChanged();
                    return SelectionResult::createSuccess("");
                }
            }
            {
                auto tileIndex = getFloorTileAtPosition(worldPos, currentElevation);
                if (tileIndex) {
                    SelectedItem item{ SelectionType::FLOOR_TILE, tileIndex.value() };
                    addItemToSelection(item);
                    _state.mode = mode;
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

void SelectionManager::appendObjectCandidates(std::vector<SelectedItem>& candidates, sf::Vector2f worldPos, int elevation) const {
    for (auto& object : getObjectsAtPosition(worldPos, elevation)) {
        candidates.emplace_back(SelectionType::OBJECT, object);
    }
}

void SelectionManager::appendHexCandidate(std::vector<SelectedItem>& candidates, sf::Vector2f worldPos) const {
    auto* viewport = _provider.getViewportController();
    const auto* hexGrid = _provider.getHexagonGrid();
    if (viewport && hexGrid) {
        int hexIndex = viewport->worldPosToHexIndex(worldPos);
        if (hexGrid->containsPosition(hexIndex)) {
            candidates.emplace_back(SelectionType::HEX, hexIndex);
        }
    }
}

std::vector<SelectedItem> SelectionManager::collectDeselectableAtPosition(sf::Vector2f worldPos, SelectionMode mode, int elevation) const {
    std::vector<SelectedItem> candidates;

    switch (mode) {
        case SelectionMode::FLOOR_TILES:
            appendTileCandidate(candidates, getFloorTileAtPosition(worldPos, elevation), SelectionType::FLOOR_TILE);
            break;

        case SelectionMode::ROOF_TILES:
            appendRoofCandidate(candidates, getRoofTileAtPosition(worldPos, elevation));
            break;

        case SelectionMode::ROOF_TILES_ALL:
            appendRoofCandidate(candidates, getRoofTileAtPositionIncludingEmpty(worldPos, elevation));
            break;

        case SelectionMode::OBJECTS:
            appendObjectCandidates(candidates, worldPos, elevation);
            break;

        case SelectionMode::ALL:
            // Priority order matches single-click selection: roof -> objects -> floor.
            appendRoofCandidate(candidates, getRoofTileAtPosition(worldPos, elevation));
            appendObjectCandidates(candidates, worldPos, elevation);
            appendTileCandidate(candidates, getFloorTileAtPosition(worldPos, elevation), SelectionType::FLOOR_TILE);
            break;

        case SelectionMode::HEXES:
            appendHexCandidate(candidates, worldPos);
            break;

        default:
            break;
    }

    return candidates;
}

SelectionResult SelectionManager::deselectAtPosition(sf::Vector2f worldPos, SelectionMode mode, int currentElevation) {
    // Ctrl+click is deselect-only: remove the topmost visible layer under the cursor that is
    // already selected, and never add. Clicking outside the selection leaves it untouched.
    // collectDeselectableAtPosition already drops hidden roofs, so they stay selected.
    for (const auto& candidate : collectDeselectableAtPosition(worldPos, mode, currentElevation)) {
        if (isItemSelected(candidate)) {
            removeItemFromSelection(candidate);
            _state.mode = mode;
            notifySelectionChanged();
            return SelectionResult::createSuccess("Item removed from selection");
        }
    }

    return SelectionResult::createNoChange();
}

bool SelectionManager::startDrag(sf::Vector2f worldPos) {
    if (_state.isEmpty()) {
        return false;
    }

    _state.isDragging = true;
    _state.dragStartPosition = worldPos;

    spdlog::debug("Started drag operation at ({:.2f}, {:.2f})", worldPos.x, worldPos.y);
    return true;
}

void SelectionManager::updateDrag(sf::Vector2f currentPos) {
    if (!_state.isDragging) {
        return;
    }

    spdlog::debug("Updating drag to ({:.2f}, {:.2f})", currentPos.x, currentPos.y);
}

SelectionResult SelectionManager::finishDrag(sf::Vector2f endPos) {
    if (!_state.isDragging) {
        return SelectionResult::createError("No drag operation in progress");
    }

    _state.isDragging = false;

    sf::Vector2f offset = endPos - _state.dragStartPosition;

    spdlog::info("Drag completed: offset ({:.2f}, {:.2f})", offset.x, offset.y);

    bool hasMovements = false;

    for (const auto& item : _state.items) {
        switch (item.type) {
            case SelectionType::OBJECT: {
                auto object = item.getObject();
                if (object && moveObject(object, offset)) {
                    hasMovements = true;
                }
                break;
            }

            case SelectionType::FLOOR_TILE: {
                int tileIndex = item.getTileIndex();
                if (moveTile(tileIndex, offset, false)) {
                    hasMovements = true;
                }
                break;
            }

            case SelectionType::ROOF_TILE: {
                int tileIndex = item.getTileIndex();
                if (moveTile(tileIndex, offset, true)) {
                    hasMovements = true;
                }
                break;
            }

            case SelectionType::HEX:
                // Hex selections are placement markers and do not move with drag operations.
                break;
        }
    }

    if (hasMovements) {
        notifySelectionChanged();
        return SelectionResult::createSuccess("Items moved successfully");
    } else {
        return SelectionResult::createNoChange();
    }
}

void SelectionManager::cancelDrag() {
    _state.isDragging = false;
    spdlog::debug("Drag operation cancelled");
}

bool SelectionManager::startAreaSelection(sf::Vector2f startPos, SelectionMode mode) {
    _state.selectionArea = sf::FloatRect({ startPos.x, startPos.y }, { 0, 0 });
    _state.mode = mode;

    spdlog::debug("Started area selection at ({:.2f}, {:.2f}) for mode: {}",
        startPos.x, startPos.y, static_cast<int>(mode));
    return true;
}

void SelectionManager::updateAreaSelection(sf::Vector2f currentPos) {
    if (!_state.selectionArea) {
        return;
    }

    auto& rect = _state.selectionArea.value();
    sf::Vector2f startPos(rect.position.x, rect.position.y);

    // Update rectangle to encompass start and current position
    float left = std::min(startPos.x, currentPos.x);
    float top = std::min(startPos.y, currentPos.y);
    float right = std::max(startPos.x, currentPos.x);
    float bottom = std::max(startPos.y, currentPos.y);

    rect = sf::FloatRect({ left, top }, { right - left, bottom - top });

    spdlog::debug("Updated area selection: ({:.2f}, {:.2f}, {:.2f}, {:.2f})",
        rect.position.x, rect.position.y, rect.size.x, rect.size.y);
}

SelectionResult SelectionManager::finishAreaSelection() {
    if (!_state.selectionArea) {
        return SelectionResult::createError("No area selection in progress");
    }

    auto area = _state.selectionArea.value();
    auto mode = _state.mode;

    // Clear the area selection state but keep the mode
    _state.selectionArea.reset();

    // Bug fix: previously hardcoded elevation 0, which broke area selection on
    // any elevation other than the ground floor. Use the host's current elevation.
    return selectArea(area, mode, _provider.getCurrentElevation());
}

void SelectionManager::cancelAreaSelection() {
    _state.selectionArea.reset();
    spdlog::debug("Area selection cancelled");
}

void SelectionManager::clearSelection() {
    bool hadSelection = !_state.isEmpty();
    _state.clear();

    if (hadSelection) {
        notifySelectionChanged();
    }
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
            if (const auto* hexGrid = _provider.getHexagonGrid()) {
                for (int position = 0; position < HexagonGrid::GRID_WIDTH * HexagonGrid::GRID_HEIGHT; ++position) {
                    if (hexGrid->containsPosition(position)) {
                        addItemToSelection(SelectedItem{ SelectionType::HEX, position });
                    }
                }
            }
            break;

        case SelectionMode::SCROLL_BLOCKER_RECTANGLE:
            // Rectangle mode is a drawing tool, not a persistent selection set.
            spdlog::debug("SelectionManager::selectAll ignored in scroll blocker rectangle mode");
            break;

        case SelectionMode::ALL:
            // Inlined rather than recursing to avoid the clearSelection() conflicts that nested calls would cause.
            for (int i = 0; i < static_cast<int>(Map::TILES_PER_ELEVATION); ++i) {
                SelectedItem item{ SelectionType::FLOOR_TILE, i };
                addItemToSelection(item);
            }

            // Roof tiles: only those with textures
            for (int i = 0; i < static_cast<int>(Map::TILES_PER_ELEVATION); ++i) {
                auto tile = _provider.getMapFile().tiles.at(currentElevation).at(i);
                if (tile.getRoof() != Map::EMPTY_TILE) {
                    SelectedItem item{ SelectionType::ROOF_TILE, i };
                    addItemToSelection(item);
                }
            }

            for (auto& object : _provider.getObjects()) {
                SelectedItem item{ SelectionType::OBJECT, object };
                addItemToSelection(item);
            }
            break;

        case SelectionMode::NUM_SELECTION_TYPES:
            // This should not happen - it's just an enum count
            break;
    }

    _state.mode = mode;
    notifySelectionChanged();
}

bool SelectionManager::isSpriteClicked(sf::Vector2f worldPos, const sf::Sprite& sprite) const {
    return sprite.getGlobalBounds().contains(worldPos);
}

std::vector<std::shared_ptr<Object>> SelectionManager::getObjectsAtPosition(sf::Vector2f worldPos, [[maybe_unused]] int elevation) const {
    return _provider.getObjectsAtPosition(worldPos);
}

std::optional<int> SelectionManager::getRoofTileAtPosition(sf::Vector2f worldPos, [[maybe_unused]] int elevation) const {
    return _provider.getTileAtPosition(worldPos, true); // true for roof
}

std::optional<int> SelectionManager::getRoofTileAtPositionIncludingEmpty(sf::Vector2f worldPos, [[maybe_unused]] int elevation) const {
    return _provider.getRoofTileAtPositionIncludingEmpty(worldPos);
}

std::optional<int> SelectionManager::getFloorTileAtPosition(sf::Vector2f worldPos, [[maybe_unused]] int elevation) const {
    return _provider.getTileAtPosition(worldPos, false); // false for floor
}

std::vector<int> SelectionManager::getTilesInArea(const sf::FloatRect& area, bool roof, [[maybe_unused]] int elevation) const {
    if (_spatialIndex) {
        auto spatialResult = _spatialIndex->getTilesInArea(area, roof);

        // The spatial index doesn't track empty tiles, so filter roof results by tile content.
        if (roof) {
            std::vector<int> result;
            result.reserve(spatialResult.size());

            const auto& mapFile = _provider.getMapFile();
            int currentElevation = _provider.getCurrentElevation();

            for (int tileIndex : spatialResult) {
                if (mapFile.tiles.at(currentElevation).at(tileIndex).getRoof() != Map::EMPTY_TILE) {
                    result.push_back(tileIndex);
                }
            }

            return result;
        } else {
            // Floor tiles always have content, so no filtering needed.
            return spatialResult;
        }
    }

    // Fallback to linear search if spatial index not available
    std::vector<int> result;
    result.reserve(1000);

    const auto& floorSprites = _provider.getFloorSprites();
    const auto& roofSprites = _provider.getRoofSprites();
    const auto& mapFile = _provider.getMapFile();
    int currentElevation = _provider.getCurrentElevation();

    for (int i = 0; i < static_cast<int>(Map::TILES_PER_ELEVATION); ++i) {
        sf::FloatRect tileBounds = roof ? roofSprites.at(i).getGlobalBounds() : floorSprites.at(i).getGlobalBounds();

        if (area.findIntersection(tileBounds)) {
            if (roof) {
                if (mapFile.tiles.at(currentElevation).at(i).getRoof() != Map::EMPTY_TILE) {
                    result.push_back(i);
                }
            } else {
                result.push_back(i);
            }
        }
    }

    return result;
}

std::vector<int> SelectionManager::getTilesInAreaIncludingEmpty(const sf::FloatRect& area, bool roof, [[maybe_unused]] int elevation) const {
    // Include all tiles intersecting the area, even if currently empty.
    // We intentionally skip the spatial index here because it only tracks placed tiles.
    std::vector<int> result;
    result.reserve(1000);

    const auto& floorSprites = _provider.getFloorSprites();
    const auto& roofSprites = _provider.getRoofSprites();

    for (int i = 0; i < TILES_PER_ELEVATION; ++i) {
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

std::vector<std::shared_ptr<Object>> SelectionManager::getObjectsInArea(const sf::FloatRect& area, [[maybe_unused]] int elevation) const {
    // TileSpatialIndex only handles tiles, so objects use a linear search.
    std::vector<std::shared_ptr<Object>> result;
    result.reserve(100);

    const auto& allObjects = _provider.getObjects();

    for (const auto& object : allObjects) {
        const auto& sprite = object->getSprite();
        sf::FloatRect objectBounds = sprite.getGlobalBounds();

        // Bounds intersection is much faster than full hit detection.
        if (area.findIntersection(objectBounds)) {
            result.push_back(object);
        }
    }

    return result;
}

SelectionResult SelectionManager::selectSingleAtPosition(sf::Vector2f worldPos, SelectionMode mode, int elevation) {
    clearSelection();

    switch (mode) {
        case SelectionMode::FLOOR_TILES: {
            auto tileIndex = getFloorTileAtPosition(worldPos, elevation);
            if (tileIndex) {
                SelectedItem item{ SelectionType::FLOOR_TILE, tileIndex.value() };
                addItemToSelection(item);
                _state.mode = mode;
                notifySelectionChanged();
                return SelectionResult::createSuccess();
            }
            break;
        }

        case SelectionMode::ROOF_TILES: {
            auto tileIndex = getRoofTileAtPosition(worldPos, elevation);
            if (tileIndex) {
                SelectedItem item{ SelectionType::ROOF_TILE, tileIndex.value() };
                addItemToSelection(item);
                _state.mode = mode;
                notifySelectionChanged();
                return SelectionResult::createSuccess();
            }
            break;
        }

        case SelectionMode::ROOF_TILES_ALL: {
            auto tileIndex = getRoofTileAtPositionIncludingEmpty(worldPos, elevation);
            if (tileIndex) {
                SelectedItem item{ SelectionType::ROOF_TILE, tileIndex.value() };
                addItemToSelection(item);
                _state.mode = mode;
                notifySelectionChanged();
                return SelectionResult::createSuccess();
            }
            break;
        }

        case SelectionMode::OBJECTS: {
            auto objects = getObjectsAtPosition(worldPos, elevation);
            if (!objects.empty()) {
                SelectedItem item{ SelectionType::OBJECT, objects[0] }; // Select first (topmost)
                addItemToSelection(item);
                _state.mode = mode;
                notifySelectionChanged();
                return SelectionResult::createSuccess();
            }
            break;
        }

        case SelectionMode::HEXES: {
            int hexIndex = _provider.getViewportController()->worldPosToHexIndex(worldPos);
            const auto* hexGrid = _provider.getHexagonGrid();
            if (hexGrid && hexGrid->containsPosition(hexIndex)) {
                SelectedItem item{ SelectionType::HEX, hexIndex };
                addItemToSelection(item);
                _state.mode = mode;
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

SelectionResult SelectionManager::cycleThroughItemsAtPosition(sf::Vector2f worldPos, int elevation) {
    auto objectsAtPos = getObjectsAtPosition(worldPos, elevation);
    auto roofTileIndex = getRoofTileAtPosition(worldPos, elevation);
    auto floorTileIndex = getFloorTileAtPosition(worldPos, elevation);

    bool roofSelected = false;
    bool floorSelected = false;
    int selectedObjectIndex = -1;

    for (const auto& item : _state.items) {
        switch (item.type) {
            case SelectionType::ROOF_TILE:
                if (roofTileIndex && item.getTileIndex() == roofTileIndex.value()) {
                    roofSelected = true;
                }
                break;

            case SelectionType::FLOOR_TILE:
                if (floorTileIndex && item.getTileIndex() == floorTileIndex.value()) {
                    floorSelected = true;
                }
                break;

            case SelectionType::OBJECT: {
                auto it = std::find(objectsAtPos.begin(), objectsAtPos.end(), item.getObject());
                if (it != objectsAtPos.end()) {
                    selectedObjectIndex = static_cast<int>(std::distance(objectsAtPos.begin(), it));
                }
                break;
            }

            case SelectionType::HEX:
                // Hex selections are not part of ALL-mode item cycling.
                break;
        }
    }

    // Cycle order: roof -> objects (all) -> floor -> deselect
    if (roofSelected) {
        if (!objectsAtPos.empty()) {
            clearSelection();
            SelectedItem item{ SelectionType::OBJECT, objectsAtPos[0] };
            addItemToSelection(item);
            _state.mode = SelectionMode::ALL;
            notifySelectionChanged();
            return SelectionResult::createSuccess();
        } else if (floorTileIndex) {
            clearSelection();
            SelectedItem item{ SelectionType::FLOOR_TILE, floorTileIndex.value() };
            addItemToSelection(item);
            _state.mode = SelectionMode::ALL;
            notifySelectionChanged();
            return SelectionResult::createSuccess();
        } else {
            clearSelection();
            return SelectionResult::createSuccess();
        }
    } else if (selectedObjectIndex >= 0) {
        if (selectedObjectIndex < static_cast<int>(objectsAtPos.size()) - 1) {
            clearSelection();
            SelectedItem item{ SelectionType::OBJECT, objectsAtPos[selectedObjectIndex + 1] };
            addItemToSelection(item);
            _state.mode = SelectionMode::ALL;
            notifySelectionChanged();
            return SelectionResult::createSuccess();
        } else if (floorTileIndex) {
            clearSelection();
            SelectedItem item{ SelectionType::FLOOR_TILE, floorTileIndex.value() };
            addItemToSelection(item);
            _state.mode = SelectionMode::ALL;
            notifySelectionChanged();
            return SelectionResult::createSuccess();
        } else {
            clearSelection();
            return SelectionResult::createSuccess();
        }
    } else if (floorSelected) {
        clearSelection();
        return SelectionResult::createSuccess();
    } else {
        // Nothing selected yet: start at roof, else first available item.
        if (roofTileIndex) {
            clearSelection();
            SelectedItem item{ SelectionType::ROOF_TILE, roofTileIndex.value() };
            addItemToSelection(item);
            _state.mode = SelectionMode::ALL;
            notifySelectionChanged();
            return SelectionResult::createSuccess();
        } else if (!objectsAtPos.empty()) {
            clearSelection();
            SelectedItem item{ SelectionType::OBJECT, objectsAtPos[0] };
            addItemToSelection(item);
            _state.mode = SelectionMode::ALL;
            notifySelectionChanged();
            return SelectionResult::createSuccess();
        } else if (floorTileIndex) {
            clearSelection();
            SelectedItem item{ SelectionType::FLOOR_TILE, floorTileIndex.value() };
            addItemToSelection(item);
            _state.mode = SelectionMode::ALL;
            notifySelectionChanged();
            return SelectionResult::createSuccess();
        }
    }

    clearSelection();
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

bool SelectionManager::moveObject(std::shared_ptr<Object> object, sf::Vector2f offset) {
    if (!object) {
        return false;
    }

    auto& mapObject = object->getMapObject();
    int32_t currentPosition = mapObject.position;

    auto currentCoords = indexToCoordinates(currentPosition);

    // Approximate screen offset -> hex offset; a full implementation would use proper isometric projection.
    int deltaX = static_cast<int>(offset.x / TILE_WIDTH);
    int deltaY = static_cast<int>(offset.y / TILE_HEIGHT);

    int newX = static_cast<int>(currentCoords.x) + deltaY; // Screen Y maps to hex X
    int newY = static_cast<int>(currentCoords.y) + deltaX; // Screen X maps to hex Y

    if (newX < 0 || newX >= MAP_HEIGHT || newY < 0 || newY >= MAP_WIDTH) {
        spdlog::debug("Object move out of bounds: ({}, {}) -> ({}, {})",
            currentCoords.x, currentCoords.y, newX, newY);
        return false;
    }

    int newPosition = coordinatesToIndex(TileCoordinates(newX, newY));
    mapObject.position = newPosition;

    // Object sprite position is updated on the next render cycle.

    spdlog::info("Moved object from tile {} to tile {}", currentPosition, newPosition);
    return true;
}

bool SelectionManager::moveTile(int sourceTileIndex, sf::Vector2f offset, bool isRoof) {
    if (sourceTileIndex < 0 || sourceTileIndex >= static_cast<int>(Map::TILES_PER_ELEVATION)) {
        return false;
    }

    auto sourceCoords = indexToCoordinates(sourceTileIndex);

    // Approximate screen offset -> hex offset; screen Y maps to hex X, screen X maps to hex Y.
    int deltaX = static_cast<int>(offset.x / TILE_WIDTH);
    int deltaY = static_cast<int>(offset.y / TILE_HEIGHT);

    int newX = static_cast<int>(sourceCoords.x) + deltaY;
    int newY = static_cast<int>(sourceCoords.y) + deltaX;

    if (newX < 0 || newX >= MAP_HEIGHT || newY < 0 || newY >= MAP_WIDTH) {
        spdlog::debug("Tile move out of bounds: ({}, {}) -> ({}, {})",
            sourceCoords.x, sourceCoords.y, newX, newY);
        return false;
    }

    int targetTileIndex = coordinatesToIndex(TileCoordinates(newX, newY));

    if (targetTileIndex == sourceTileIndex) {
        return false;
    }

    int currentElevation = _provider.getCurrentElevation();

    auto& mapFile = _provider.getMapFile();
    auto& tiles = mapFile.tiles.at(currentElevation);

    auto& sourceTile = tiles.at(sourceTileIndex);
    auto& targetTile = tiles.at(targetTileIndex);

    if (isRoof) {
        uint16_t sourceRoof = sourceTile.getRoof();
        if (sourceRoof == Map::EMPTY_TILE) {
            return false; // Nothing to move
        }

        targetTile.setRoof(sourceRoof);
        sourceTile.setRoof(Map::EMPTY_TILE);

        spdlog::info("Moved roof tile from {} to {}", sourceTileIndex, targetTileIndex);
    } else {
        uint16_t sourceFloor = sourceTile.getFloor();
        uint16_t targetFloor = targetTile.getFloor();

        // Floor tiles are always present, so swap rather than clear the source.
        targetTile.setFloor(sourceFloor);
        sourceTile.setFloor(targetFloor);

        spdlog::info("Swapped floor tiles {} and {}", sourceTileIndex, targetTileIndex);
    }

    // Tile sprites are updated on the next render cycle.

    return true;
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
    if (!_spatialIndex) {
        spdlog::error("Spatial index not initialized");
        return;
    }

    const auto& floorSprites = _provider.getFloorSprites();
    const auto& roofSprites = _provider.getRoofSprites();

    _spatialIndex->buildIndex(floorSprites, roofSprites);

    spdlog::info("Spatial index initialized with {} indexed tiles",
        _spatialIndex->getIndexedTileCount());
}

} // namespace geck::selection
