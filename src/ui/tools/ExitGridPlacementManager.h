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

    // "Draw edge" finalize: place the FROZEN committed segments — every segment was captured (hexes +
    // directional art) at the click that closed it, so the placed edge is pixel-identical to the last
    // preview and each segment keeps the side it was committed with (the flip in effect then). If
    // existing exit grids already sit on every hex, their destination is bulk-edited instead (keeping
    // their art). The committed segments are reset by the caller (onMarkExitsLineReset) after this runs.
    void selectExitGridsAlongLine();

    // --- "Draw edge" TRUE-FREEZE state machine -----------------------------------------------------
    //
    // A committed segment is captured IMMUTABLY at the click that closes it: its hex list AND its art
    // (direction + side, using the flip in effect AT THAT MOMENT). Once captured it never recomputes,
    // so pressing Space or moving the cursor (incl. Shift-snap) can only touch the ONE live segment
    // (last committed vertex -> cursor). The committed segments live HERE (the manager already owns the
    // per-segment art logic); the InputHandler drives capture/reset because only it knows when a vertex
    // is committed and when the line resets.
    //
    // beginLine: start a fresh edge with NO committed segments (called when the first vertex is placed).
    void beginLine();
    // commitSegment: freeze the segment `from -> to` (its hexes + art at the current flip). Called on
    // every left-click that closes a segment (the 2nd vertex onward). A degenerate/off-grid segment
    // captures nothing, so the live preview can still extend from the last good vertex.
    void commitSegment(sf::Vector2f from, sf::Vector2f to, bool flipSide);
    // resetLine: drop all committed segments (line finalized OR cancelled / mode left).
    void resetLine();
    // The number of frozen committed segments — exposed for tests of the freeze state machine.
    [[nodiscard]] std::size_t committedSegmentCount() const { return _committedSegments.size(); }

    // Destination kind the live region preview tints by: inter-map (green) vs world/town map
    // (brown). Defaults to inter-map and is updated whenever a region dialog sets the destination,
    // so the next drawn region previews in the kind the user last chose.
    enum class DestinationKind {
        InterMap,
        WorldMap
    };
    DestinationKind currentDestinationKind() const { return _currentDestinationKind; }

    // The live "Draw edge" preview: the FROZEN committed segments (never recomputed) followed by the
    // ONE live segment (last committed vertex `liveFrom` -> cursor `liveTo`), deduped first-seen, with
    // the directional marker FRM for each hex. Committed segments keep the art they were captured with;
    // only the live segment is (re)classified here from its own screen direction (Auto, optionally
    // flipped) or the marker-direction override. `flipSide` therefore affects ONLY the live segment.
    // `hasLive` is false before the first vertex is committed (no segment to draw yet). The returned
    // vectors are parallel (hex i has frm i).
    struct LinePreview {
        std::vector<int> hexes;
        std::vector<uint32_t> frmPids;
    };
    [[nodiscard]] LinePreview previewForLine(sf::Vector2f liveFrom, sf::Vector2f liveTo, bool hasLive,
        bool flipSide) const;

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
    // createExitGridsForLine: show one dialog and create one exit grid on each FRESH hex (those in
    // `freshHexes`) via registerExitGridCreation, each carrying its own pre-classified per-segment art
    // (parallel to `hexes`). Returns the number created.
    std::size_t createExitGridsForLine(const std::vector<int>& hexes,
        const std::vector<ExitGridArt>& art, const std::set<int>& freshHexes);

    // One immutably-captured polyline segment: its gap-free hex run and the parallel per-hex art it was
    // frozen with (direction + side at the flip in effect when the closing vertex was clicked). The
    // hexes/art never recompute, so flips or cursor moves on later segments can't disturb it.
    struct CommittedSegment {
        std::vector<int> hexes;
        std::vector<ExitGridArt> art;
    };

    // Capture (classify) ONE world-space segment `from -> to` at `flipSide`: its hex-line walk and the
    // parallel per-hex art (Auto from the segment's own screen direction, optionally flipped, or the
    // marker-direction override). Returns an empty segment for a degenerate/off-grid pair. This is the
    // single capture used both to FREEZE a committed segment and to (re)compute the live segment.
    CommittedSegment classifySegment(sf::Vector2f from, sf::Vector2f to, bool flipSide) const;
    // The geometry (hexes + screen axis + outward facing) of one world-space segment, or std::nullopt
    // for a degenerate/off-grid pair. The per-segment classification input.
    std::optional<ExitGridSegmentRun> buildSegmentRun(sf::Vector2f from, sf::Vector2f to) const;
    // The art ({proto, frm}) for one classified segment run honouring the marker-direction override:
    // Auto -> the segment's own screen axis + outward side (optionally flipped); an explicit direction
    // -> that direction. Shared by the freeze and the live recompute so they agree hex-for-hex.
    ExitGridArt segmentArt(const ExitGridSegmentRun& run, bool flipSide) const;
    // Flatten the FROZEN committed segments followed by the live segment into deduped, parallel
    // (hexes, art) vectors: each hex appears once in first-seen order, keeping the art of the FIRST
    // segment that covered it. Because committed segments are flattened in commit order before the live
    // one, their hexes/art are pixel-stable regardless of how the live segment moves. `live` may be
    // empty (no live segment yet). Pure + static so the freeze/dedup is unit-testable.
    static void flattenSegments(const std::vector<CommittedSegment>& committed,
        const CommittedSegment& live, std::vector<int>& outHexes, std::vector<ExitGridArt>& outArt);
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
    // The FROZEN committed segments of the in-progress "Draw edge" line, captured one-per-closing-click.
    // Reset on line begin/finalize/cancel. Pixel-stable: never recomputed once captured.
    std::vector<CommittedSegment> _committedSegments;
};

} // namespace geck