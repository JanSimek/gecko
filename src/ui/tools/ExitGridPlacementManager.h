#pragma once

#include <SFML/Graphics.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <QString>

#include "ui/dialogs/ExitGridPropertiesDialog.h"
#include "util/ExitGridDirection.h"
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
    // "Draw edge" mode: walk a gap-free hex line through the polyline of world-space vertices; every
    // hex on the line becomes an exit grid sharing one destination, each with the directional art for
    // its outward facing — or, if existing exit grids sit on those hexes, those are bulk-edited
    // instead (mirrors selectExitGridsInArea).
    void selectExitGridsAlongLine(const std::vector<sf::Vector2f>& worldVertices);

    // Destination kind the live region preview tints by: inter-map (green) vs world/town map
    // (brown). Defaults to inter-map and is updated whenever a region dialog sets the destination,
    // so the next drawn region previews in the kind the user last chose.
    enum class DestinationKind {
        InterMap,
        WorldMap
    };
    DestinationKind currentDestinationKind() const { return _currentDestinationKind; }

private:
    // Create exit grid MISC object with the given directional art (proPid/frmPid, chosen per hex by
    // its outward facing) and the destination fields from `properties`. The art is independent of the
    // destination — the destination drives only the exit_* fields, not which marker is drawn.
    std::shared_ptr<MapObject> createExitGridObject(int hexPosition, uint32_t proPid, uint32_t frmPid,
        const ExitGridPropertiesDialog::ExitGridProperties& properties);

    // Show properties dialog and handle result
    bool showPropertiesDialog(ExitGridPropertiesDialog::ExitGridProperties& properties, const ExitGridPropertiesDialog::ExitGridProperties* existing = nullptr);

    // Shared region logic, factored out so selectExitGridsInArea and selectExitGridsAlongLine
    // don't copy-paste the dialog/apply/create flow.
    //
    // bulkEditExistingExitGrids: show one dialog (defaulted from the first object) and apply the
    // chosen destination to every passed exit grid via registerExitGridEdit. Returns true if a
    // change was committed.
    bool bulkEditExistingExitGrids(const std::vector<std::shared_ptr<Object>>& exitGrids);
    // createExitGridsForHexes: show one dialog and create one exit grid per hex via
    // registerExitGridCreation, each with the directional art for its outward facing. Returns the
    // number created.
    std::size_t createExitGridsForHexes(const std::vector<int>& hexPositions);
    // The deduped, gap-free hex line through `worldVertices`: each vertex is mapped to its hex and
    // consecutive hexes are joined by a hex-line walk. Split out of selectExitGridsAlongLine.
    std::vector<int> collectHexesAlongLine(const std::vector<sf::Vector2f>& worldVertices) const;
    // The existing exit-grid objects sitting on any of `hexPositions`.
    std::vector<std::shared_ptr<Object>> collectExitGridsOnHexes(const std::vector<int>& hexPositions) const;
    // The directional exit-grid art (proto + frm) for `hexPosition`, picked by which way that hex
    // faces away from the map centre. Returns the map-exit art if the hex is off-grid.
    ExitGridArt directionalArtForHex(int hexPosition) const;

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