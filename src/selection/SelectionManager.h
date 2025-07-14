#pragma once

#include <SFML/Graphics.hpp>
#include <memory>
#include <vector>
#include <optional>
#include <variant>
#include <functional>

#include "../util/Types.h"
#include "../format/map/Map.h"
#include "../editor/Object.h"

namespace geck::selection {

// Forward declarations
class SelectionObserver;

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
};

/**
 * @brief Represents the current selection state
 */
struct Selection {
    std::vector<SelectedItem> items;
    SelectionMode mode;
    
    // Area selection support (for future drag operations)
    std::optional<sf::FloatRect> selectionArea;
    bool isDragging = false;
    sf::Vector2f dragStartPosition;
    
    void clear() {
        items.clear();
        selectionArea.reset();
        isDragging = false;
    }
    
    bool isEmpty() const { return items.empty(); }
    size_t count() const { return items.size(); }
    
    // Helper methods for different selection types
    std::vector<int> getRoofTileIndices() const;
    std::vector<int> getFloorTileIndices() const;
    std::vector<std::shared_ptr<Object>> getObjects() const;
};

/**
 * @brief Result of a selection operation
 */
struct SelectionResult {
    bool success = false;
    bool selectionChanged = false;
    std::string message; // For error messages or status info
    
    static SelectionResult createSuccess() { return {true, true, ""}; }
    static SelectionResult createNoChange() { return {true, false, ""}; }
    static SelectionResult createError(const std::string& msg) { return {false, false, msg}; }
};

/**
 * @brief Interface for classes that want to observe selection changes
 */
class SelectionObserver {
public:
    virtual ~SelectionObserver() = default;
    virtual void onSelectionChanged(const Selection& selection) = 0;
    virtual void onSelectionCleared() = 0;
};

/**
 * @brief Manages all selection operations for tiles and objects
 * 
 * This class encapsulates all selection logic and prepares for future features:
 * - Drag and drop functionality
 * - Area selection for tiles
 * - Multiple object selection
 */
// Forward declaration
class SelectionBridge;

class SelectionManager {
public:
    explicit SelectionManager(Map* map);
    ~SelectionManager() = default;
    
    // Set the bridge for connecting to UI layer
    void setBridge(SelectionBridge* bridge) { _bridge = bridge; }
    
    // Selection operations
    SelectionResult selectAtPosition(sf::Vector2f worldPos, SelectionMode mode, int currentElevation);
    SelectionResult selectArea(const sf::FloatRect& area, SelectionMode mode, int currentElevation);
    SelectionResult addToSelection(sf::Vector2f worldPos, SelectionMode mode, int currentElevation);
    SelectionResult toggleSelection(sf::Vector2f worldPos, SelectionMode mode, int currentElevation);
    
    // Drag and drop preparation
    bool startDrag(sf::Vector2f worldPos);
    void updateDrag(sf::Vector2f currentPos);
    SelectionResult finishDrag(sf::Vector2f endPos);
    void cancelDrag();
    
    // Area selection (for tiles in FLOOR/ROOF modes, objects in OBJECTS mode)
    bool startAreaSelection(sf::Vector2f startPos, SelectionMode mode);
    void updateAreaSelection(sf::Vector2f currentPos);
    SelectionResult finishAreaSelection();
    void cancelAreaSelection();
    
    // Selection management
    void clearSelection();
    void selectAll(SelectionMode mode, int currentElevation);
    void invertSelection(SelectionMode mode, int currentElevation);
    
    // Getters
    const Selection& getCurrentSelection() const { return _currentSelection; }
    bool hasSelection() const { return !_currentSelection.isEmpty(); }
    bool isDragging() const { return _currentSelection.isDragging; }
    bool isAreaSelecting() const { return _currentSelection.selectionArea.has_value(); }
    
    // Observer pattern for UI updates
    void addObserver(std::weak_ptr<SelectionObserver> observer);
    void removeObserver(std::weak_ptr<SelectionObserver> observer);
    
    // Helper for external classes (like EditorWidget) to check collision
    bool isSpriteClicked(sf::Vector2f worldPos, const sf::Sprite& sprite) const;
    
    // Public access to notification (needed by SelectionBridge)
    void notifyObservers();
    
private:
    Map* _map;
    SelectionBridge* _bridge = nullptr;
    Selection _currentSelection;
    std::vector<std::weak_ptr<SelectionObserver>> _observers;
    
    // Tile selection helpers
    std::vector<std::shared_ptr<Object>> getObjectsAtPosition(sf::Vector2f worldPos, int elevation) const;
    std::optional<int> getRoofTileAtPosition(sf::Vector2f worldPos, int elevation) const;
    std::optional<int> getFloorTileAtPosition(sf::Vector2f worldPos, int elevation) const;
    
    // Area selection helpers
    std::vector<int> getTilesInArea(const sf::FloatRect& area, bool roof, int elevation) const;
    std::vector<std::shared_ptr<Object>> getObjectsInArea(const sf::FloatRect& area, int elevation) const;
    
    // Single item selection logic (current behavior)
    SelectionResult selectSingleAtPosition(sf::Vector2f worldPos, SelectionMode mode, int elevation);
    
    // Cycling logic for ALL mode (current behavior)
    SelectionResult cycleThroughItemsAtPosition(sf::Vector2f worldPos, int elevation);
    
    // Internal helpers
    void addItemToSelection(const SelectedItem& item);
    void removeItemFromSelection(const SelectedItem& item);
    bool isItemSelected(const SelectedItem& item) const;
    
    // Position calculations (will need access to sprite positioning logic)
    sf::Vector2f getTileWorldPosition(int tileIndex) const;
    bool isPositionInTile(sf::Vector2f worldPos, int tileIndex, bool roof) const;
};

} // namespace geck::selection