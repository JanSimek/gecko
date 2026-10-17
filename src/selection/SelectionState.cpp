#include "SelectionState.h"

#include "format/map/MapObject.h"
#include "util/TileUtils.h"

namespace geck::selection {

std::vector<int> SelectionState::getRoofTileIndices() const {
    std::vector<int> indices;
    indices.reserve(items.size());
    for (const auto& item : items) {
        if (item.type == SelectionType::ROOF_TILE) {
            indices.push_back(item.getTileIndex());
        }
    }
    return indices;
}

std::vector<int> SelectionState::getFloorTileIndices() const {
    std::vector<int> indices;
    indices.reserve(items.size());
    for (const auto& item : items) {
        if (item.type == SelectionType::FLOOR_TILE) {
            indices.push_back(item.getTileIndex());
        }
    }
    return indices;
}

std::vector<std::shared_ptr<Object>> SelectionState::getObjects() const {
    std::vector<std::shared_ptr<Object>> objects;
    objects.reserve(items.size());
    for (const auto& item : items) {
        if (item.type == SelectionType::OBJECT) {
            objects.push_back(item.getObject());
        }
    }
    return objects;
}

std::vector<int> SelectionState::getHexIndices() const {
    std::vector<int> indices;
    indices.reserve(items.size());
    for (const auto& item : items) {
        if (item.type == SelectionType::HEX) {
            indices.push_back(item.getHexIndex());
        }
    }
    return indices;
}

std::optional<SelectedItem> remapSelectedItem(const SelectedItem& item, const ObjectsByMapObject& objectsByMapObject,
    const std::optional<std::pair<int, int>>& tileDelta) {
    using enum SelectionType;
    switch (item.type) {
        case OBJECT: {
            const auto oldObject = item.getObject();
            if (!oldObject || !oldObject->hasMapObject()) {
                return std::nullopt;
            }
            const auto found = objectsByMapObject.find(oldObject->getMapObjectPtr().get());
            if (found == objectsByMapObject.end()) {
                return std::nullopt;
            }
            return SelectedItem{ OBJECT, found->second };
        }
        case FLOOR_TILE:
        case ROOF_TILE: {
            if (!tileDelta.has_value()) {
                return item;
            }
            const auto coords = indexToCoordinates(item.getTileIndex());
            const int newRow = static_cast<int>(coords.x) + tileDelta->first;
            const int newColumn = static_cast<int>(coords.y) + tileDelta->second;
            if (!isTileRowColInGrid(newRow, newColumn)) {
                return std::nullopt;
            }
            return SelectedItem{ item.type,
                coordinatesToIndex(TileCoordinates(static_cast<unsigned int>(newRow), static_cast<unsigned int>(newColumn))) };
        }
        case HEX:
            return item; // hex markers don't move with a region drag
    }
    return std::nullopt;
}

} // namespace geck::selection
