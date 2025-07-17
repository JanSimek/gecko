#pragma once

#include <SFML/Graphics.hpp>
#include <memory>
#include <vector>
#include <optional>
#include <variant>
#include <functional>

#include "../util/Types.h"
#include "../util/SpatialIndex.h"
#include "../format/map/Map.h"
#include "../editor/Object.h"
#include "SelectionState.h"
#include <functional>

namespace geck {
    class EditorWidget; // Forward declaration
}

namespace geck::selection {

/**
 * @brief Result of a selection operation
 */
struct SelectionResult {
    bool success = false;
    bool selectionChanged = false;
    std::string message; // For error messages or status info
    
    static SelectionResult createSuccess(const std::string& msg = "") { return {true, true, msg}; }
    static SelectionResult createNoChange() { return {true, false, ""}; }
    static SelectionResult createError(const std::string& msg) { return {false, false, msg}; }
};

/**
 * @brief Manages core selection operations for tiles and objects
 * 
 * This class encapsulates core selection algorithms:
 * - Position-based selection logic
 * - Area selection calculations  
 * - Coordinate conversions
 * - Direct callback notification for UI updates
 */
class SelectionManager {
public:
    using SelectionCallback = std::function<void(const SelectionState&)>;
    
    explicit SelectionManager(Map* map, geck::EditorWidget* editorWidget);
    ~SelectionManager() = default;
    
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
    
    // State access
    const SelectionState& getCurrentSelection() const { return _state; }
    SelectionState& getMutableSelection() { return _state; }
    bool hasSelection() const { return !_state.isEmpty(); }
    bool isDragging() const { return _state.isDragging; }
    bool isAreaSelecting() const { return _state.isAreaSelecting(); }
    
    // Callback mechanism for UI updates
    void setSelectionCallback(SelectionCallback callback) { _selectionCallback = callback; }
    
    // Spatial index management
    void initializeSpatialIndex();
    
    // Helper for external classes (like EditorWidget) to check collision
    bool isSpriteClicked(sf::Vector2f worldPos, const sf::Sprite& sprite) const;
    
    // Area selection helpers (public for EditorWidget usage)
    std::vector<int> getTilesInArea(const sf::FloatRect& area, bool roof, int elevation) const;
    std::vector<int> getTilesInAreaIncludingEmpty(const sf::FloatRect& area, bool roof, int elevation) const;
    std::vector<std::shared_ptr<Object>> getObjectsInArea(const sf::FloatRect& area, int elevation) const;
    
private:
    Map* _map;
    geck::EditorWidget* _editorWidget = nullptr;
    SelectionState _state;
    SelectionCallback _selectionCallback;
    
    // Spatial indexing for O(1) area queries
    std::unique_ptr<TileSpatialIndex> _spatialIndex;
    
    // Tile selection helpers
    std::vector<std::shared_ptr<Object>> getObjectsAtPosition(sf::Vector2f worldPos, int elevation) const;
    std::optional<int> getRoofTileAtPosition(sf::Vector2f worldPos, int elevation) const;
    std::optional<int> getRoofTileAtPositionIncludingEmpty(sf::Vector2f worldPos, int elevation) const;
    std::optional<int> getFloorTileAtPosition(sf::Vector2f worldPos, int elevation) const;
    
    
    // Single item selection logic (current behavior)
    SelectionResult selectSingleAtPosition(sf::Vector2f worldPos, SelectionMode mode, int elevation);
    
    // Cycling logic for ALL mode (current behavior)
    SelectionResult cycleThroughItemsAtPosition(sf::Vector2f worldPos, int elevation);
    
    // Internal helpers (now work with state and notify via renderer)
    void addItemToSelection(const SelectedItem& item);
    void removeItemFromSelection(const SelectedItem& item);
    bool isItemSelected(const SelectedItem& item) const;
    
    // Notification helper
    void notifySelectionChanged();
    
    // Position calculations (will need access to sprite positioning logic)
    sf::Vector2f getTileWorldPosition(int tileIndex) const;
    bool isPositionInTile(sf::Vector2f worldPos, int tileIndex, bool roof) const;
    
    // Drag & drop implementation helpers
    bool moveObject(std::shared_ptr<Object> object, sf::Vector2f offset);
    bool moveTile(int sourceTileIndex, sf::Vector2f offset, bool isRoof);
};

} // namespace geck::selection