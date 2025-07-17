#pragma once

#include <SFML/Graphics.hpp>
#include <memory>
#include <vector>
#include <optional>
#include <variant>

#include "../util/Types.h"
#include "../editor/Object.h"

namespace geck::selection {

/**
 * @brief Represents different types of selections
 */
enum class SelectionType {
    ROOF_TILE,
    FLOOR_TILE,
    OBJECT
};

/**
 * @brief Represents a single selected item
 */
struct SelectedItem {
    SelectionType type;
    std::variant<int, std::shared_ptr<Object>> data; // tile index or object
    
    // Helper methods
    bool isTile() const { return type == SelectionType::ROOF_TILE || type == SelectionType::FLOOR_TILE; }
    bool isObject() const { return type == SelectionType::OBJECT; }
    
    int getTileIndex() const { return std::get<int>(data); }
    std::shared_ptr<Object> getObject() const { return std::get<std::shared_ptr<Object>>(data); }
    
    // Equality operator for finding/removing items
    bool operator==(const SelectedItem& other) const {
        if (type != other.type) return false;
        if (type == SelectionType::OBJECT) {
            return getObject() == other.getObject();
        } else {
            return getTileIndex() == other.getTileIndex();
        }
    }
};

/**
 * @brief Pure data class representing the current selection state
 * 
 * This class contains only selection data and has no dependencies on UI or rendering.
 * It provides a clean separation between selection state and selection operations.
 */
class SelectionState {
public:
    SelectionState() = default;
    ~SelectionState() = default;
    
    // Core selection data
    std::vector<SelectedItem> items;
    SelectionMode mode = SelectionMode::ALL;
    
    // Area selection support
    std::optional<sf::FloatRect> selectionArea;
    bool isDragging = false;
    sf::Vector2f dragStartPosition;
    
    // State management
    void clear() {
        items.clear();
        selectionArea.reset();
        isDragging = false;
    }
    
    bool isEmpty() const { return items.empty(); }
    size_t count() const { return items.size(); }
    
    // Item management
    void addItem(const SelectedItem& item) {
        if (!hasItem(item)) {
            items.push_back(item);
        }
    }
    
    void removeItem(const SelectedItem& item) {
        items.erase(
            std::remove(items.begin(), items.end(), item),
            items.end()
        );
    }
    
    bool hasItem(const SelectedItem& item) const {
        return std::find(items.begin(), items.end(), item) != items.end();
    }
    
    // Type-specific getters
    std::vector<int> getRoofTileIndices() const;
    std::vector<int> getFloorTileIndices() const;
    std::vector<std::shared_ptr<Object>> getObjects() const;
    
    // Area selection state
    void startAreaSelection(sf::Vector2f startPos, SelectionMode selectionMode) {
        dragStartPosition = startPos;
        mode = selectionMode;
        selectionArea = sf::FloatRect(startPos.x, startPos.y, 0.0f, 0.0f);
    }
    
    void updateAreaSelection(sf::Vector2f currentPos) {
        if (selectionArea.has_value()) {
            auto& area = selectionArea.value();
            area.width = currentPos.x - dragStartPosition.x;
            area.height = currentPos.y - dragStartPosition.y;
        }
    }
    
    void finishAreaSelection() {
        selectionArea.reset();
    }
    
    // Drag state
    void startDrag(sf::Vector2f startPos) {
        isDragging = true;
        dragStartPosition = startPos;
    }
    
    void updateDrag(sf::Vector2f /*currentPos*/) {
        // Drag position tracking could be added here if needed
    }
    
    void finishDrag() {
        isDragging = false;
    }
    
    void cancelDrag() {
        isDragging = false;
    }
    
    void cancelAreaSelection() {
        selectionArea.reset();
    }
    
    // State queries
    bool isAreaSelecting() const { return selectionArea.has_value(); }
};

} // namespace geck::selection