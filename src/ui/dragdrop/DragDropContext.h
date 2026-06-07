#pragma once

#include <SFML/System/Vector2.hpp>
#include <memory>
#include <utility>
#include <vector>

namespace geck {

class Object;
class HexagonGrid;
class ViewportController;

namespace resource {
    class GameResources;
}

namespace selection {
    class SelectionManager;
}

/**
 * @brief Narrow abstraction over the editor host that DragDropManager depends on.
 *
 * DragDropManager used to hold a raw EditorWidget* (and include EditorWidget.h),
 * which coupled drag and drop to the full editor and made the manager untestable in
 * isolation. This interface declares exactly the methods DragDropManager needs from its
 * host; EditorWidget implements it.
 *
 * Several accessors (getViewportController, getHexagonGrid, getSelectionManager,
 * getObjectsAtPosition) are also declared by SelectionDataProvider / TilePlacementContext;
 * EditorWidget's single override of each satisfies all interfaces (identical signatures,
 * separate non-shared bases => no ambiguity).
 */
class DragDropContext {
public:
    virtual ~DragDropContext() = default;

    virtual ViewportController* getViewportController() const = 0;
    virtual int& getCurrentHoverHex() = 0;
    virtual resource::GameResources& resources() const = 0;
    virtual const HexagonGrid* getHexagonGrid() const = 0;
    virtual selection::SelectionManager* getSelectionManager() const = 0;
    virtual void registerObjectMove(const std::vector<std::shared_ptr<Object>>& objects, const std::vector<std::pair<int, int>>& moves) = 0;
    virtual void placeObjectAtPosition(sf::Vector2f worldPos) = 0;
    virtual std::vector<std::shared_ptr<Object>> getObjectsAtPosition(sf::Vector2f worldPos) = 0;
    virtual void clearDragSelectionPreview() = 0;
};

} // namespace geck
