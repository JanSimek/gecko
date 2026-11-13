#pragma once

#include <SFML/System/Vector2.hpp>
#include <memory>
#include <vector>

#include "editing/commands/ObjectCommandController.h"

class QWidget;

namespace geck {

class Object;
class Map;
class HexagonGrid;
class ViewportController;
class SFMLWidget;
struct MapObject;

namespace selection {
    class SelectionManager;
}

/**
 * @brief Narrow abstraction over the editor host that ExitGridPlacementManager depends on.
 *
 * It declares exactly the methods ExitGridPlacementManager needs from its host, so the
 * manager stays independent of EditorWidget and testable in isolation; EditorWidget
 * implements it.
 *
 * Several accessors (getSelectionManager, getCurrentElevation, getViewportController,
 * getObjectsAtPosition, getMap, getHexagonGrid) are also declared by SelectionDataProvider /
 * TilePlacementContext; EditorWidget's single override of each satisfies all interfaces
 * (identical signatures, separate non-shared bases => no ambiguity).
 */
class ExitGridContext {
public:
    virtual ~ExitGridContext() = default;

    virtual selection::SelectionManager* getSelectionManager() const = 0;
    virtual void clearSelection() = 0;
    virtual int getCurrentElevation() const = 0;
    virtual void registerExitGridEdit(const std::vector<std::shared_ptr<MapObject>>& exitGrids,
        const std::vector<ExitGridCommandState>& beforeStates,
        const std::vector<ExitGridCommandState>& afterStates)
        = 0;
    virtual void registerExitGridCreation(const std::vector<std::shared_ptr<MapObject>>& exitGrids, int elevation) = 0;
    virtual void refreshObjects() = 0;
    virtual std::vector<std::shared_ptr<Object>> getObjectsAtPosition(sf::Vector2f worldPos) = 0;
    virtual ViewportController* getViewportController() const = 0;
    virtual SFMLWidget* getSFMLWidget() const = 0;

    // Additional accessors the manager exercises beyond the core nine.
    virtual Map* getMap() const = 0;
    virtual const std::vector<std::shared_ptr<Object>>& getObjects() const = 0;
    virtual const HexagonGrid* getHexagonGrid() const = 0;

    // Parent widget for modal dialogs/message boxes raised by the manager.
    virtual QWidget* getDialogParent() = 0;
};

} // namespace geck
