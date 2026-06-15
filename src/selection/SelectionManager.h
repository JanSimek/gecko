#pragma once

#include <SFML/Graphics.hpp>
#include <memory>
#include <vector>
#include <optional>
#include <utility>
#include <variant>
#include <functional>

#include "util/Types.h"
#include "util/SpatialIndex.h"
#include "format/map/Map.h"
#include "editor/Object.h"
#include "editor/TileChange.h"
#include "SelectionState.h"
#include "SelectionDataProvider.h"

namespace geck::selection {

/**
 * @brief Result of a selection operation
 */
struct SelectionResult {
    bool success = false;
    bool selectionChanged = false;
    std::string message; // For error messages or status info

    static SelectionResult createSuccess(const std::string& msg = "") { return { true, true, msg }; }
    static SelectionResult createNoChange() { return { true, false, "" }; }
    static SelectionResult createError(const std::string& msg) { return { false, false, msg }; }
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

    explicit SelectionManager(SelectionDataProvider& provider);
    ~SelectionManager() = default;

    // Selection operations
    SelectionResult selectAtPosition(sf::Vector2f worldPos, SelectionMode mode, int currentElevation);
    SelectionResult selectArea(const sf::FloatRect& area, SelectionMode mode, int currentElevation);
    // Alt+drag: add the covered items to the current selection (does not clear first).
    SelectionResult addArea(const sf::FloatRect& area, SelectionMode mode, int currentElevation);
    // Ctrl+drag: removes the covered items that are already selected; never adds. Hidden
    // roof tiles are kept (a layer you cannot see must not be deselected).
    SelectionResult deselectArea(const sf::FloatRect& area, SelectionMode mode, int currentElevation);
    // The covered items a Ctrl+drag would remove (selected and on a visible layer). Lets the
    // editor preview the deselection live without mutating the selection.
    std::vector<SelectedItem> itemsToDeselectInArea(const sf::FloatRect& area, SelectionMode mode, int currentElevation) const;
    SelectionResult addToSelection(sf::Vector2f worldPos, SelectionMode mode, int currentElevation);
    // Ctrl+click: removes the topmost visible selected layer under the cursor; never adds.
    SelectionResult deselectAtPosition(sf::Vector2f worldPos, SelectionMode mode, int currentElevation);

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
    bool isAreaSelecting() const { return _state.isAreaSelecting(); }

    // Replace the selected items wholesale and notify (used to make the selection follow a move).
    void setSelectedItems(std::vector<SelectedItem> items);

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
    std::vector<int> getHexesInArea(const sf::FloatRect& area) const;

    // Move the selected floor and roof tiles by a whole-tile delta, as a set of tile edits (each
    // moved tile vacates its source to Map::EMPTY_TILE and fills its target) the editor applies
    // through the shared tile-edit undo path. Empty if there is nothing to move or any tile would
    // leave the map (the block moves as a whole or not at all). Pure: computes the change set without
    // mutating the map, and is block-safe — every source is vacated before any target is filled, so
    // overlapping moves never corrupt.
    std::vector<TileChange> planSelectionTileMove(int deltaRow, int deltaColumn) const;
    // As above, deriving the whole-tile delta from a world-space translation (the snapped movement
    // the dragged objects made), so the tiles land aligned with the objects.
    std::vector<TileChange> planSelectionMoveForTranslation(sf::Vector2f worldTranslation) const;
    // The world-space translation of moving the selection by whole tiles nearest rawTranslation.
    // Moving both the dragged objects and the tiles by this keeps them aligned to the tile grid
    // (objects snap to a finer hex grid, so an unaligned translation drifts them off the tiles).
    // nullopt when no tiles are selected (then the caller should keep the raw per-object movement).
    std::optional<sf::Vector2f> tileAlignedTranslation(sf::Vector2f rawTranslation) const;
    // The whole-tile (row, column) delta the selection moves by for a world translation; nullopt
    // when no tiles are selected. Used to shift the selection's tile items so it follows the move.
    std::optional<std::pair<int, int>> selectionTileDelta(sf::Vector2f worldTranslation) const;

private:
    SelectionDataProvider& _provider;
    SelectionState _state;
    SelectionCallback _selectionCallback;

    // Spatial index for efficient area queries
    std::unique_ptr<TileSpatialIndex> _spatialIndex;

    // Tile selection helpers
    std::vector<std::shared_ptr<Object>> getObjectsAtPosition(sf::Vector2f worldPos, int elevation) const;
    std::optional<int> getRoofTileAtPosition(sf::Vector2f worldPos, int elevation) const;
    std::optional<int> getRoofTileAtPositionIncludingEmpty(sf::Vector2f worldPos, int elevation) const;
    std::optional<int> getFloorTileAtPosition(sf::Vector2f worldPos, int elevation) const;

    // Tile-move helpers (see planSelectionTileMove). A selected tile used as the move reference
    // (prefer a floor tile): {tileIndex, isRoof}. Its centre round-trips through the tile hit-test.
    std::optional<std::pair<int, bool>> selectionTileReference() const;
    // Appends the block-safe tile edits for moving one layer's selected tiles by the delta. roof
    // picks roof vs floor; returns false (and appends nothing) if any tile would leave the map.
    bool appendTileLayerMove(std::vector<TileChange>& out, bool roof, int deltaRow, int deltaColumn) const;

    // Single item selection logic (current behavior)
    SelectionResult selectSingleAtPosition(sf::Vector2f worldPos, SelectionMode mode, int elevation);

    // Cycling logic for ALL mode (current behavior)
    SelectionResult cycleThroughItemsAtPosition(sf::Vector2f worldPos, int elevation);

    // Collects the items a drag-area covers for the given mode (shared by selectArea/deselectArea).
    std::vector<SelectedItem> collectItemsInArea(const sf::FloatRect& area, SelectionMode mode, int elevation) const;

    // Per-category appenders used by collectItemsInArea (keep its branching shallow).
    void appendTilesInArea(std::vector<SelectedItem>& items, const sf::FloatRect& area, bool roof, int elevation, bool includeEmpty) const;
    void appendObjectsInArea(std::vector<SelectedItem>& items, const sf::FloatRect& area, int elevation) const;
    void appendHexesInArea(std::vector<SelectedItem>& items, const sf::FloatRect& area) const;

    // Visible, selectable layers at a point in priority order (roof -> objects -> floor
    // for ALL mode). Used by deselectAtPosition so a Ctrl+click removes whichever visible
    // layer is actually selected. Hidden roofs are skipped so they stay selected.
    std::vector<SelectedItem> collectDeselectableAtPosition(sf::Vector2f worldPos, SelectionMode mode, int elevation) const;

    // Per-category appenders used by collectDeselectableAtPosition (keep its branching shallow).
    // appendRoofCandidate is a no-op while the roof layer is hidden so hidden roofs stay selected.
    void appendRoofCandidate(std::vector<SelectedItem>& candidates, std::optional<int> tileIndex) const;
    void appendObjectCandidates(std::vector<SelectedItem>& candidates, sf::Vector2f worldPos, int elevation) const;
    void appendHexCandidate(std::vector<SelectedItem>& candidates, sf::Vector2f worldPos) const;

    // Selection helpers
    void addItemToSelection(const SelectedItem& item);
    void removeItemFromSelection(const SelectedItem& item);
    bool isItemSelected(const SelectedItem& item) const;

    // Notification helper
    void notifySelectionChanged();
};

} // namespace geck::selection
