#pragma once

#include <SFML/Graphics.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
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
    // hex on the line becomes an exit grid sharing one destination AND one directional art — the side
    // is classified once for the whole stroke (its overall screen axis + outward facing), so the bars
    // form a clean continuous edge on a single consistent side rather than flipping mid-run. `flipSide`
    // inverts that side (the live "flip" key). If existing exit grids sit on those hexes, those have
    // their destination bulk-edited instead (keeping their directional art).
    void selectExitGridsAlongLine(const std::vector<sf::Vector2f>& worldVertices, bool flipSide = false);

    // Destination kind the live region preview tints by: inter-map (green) vs world/town map
    // (brown). Defaults to inter-map and is updated whenever a region dialog sets the destination,
    // so the next drawn region previews in the kind the user last chose.
    enum class DestinationKind {
        InterMap,
        WorldMap
    };
    DestinationKind currentDestinationKind() const { return _currentDestinationKind; }

    // The live "Draw edge" preview for a polyline of world-space vertices (the committed vertices plus
    // the live cursor as the last point): the deduped gap-free hex run AND the directional marker FRM
    // for each of those hexes, classified PER SEGMENT. Each polyline segment (vertex[i] -> vertex[i+1])
    // is classified once from its own screen direction (Auto) or forced by the marker-direction
    // override, and that art is applied to all of the segment's hexes; a shared hex keeps its
    // first-seen segment's art. So an earlier, committed segment's art is FROZEN — only the live
    // segment (last vertex -> cursor) changes as the cursor moves. `flipSide` inverts the side on every
    // segment (the live "flip" key). The returned vectors are parallel (hex i has frm i).
    struct LinePreview {
        std::vector<int> hexes;
        std::vector<uint32_t> frmPids;
    };
    [[nodiscard]] LinePreview previewForLine(const std::vector<sf::Vector2f>& worldVertices,
        bool flipSide = false) const;

    /// The line hexes that don't already have an exit grid -- the ones a stroke CREATES on (empty when
    /// the whole stroke is already grids, so the caller bulk-edits an existing edge). Pure + static so
    /// the partial-overlap placement decision is unit-testable without a dialog/context.
    [[nodiscard]] static std::vector<int> freshHexesForLine(const std::vector<int>& lineHexes,
        const std::set<int>& occupied);

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
    // createExitGridsForLine: show one dialog, classify the polyline PER SEGMENT, and create one exit
    // grid on each FRESH hex (those in `freshHexes`) via registerExitGridCreation, each carrying its
    // own segment's directional art (optionally flipped). Returns the number created.
    std::size_t createExitGridsForLine(const std::vector<sf::Vector2f>& worldVertices,
        const std::set<int>& freshHexes, bool flipSide);
    // The deduped, gap-free hex line through `worldVertices`: each vertex is mapped to its hex and
    // consecutive hexes are joined by a hex-line walk. Split out of selectExitGridsAlongLine.
    std::vector<int> collectHexesAlongLine(const std::vector<sf::Vector2f>& worldVertices) const;
    // The per-segment runs of `worldVertices`: one ExitGridSegmentRun per consecutive vertex pair,
    // each carrying its hex-line walk, its screen delta (for the axis) and its midpoint outward facing
    // (for the side). Drives the PER-SEGMENT classification used by both the preview and the commit so
    // each segment gets its own art (frozen once committed) instead of one art for the whole stroke.
    std::vector<ExitGridSegmentRun> buildSegmentRuns(const std::vector<sf::Vector2f>& worldVertices) const;
    // The per-hex directional art ({proto, frm}) for a polyline, classified PER SEGMENT: parallel to
    // `outHexes`. An explicit marker-direction override forces one direction on every hex; Auto
    // classifies each segment from its own screen direction. `flipSide` inverts the side on every
    // segment. Shared by previewForLine and the commit so they agree hex-for-hex.
    void perSegmentArt(const std::vector<sf::Vector2f>& worldVertices, bool flipSide,
        std::vector<int>& outHexes, std::vector<ExitGridArt>& outArt) const;
    // The existing exit-grid objects sitting on any of `hexPositions`.
    std::vector<std::shared_ptr<Object>> collectExitGridsOnHexes(const std::vector<int>& hexPositions) const;
    // The SINGLE auto (non-override) directional art for a whole drawn stroke: classify the stroke's
    // overall screen axis (first hex -> last hex) and pick the side once from the stroke midpoint's
    // outward facing, so every hex shares one consistent side. `flipSide` inverts that side. A lone hex
    // (or no grid) falls back to the center-facing classifier. Family comes from the destination kind.
    ExitGridArt autoArtForLine(const std::vector<int>& orderedHexes, bool flipSide) const;
    // The exit-grid art for a whole stroke honouring the marker-direction override: Auto -> the single
    // whole-stroke side (autoArtForLine, optionally flipped); an explicit direction -> that direction's
    // art. Family (green/brown) comes from the destination kind. Used by both the commit and the
    // preview so they agree.
    ExitGridArt artForLine(const std::vector<int>& orderedHexes,
        ExitGridPropertiesDialog::ExitGridProperties::MarkerArt markerArt, bool flipSide) const;
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