#pragma once

#include <SFML/Graphics.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
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

    // "Draw edge" mode: walk a gap-free hex line through the polyline of world-space vertices; every
    // hex on the line becomes an exit grid sharing one destination, each with the directional art for
    // its local segment — or, if existing exit grids sit on those hexes, those have their destination
    // bulk-edited instead (keeping their directional art).
    void selectExitGridsAlongLine(const std::vector<sf::Vector2f>& worldVertices);

    // Destination kind the live region preview tints by: inter-map (green) vs world/town map
    // (brown). Defaults to inter-map and is updated whenever a region dialog sets the destination,
    // so the next drawn region previews in the kind the user last chose.
    enum class DestinationKind {
        InterMap,
        WorldMap
    };
    DestinationKind currentDestinationKind() const { return _currentDestinationKind; }

    // The directional marker FRM the live preview should draw for `hexPosition`, using the same art
    // selection a commit would: the last-chosen marker-direction override (Auto -> the hex's drawn
    // segment + outward facing, an explicit direction -> that direction's art for every hex). segDx/
    // segDy are the hex's local screen-space segment direction on the previewed line (0,0 = lone hex).
    [[nodiscard]] uint32_t previewFrmPidForHex(int hexPosition, int segDx, int segDy) const;

    // The per-hex directional marker FRM for an ordered line of hexes, parallel to `orderedHexes`,
    // each picked from its own local segment direction — exactly what createExitGridsForHexes commits.
    // Lets the live preview show the same per-segment art the placement will produce.
    [[nodiscard]] std::vector<uint32_t> previewFrmPidsForLine(const std::vector<int>& orderedHexes) const;

private:
    // Create exit grid MISC object with the given directional art (proPid/frmPid, chosen per hex by
    // its outward facing) and the destination fields from `properties`. The art is independent of the
    // destination — the destination drives only the exit_* fields, not which marker is drawn.
    std::shared_ptr<MapObject> createExitGridObject(int hexPosition, uint32_t proPid, uint32_t frmPid,
        const ExitGridPropertiesDialog::ExitGridProperties& properties);

    // Show properties dialog and handle result
    bool showPropertiesDialog(ExitGridPropertiesDialog::ExitGridProperties& properties, const ExitGridPropertiesDialog::ExitGridProperties* existing = nullptr);

    // Shared edge-line logic for selectExitGridsAlongLine: bulk-edit existing exit grids on the line,
    // or create one per line hex.
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
    // The screen-space (dx, dy) of each ordered line hex's local segment (the direction the line runs
    // through that hex), used to classify its directional art. Endpoints take their single neighbour's
    // direction; a lone hex is (0, 0).
    std::vector<std::pair<int, int>> segmentDirectionsForLine(const std::vector<int>& orderedHexes) const;
    // The auto (non-override) directional art for `hexPosition` from its drawn segment (segDx/segDy in
    // screen space; 0,0 = lone hex) and destination kind. Falls back to a deterministic bottom marker
    // if the hex is off-grid.
    ExitGridArt autoArtForHex(int hexPosition, int segDx, int segDy) const;
    // The exit-grid art for `hexPosition` honouring the marker-direction override: Auto keeps the
    // per-hex segment+facing classification (autoArtForHex); an explicit direction forces that
    // direction's art for every hex on the line. Family (green/brown) comes from the destination kind.
    ExitGridArt artForHex(int hexPosition, ExitGridPropertiesDialog::ExitGridProperties::MarkerArt markerArt,
        int segDx, int segDy) const;
    // The art family (green inter-map vs brown world/town) for the destination the user last chose.
    ExitGridDestinationKind destinationKind() const;

    // Track the destination kind from a dialog's chosen exit map (drives the preview tint).
    void rememberDestinationKind(uint32_t exitMap);

    ExitGridContext& _context;
    resource::GameResources& _resources;
    std::function<void(const QString&)> _showStatus;

    // State
    bool _exitGridPlacementMode = false;
    bool _markExitsMode = false;
    DestinationKind _currentDestinationKind = DestinationKind::InterMap;
    // Last marker-direction override the user chose (drives the live preview art). Defaults to Auto
    // so the first drawn region previews each hex by its outward facing.
    ExitGridPropertiesDialog::ExitGridProperties::MarkerArt _currentMarkerArt
        = ExitGridPropertiesDialog::ExitGridProperties::MarkerArt::Auto;
};

} // namespace geck