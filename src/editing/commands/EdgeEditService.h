#pragma once

#include <memory>
#include <optional>
#include <string>

#include "format/map/MapEdge.h"

namespace geck {

class Map;
class UndoBatcher;

/**
 * @brief Undoable editing of the `.edg` map-edge sidecar (Map::edge()).
 *
 * One of the aggregate services ObjectCommandController delegates to. Mirrors ScriptEditService's
 * snapshot pattern, but the snapshot unit is the whole `std::optional<MapEdge>` — a value type with
 * `operator==`, so a before/after copy is trivially correct and round-trip-safe.
 *
 * The engine's mapper (fallout2-ce `map_edge_setup.cc`) is the semantic reference: a zone side value
 * is a hex index, a new zone is seeded with the full-grid rect, and v2 adds the per-elevation
 * square-grid clip block. UX (click-select, drag) lives in the UI layer; this service only mutates
 * data and records undo.
 */
class EdgeEditService {
public:
    /// A side of a zone's tileRect (or the v2 squareRect). Matches the renderer's `activeEdgeSide`.
    enum Side { LEFT = 0,
        TOP = 1,
        RIGHT = 2,
        BOTTOM = 3 };

    EdgeEditService(std::unique_ptr<Map>& map, UndoBatcher& batcher);

    // --- Zone editing (undoable) ---

    /// Appends `seed` as a new zone on `elevation`, creating the edge if none exists yet. Returns the
    /// new zone's index, or -1 on failure. The caller supplies `seed` (e.g. mapEdgeFullGridZone()).
    int addZone(int elevation, const MapEdge::Rect& seed);
    /// Removes zone `zoneIndex` on `elevation`. Returns false if there is no edge or the index is out
    /// of range.
    bool deleteZone(int elevation, int zoneIndex);
    /// Sets one side of a zone's tileRect to a hex index (0..POSITION_COUNT-1). Returns false on bad
    /// args or when the value is unchanged.
    bool setZoneSide(int elevation, int zoneIndex, Side side, int hexIndex);

    // --- v2 square/clip editing (undoable) ---

    /// Marks the edge as version 2 so its per-elevation square/clip block is written. No-op if there
    /// is no edge or it is already v2.
    bool upgradeToVersion2();
    /// Sets one side of an elevation's squareRect (LEFT/RIGHT = column, TOP/BOTTOM = row; 0..99).
    bool setSquareSide(int elevation, Side side, int colOrRow);
    /// Flips one clip-side flag of an elevation's square block.
    bool toggleClipSide(int elevation, Side side);
    /// Restores an elevation's squareRect + clip flags to the engine defaults (map_edge.cc:229).
    bool resetSquare(int elevation);

    // --- Live drag support (mutates without recording undo until commit) ---

    /// A copy of the current edge, held by the UI as the "before" state across a drag gesture.
    std::optional<MapEdge> snapshot() const;
    /// Sets a zone side to a hex index for live preview, WITHOUT recording undo. Silently ignores bad
    /// args. Used each mouse-move of a side drag; the gesture is committed once via commitEdit().
    void previewZoneSide(int elevation, int zoneIndex, Side side, int hexIndex);
    /// Restores the edge to `before` WITHOUT recording undo (drag cancel).
    void restore(const std::optional<MapEdge>& before);
    /// Records the current edge as the result of a gesture that started at `before`, as one undo
    /// command. Records nothing and returns false if the edge is unchanged.
    bool commitEdit(const std::string& description, std::optional<MapEdge> before);

    // --- Reads ---

    const std::optional<MapEdge>& edge() const;
    bool hasEdge() const;
    int zoneCount(int elevation) const;

private:
    void applyEdgeSnapshot(const std::optional<MapEdge>& snapshot);
    /// Records the before/after snapshot of Map::edge() (already mutated by the caller) under
    /// `description`. Records nothing and returns false when the mutation left the edge unchanged.
    bool recordEdgeEdit(const std::string& description, std::optional<MapEdge> before);

    std::unique_ptr<Map>& _map;
    UndoBatcher& _batcher;
};

} // namespace geck
