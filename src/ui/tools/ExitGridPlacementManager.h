#pragma once

#include <SFML/Graphics.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
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

    // Finalize: place the FROZEN committed segments, so the placed edge matches the last preview. If
    // exit grids already cover every hex, bulk-edit their destination instead.
    void selectExitGridsAlongLine();

    // --- "Draw edge" true-freeze state machine ---
    // A committed segment is captured immutably at the click that closes it and never recomputes, so
    // Space or a cursor move touches only the one live segment. The manager owns the per-segment art;
    // the InputHandler drives capture and reset, since only it knows when a vertex commits.
    void beginLine();
    // commitSegment: freeze `from -> to` (hexes + art at the current flip), on every click that closes a
    // segment (2nd vertex on). A degenerate/off-grid segment captures nothing.
    void commitSegment(sf::Vector2f from, sf::Vector2f to, bool flipSide);
    // resetLine: drop all committed segments (line finalized / cancelled / mode left).
    void resetLine();
    // The number of frozen committed segments — exposed for tests.
    [[nodiscard]] std::size_t committedSegmentCount() const { return _committedSegments.size(); }

    // Destination kind the live preview tints by (green inter-map vs brown world/town). Updated whenever
    // a dialog sets the destination, so the next region previews in the kind last chosen.
    enum class DestinationKind {
        InterMap,
        WorldMap
    };
    DestinationKind currentDestinationKind() const { return _currentDestinationKind; }

    // The live preview: the frozen segments plus the one live segment, deduped first-seen, with each
    // hex's directional FRM. Only the live segment is reclassified, so `flipSide` affects only it.
    struct LinePreview {
        std::vector<int> hexes;
        std::vector<uint32_t> frmPids;
    };
    [[nodiscard]] LinePreview previewForLine(sf::Vector2f liveFrom, sf::Vector2f liveTo, bool hasLive,
        bool flipSide) const;

    /// The line hexes that don't already have an exit grid -- the ones a stroke CREATES on (empty when
    /// the whole stroke is already grids, so the caller bulk-edits instead). Pure + static for testing.
    [[nodiscard]] static std::vector<int> freshHexesForLine(const std::vector<int>& lineHexes,
        const std::set<int>& occupied);

private:
    // Create an exit-grid MISC object with the given directional art (proPid/frmPid) and the destination
    // fields from `properties`. The art is independent of the destination (which drives only exit_*).
    std::shared_ptr<MapObject> createExitGridObject(int hexPosition, uint32_t proPid, uint32_t frmPid,
        const ExitGridPropertiesDialog::ExitGridProperties& properties);

    // Show properties dialog and handle result
    bool showPropertiesDialog(ExitGridPropertiesDialog::ExitGridProperties& properties, const ExitGridPropertiesDialog::ExitGridProperties* existing = nullptr);

    // bulkEditExistingExitGrids: show one dialog (defaulted from the first object) and apply the chosen
    // destination to every passed exit grid via registerExitGridEdit. Returns true if a change committed.
    bool bulkEditExistingExitGrids(const std::vector<std::shared_ptr<Object>>& exitGrids);
    // createExitGridsForLine: show one dialog and create one exit grid on each FRESH hex (in
    // `freshHexes`), each carrying its own per-segment art (parallel to `hexes`). Returns the count.
    std::size_t createExitGridsForLine(const std::vector<int>& hexes,
        const std::vector<ExitGridArt>& art, const std::set<int>& freshHexes);

    // One immutably-captured polyline segment: its gap-free hex run and the parallel per-hex art it was
    // frozen with. Never recomputes, so flips/cursor moves on later segments can't disturb it.
    struct CommittedSegment {
        std::vector<int> hexes;
        std::vector<ExitGridArt> art;
    };

    // Capture one world-space segment at `flipSide`: its hex-line walk and the parallel per-hex art.
    // Empty for a degenerate or off-grid pair.
    CommittedSegment classifySegment(sf::Vector2f from, sf::Vector2f to, bool flipSide) const;
    // The geometry (hexes + screen axis + outward facing) of one segment, or nullopt for degenerate/
    // off-grid. The per-segment classification input.
    std::optional<ExitGridSegmentRun> buildSegmentRun(sf::Vector2f from, sf::Vector2f to) const;
    // The art for one classified segment run: Auto -> its own screen axis + side (optionally flipped),
    // or the marker-direction override. Shared by freeze and live recompute so they agree hex-for-hex.
    ExitGridArt segmentArt(const ExitGridSegmentRun& run, bool flipSide) const;
    // Flatten the frozen segments then the live one into deduped parallel vectors, first-seen winning,
    // so committed art stays pixel-stable as the live segment moves. Pure and static for testing.
    static void flattenSegments(const std::vector<CommittedSegment>& committed,
        const CommittedSegment& live, std::vector<int>& outHexes, std::vector<ExitGridArt>& outArt);
    // The existing exit-grid objects sitting on any of `hexPositions`.
    std::vector<std::shared_ptr<Object>> collectExitGridsOnHexes(const std::vector<int>& hexPositions) const;
    // One auto art for a whole stroke: axis from first to last hex, side once from the midpoint's
    // outward facing. `flipSide` inverts it; a lone hex falls back to the centre-facing classifier.
    ExitGridArt autoArtForLine(const std::vector<int>& orderedHexes, bool flipSide) const;
    // The art for a whole stroke honouring the override: Auto -> autoArtForLine (optionally flipped),
    // explicit -> that direction. Family from the destination kind. Shared by commit and preview.
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
    // Last marker-direction override chosen (drives the live preview art). Defaults to Auto.
    ExitGridPropertiesDialog::ExitGridProperties::MarkerArt _currentMarkerArt
        = ExitGridPropertiesDialog::ExitGridProperties::MarkerArt::Auto;
    // The FROZEN committed segments of the in-progress line, one per closing click; never recomputed.
    // Reset on line begin/finalize/cancel.
    std::vector<CommittedSegment> _committedSegments;
};

} // namespace geck