#pragma once

#include <SFML/Graphics.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <QString>

#include "ui/dialogs/ExitGridPropertiesDialog.h"
#include "ExitGridContext.h"

namespace geck {

// Forward declarations
class Map;
struct MapObject;

namespace resource {
    class GameResources;
}

/**
 * @brief Manages exit grid placement operations for the map editor
 *
 * This class handles:
 * - Exit grid placement at cursor position
 * - Properties dialog management
 * - Exit grid placement mode state
 * - MISC object creation for exit grids
 */
class ExitGridPlacementManager {
public:
    ExitGridPlacementManager(ExitGridContext& context, resource::GameResources& resources,
        std::function<void(const QString&)> showStatus);
    ~ExitGridPlacementManager() = default;

    // Exit grid placement operations
    void placeExitGridAtPosition(sf::Vector2f worldPos);
    void editExitGridProperties(std::shared_ptr<MapObject> exitGrid);

    // Exit grid placement mode control
    void setExitGridPlacementMode(bool enabled);
    void setMarkExitsMode(bool enabled);

    // State queries
    bool isExitGridPlacementMode() const { return _exitGridPlacementMode; }
    bool isMarkExitsMode() const { return _markExitsMode; }

    // Callback interface for triggering exit grid placement from input events
    void handleExitGridPlacement(sf::Vector2f worldPos);

    // Reset all exit grid placement state
    void resetState();

    // Check if selected objects contain exit grids and edit them
    bool editSelectedExitGrids();

    // Mark exits mode - select and edit exit grids
    void handleMarkExitsSelection(sf::Vector2f worldPos);
    void selectExitGridsInArea(sf::Vector2f startPos, sf::Vector2f endPos);
    // Polygon "Draw region" mode: every hex whose world center lies inside the polygon (described
    // by its world-space vertices) becomes an exit grid sharing one destination — or, if existing
    // exit grids fall inside, those are bulk-edited instead (mirrors selectExitGridsInArea).
    void selectExitGridsInPolygon(const std::vector<sf::Vector2f>& worldVertices);

    // Destination kind the live region preview tints by: inter-map (green) vs world/town map
    // (brown). Defaults to inter-map and is updated whenever a region dialog sets the destination,
    // so the next drawn region previews in the kind the user last chose.
    enum class DestinationKind {
        InterMap,
        WorldMap
    };
    DestinationKind currentDestinationKind() const { return _currentDestinationKind; }

private:
    // Create exit grid MISC object with properties
    std::shared_ptr<MapObject> createExitGridObject(int hexPosition, const ExitGridPropertiesDialog::ExitGridProperties& properties);

    // Show properties dialog and handle result
    bool showPropertiesDialog(ExitGridPropertiesDialog::ExitGridProperties& properties, const ExitGridPropertiesDialog::ExitGridProperties* existing = nullptr);

    // Shared region logic, factored out so selectExitGridsInArea and selectExitGridsInPolygon
    // don't copy-paste the dialog/apply/create flow.
    //
    // bulkEditExistingExitGrids: show one dialog (defaulted from the first object) and apply the
    // chosen destination to every passed exit grid via registerExitGridEdit. Returns true if a
    // change was committed.
    bool bulkEditExistingExitGrids(const std::vector<std::shared_ptr<Object>>& exitGrids);
    // createExitGridsForHexes: show one dialog and create one exit grid per hex via
    // registerExitGridCreation. Returns the number created.
    std::size_t createExitGridsForHexes(const std::vector<int>& hexPositions);
    // The per-hex point-in-polygon collection, split out of selectExitGridsInPolygon to keep its
    // complexity down: existing exit grids whose hex center is inside the polygon, and the indices of
    // every interior hex.
    std::vector<std::shared_ptr<Object>> collectExitGridsInPolygon(const std::vector<sf::Vector2f>& worldVertices) const;
    std::vector<int> collectHexesInPolygon(const std::vector<sf::Vector2f>& worldVertices) const;

    // Track the destination kind from a dialog's chosen exit map (drives the preview tint).
    void rememberDestinationKind(uint32_t exitMap);

    ExitGridContext& _context;
    resource::GameResources& _resources;
    std::function<void(const QString&)> _showStatus;

    // State
    bool _exitGridPlacementMode = false;
    bool _markExitsMode = false;
    DestinationKind _currentDestinationKind = DestinationKind::InterMap;
};

} // namespace geck