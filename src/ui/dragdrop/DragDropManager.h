#pragma once

#include <SFML/Graphics/View.hpp>
#include <SFML/System/Vector2.hpp>
#include <functional>
#include <memory>
#include <vector>
#include "../../util/Types.h"
#include "DragDropContext.h"

namespace geck {

class Object;
class HexagonGrid;
struct ObjectInfo;
enum class ObjectCategory;

/**
 * @brief Manages drag and drop operations for objects and palette items
 *
 * This class encapsulates all drag and drop functionality that was previously
 * scattered throughout EditorWidget, providing clean separation between
 * drag operations and editor logic.
 */
class DragDropManager {
public:
    /**
     * @brief Types of drag operations
     */
    enum class DragType {
        NONE,
        OBJECT_MOVE,    // Moving existing objects
        PALETTE_PREVIEW // Previewing object from palette
    };

    DragDropManager(DragDropContext& context,
        std::function<const ObjectInfo*(int objectIndex, ObjectCategory category)> lookupObjectInfo);
    ~DragDropManager() = default;

    // Object dragging (moving existing objects)
    bool canStartObjectDrag(sf::Vector2f worldPos) const;
    bool startObjectDrag(sf::Vector2f worldPos);
    void updateObjectDrag(sf::Vector2f currentWorldPos);
    void finishObjectDrag(sf::Vector2f finalWorldPos);
    void cancelObjectDrag();

    // Palette drag preview (placing new objects from palette)
    void startDragPreview(int objectIndex, int categoryInt, sf::Vector2f worldPos);
    void updateDragPreview(sf::Vector2f worldPos);
    void finishDragPreview(sf::Vector2f worldPos);
    void cancelDragPreview();

    // State queries
    bool isDraggingObjects() const { return _isDraggingObjects; }
    bool isDraggingFromPalette() const { return _isDraggingFromPalette; }
    DragType getCurrentDragType() const;

    // Access to drag state for rendering
    const std::vector<std::shared_ptr<Object>>& getDraggedObjects() const { return _draggedObjects; }
    const std::shared_ptr<Object>& getDragPreviewObject() const { return _dragPreviewObject; }
    sf::Vector2f getObjectDragOffset() const { return _objectDragOffset; }

private:
    DragDropContext& _context;
    std::function<const ObjectInfo*(int objectIndex, ObjectCategory category)> _lookupObjectInfo;

    // Object dragging state
    bool _isDraggingObjects = false;
    std::vector<std::shared_ptr<Object>> _draggedObjects;
    std::vector<sf::Vector2f> _objectDragStartPositions;
    sf::Vector2f _dragStartWorldPos;
    sf::Vector2f _objectDragOffset;

    // Palette drag preview state
    bool _isDraggingFromPalette = false;
    std::shared_ptr<Object> _dragPreviewObject;
    int _previewObjectIndex = -1;
    int _previewObjectCategory = 0;
    const ObjectInfo* _previewObjectInfo = nullptr;
};

} // namespace geck