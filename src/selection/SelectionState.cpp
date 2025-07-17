#include "SelectionState.h"

namespace geck::selection {

std::vector<int> SelectionState::getRoofTileIndices() const {
    std::vector<int> indices;
    indices.reserve(items.size()); // Reserve space for worst-case scenario
    for (const auto& item : items) {
        if (item.type == SelectionType::ROOF_TILE) {
            indices.push_back(item.getTileIndex());
        }
    }
    return indices;
}

std::vector<int> SelectionState::getFloorTileIndices() const {
    std::vector<int> indices;
    indices.reserve(items.size()); // Reserve space for worst-case scenario
    for (const auto& item : items) {
        if (item.type == SelectionType::FLOOR_TILE) {
            indices.push_back(item.getTileIndex());
        }
    }
    return indices;
}

std::vector<std::shared_ptr<Object>> SelectionState::getObjects() const {
    std::vector<std::shared_ptr<Object>> objects;
    objects.reserve(items.size()); // Reserve space for worst-case scenario
    for (const auto& item : items) {
        if (item.type == SelectionType::OBJECT) {
            objects.push_back(item.getObject());
        }
    }
    return objects;
}

} // namespace geck::selection