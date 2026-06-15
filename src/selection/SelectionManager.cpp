#include "SelectionManager.h"
#include "SelectionDataProvider.h"
#include "ui/viewport/ViewportController.h"
#include "format/map/MapObject.h"
#include "util/Constants.h"
#include "util/TileUtils.h"
#include "editor/HexagonGrid.h"
#include <algorithm>
#include <unordered_map>
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
            // In mixed (ALL) mode you should only select roofs you can see. The explicit
            // ROOF_TILES modes above still select roofs regardless of the layer toggle.
            if (_provider.isRoofVisible()) {
                appendTilesInArea(items, area, true, elevation, false);
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

SelectionResult SelectionManager::selectArea(const sf::FloatRect& area, SelectionMode mode, int currentElevation) {
    clearSelection();
    for (const auto& item : collectItemsInArea(area, mode, currentElevation)) {
        addItemToSelection(item);
    }
    _state.mode = mode;
    notifySelectionChanged();
    return SelectionResult::createSuccess("");
}

SelectionResult SelectionManager::addArea(const sf::FloatRect& area, SelectionMode mode, int currentElevation) {
    // Alt+drag: add the covered items to the current selection without clearing it.
    // addItemToSelection ignores items that are already selected, so overlaps are harmless.
    for (const auto& item : collectItemsInArea(area, mode, currentElevation)) {
        addItemToSelection(item);
    }
    _state.mode = mode;
    notifySelectionChanged();
    return SelectionResult::createSuccess("");
}

std::vector<SelectedItem> SelectionManager::itemsToDeselectInArea(const sf::FloatRect& area, SelectionMode mode, int currentElevation) const {
    // The covered items a Ctrl+drag would remove: currently selected and on a visible layer.
    // A roof you cannot see must never be silently deselected, so skip hidden roof tiles.
    // (Hidden objects are already excluded by collectItemsInArea -> getObjectsInArea; floors
    // are always visible.)
    const bool roofVisible = _provider.isRoofVisible();
    std::vector<SelectedItem> toRemove;
    for (const auto& item : collectItemsInArea(area, mode, currentElevation)) {
        if (item.type == SelectionType::ROOF_TILE && !roofVisible) {
            continue;
        }
        if (isItemSelected(item)) {
            toRemove.push_back(item);
        }
    }
    return toRemove;
}

SelectionResult SelectionManager::deselectArea(const sf::FloatRect& area, SelectionMode mode, int currentElevation) {
    // Ctrl+drag: remove the covered items that are already selected; never add.
    for (const auto& item : itemsToDeselectInArea(area, mode, currentElevation)) {
        removeItemFromSelection(item);
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

bool SelectionManager::isPointOnSelection(sf::Vector2f worldPos) const {
    // The visible, selectable layers under the cursor (roof skipped while hidden, hidden objects
    // dropped, floor always present) — the same candidates Ctrl+click deselect considers. If any
    // of them is in the current selection, the point is a valid grab handle for moving it.
    const auto candidates = collectDeselectableAtPosition(worldPos, SelectionMode::ALL, _provider.getCurrentElevation());
    return std::ranges::any_of(candidates, [this](const SelectedItem& candidate) { return isItemSelected(candidate); });
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

    // Select on the host's current elevation so area selection works above the ground floor.
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
        // Only visible objects are selectable, matching getObjectsAtPosition's point-pick
        // rule. Without this, an area drag selects objects on a hidden layer (e.g. scroll
        // blockers while their layer is off) that the user cannot see.
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
    // A tile centre round-trips through the hit-test, so translating it and snapping yields a
    // whole-tile delta aligned with how the dragged objects move — deriving the delta from the
    // arbitrary click/drop points double-rounds and lands the tiles off-centre.
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
