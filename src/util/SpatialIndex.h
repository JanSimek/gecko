#pragma once

#include <SFML/Graphics.hpp>
#include <vector>
#include <memory>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <cmath>

#include "Constants.h"
#include <algorithm>

namespace geck {

/**
 * @brief Spatial hash grid for efficient area-based queries
 *
 * This class provides O(1) average-case performance for spatial queries
 * by dividing the map into a grid and maintaining indices for each cell.
 */
template <typename T>
class SpatialIndex {
public:
    using ItemId = size_t;
    using QueryCallback = std::function<bool(const T&)>;

    explicit SpatialIndex(float cellSize = 100.0f);
    ~SpatialIndex() = default;

    // Item management
    ItemId addItem(const T& item, sf::FloatRect bounds);
    void clear();

    // Spatial queries
    std::vector<T> queryArea(sf::FloatRect area) const;

    // Optimized query with a callback (avoids allocations)
    void queryArea(sf::FloatRect area, QueryCallback callback) const;

private:
    struct Item {
        T data;
        sf::FloatRect bounds;

        Item(const T& d, sf::FloatRect b)
            : data(d)
            , bounds(b) { }
    };

    using CellKey = std::pair<int, int>; // (x, y) grid coordinates

    // Hash function for cell keys
    struct CellKeyHash {
        std::size_t operator()(const CellKey& key) const {
            return std::hash<int>()(key.first) ^ (std::hash<int>()(key.second) << 1);
        }
    };

    float _cellSize;
    std::unordered_map<ItemId, std::unique_ptr<Item>> _items;
    std::unordered_map<CellKey, std::vector<ItemId>, CellKeyHash> _cells;
    ItemId _nextId = 0;

    // Helper methods
    CellKey worldToCell(sf::Vector2f worldPos) const;
    std::vector<CellKey> boundsToKeys(sf::FloatRect bounds) const;
};

/**
 * @brief Specialized spatial index for map tiles
 *
 * Optimized for the hex grid layout used by Fallout 2 maps.
 */
class TileSpatialIndex {
public:
    TileSpatialIndex();
    ~TileSpatialIndex() = default;

    // Initialize with map data
    void buildIndex(const std::vector<sf::Sprite>& floorSprites,
        const std::vector<sf::Sprite>& roofSprites);

    // Fast tile queries
    std::vector<int> getTilesInArea(sf::FloatRect area, bool roof = false) const;

    // Performance stats
    size_t getIndexedTileCount() const { return _indexedTiles; }

private:
    SpatialIndex<int> _floorIndex;
    SpatialIndex<int> _roofIndex;
    size_t _indexedTiles = 0;
};

// Template implementation
template <typename T>
SpatialIndex<T>::SpatialIndex(float cellSize)
    : _cellSize(cellSize) {
    if (_cellSize <= 0) {
        _cellSize = 100.0f; // Default fallback
    }
}

template <typename T>
typename SpatialIndex<T>::ItemId SpatialIndex<T>::addItem(const T& item, sf::FloatRect bounds) {
    ItemId id = _nextId++;
    auto itemPtr = std::make_unique<Item>(item, bounds);

    // Add to spatial cells
    for (const auto& key : boundsToKeys(bounds)) {
        _cells[key].push_back(id);
    }

    _items[id] = std::move(itemPtr);
    return id;
}

template <typename T>
void SpatialIndex<T>::clear() {
    _items.clear();
    _cells.clear();
    _nextId = 0;
}

template <typename T>
std::vector<T> SpatialIndex<T>::queryArea(sf::FloatRect area) const {
    std::vector<T> results;
    results.reserve(100); // Reserve space for typical query

    queryArea(area, [&results](const T& item) {
        results.push_back(item);
        return true; // Continue searching
    });

    return results;
}

template <typename T>
void SpatialIndex<T>::queryArea(sf::FloatRect area, QueryCallback callback) const {
    std::unordered_set<ItemId> visitedItems; // Avoid duplicate results

    auto cellKeys = boundsToKeys(area);
    for (const auto& key : cellKeys) {
        auto cellIt = _cells.find(key);
        if (cellIt == _cells.end())
            continue;

        for (ItemId id : cellIt->second) {
            if (visitedItems.find(id) != visitedItems.end())
                continue;
            visitedItems.insert(id);

            auto itemIt = _items.find(id);
            if (itemIt == _items.end())
                continue;

            const auto& item = itemIt->second;
            // Check if item bounds actually intersect with query area
            if (item->bounds.findIntersection(area)) {
                if (!callback(item->data)) {
                    return; // Callback requested to stop
                }
            }
        }
    }
}

template <typename T>
typename SpatialIndex<T>::CellKey SpatialIndex<T>::worldToCell(sf::Vector2f worldPos) const {
    int x = static_cast<int>(std::floor(worldPos.x / _cellSize));
    int y = static_cast<int>(std::floor(worldPos.y / _cellSize));
    return { x, y };
}

template <typename T>
std::vector<typename SpatialIndex<T>::CellKey> SpatialIndex<T>::boundsToKeys(sf::FloatRect bounds) const {
    std::vector<CellKey> keys;

    CellKey minKey = worldToCell({ bounds.position.x, bounds.position.y });
    CellKey maxKey = worldToCell({ bounds.position.x + bounds.size.x, bounds.position.y + bounds.size.y });

    for (int x = minKey.first; x <= maxKey.first; ++x) {
        for (int y = minKey.second; y <= maxKey.second; ++y) {
            keys.push_back({ x, y });
        }
    }

    return keys;
}

} // namespace geck