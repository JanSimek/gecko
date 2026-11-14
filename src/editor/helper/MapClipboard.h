#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "editor/TileChange.h"
#include "format/map/Map.h"
#include "format/map/MapScript.h"

namespace geck {

struct MapObject;
namespace selection {
    class SelectionState;
}

/// An in-memory copy of part of a map (floor tiles, roof tiles and objects) for copy/cut/paste,
/// held relative to an anchor so it can be pasted anywhere: the same map, another elevation or
/// another open map. Pure data and logic (no Qt, no sprites); the editor builds the visuals and
/// commits a placement as one undo entry.
///
/// Objects are kept as deep copies (MapObject::cloneDeep, inventory included) taken at copy time,
/// so a paste never depends on the source map still being open. The copies are never placed
/// themselves: every place() clones them again, so repeated pastes never share objects.
class MapClipboard {
public:
    using TileSelection = std::pair<int, uint16_t>; ///< (tile index in tile space, tile id)

    /// What one object's copy runs: its script's section and program (scripts.lst index). Captured
    /// from the source map's script record, since the record itself does not travel with the copy.
    struct ScriptLink {
        int section = 0;
        uint32_t programIndex = 0;
    };

    struct ClipObject {
        int dCol = 0; ///< hex column offset from the anchor
        int dRow = 0; ///< hex row offset from the anchor
        /// Deep copy with no script linkage (map_scripts_pid == -1 throughout its inventory).
        std::shared_ptr<const MapObject> object;
        /// One slot per object in this copy, pre-order (the object, then its inventory depth first);
        /// set where that object had a script in the source map.
        std::vector<std::optional<ScriptLink>> scripts;
    };

    struct ClipTile {
        int dCol = 0; ///< tile column offset from the anchor's tile
        int dRow = 0; ///< tile row offset from the anchor's tile
        uint16_t tileId = 0;
    };

    /// Where each entry lands for a given target, index-aligned with objects()/floor()/roof();
    /// -1 marks an entry that falls off the grid.
    struct Resolved {
        int anchorHex = -1; ///< the snapped target the anchor lands on
        std::vector<int> objectHexes;
        std::vector<int> floorTiles;
        std::vector<int> roofTiles;
    };

    /// A built, uncommitted paste. Nothing in the map changes until the editor commits it.
    struct Placement {
        struct Entry {
            std::shared_ptr<MapObject> object;
            /// New records for this object and any scripted inventory items, owned through their
            /// fresh SID/OID. Commit them only if the object itself is placed.
            std::vector<MapScript> scripts;
        };
        std::vector<Entry> objects;
        std::vector<TileChange> tiles; ///< before read from the target map, after = clipboard id
        int objectsDropped = 0;        ///< objects whose target hex is off the grid
        int tilesDropped = 0;          ///< tiles whose target is off the tile grid

        [[nodiscard]] std::size_t scriptCount() const;
    };

    /// Replace the contents with `objects` and the given floor/roof tiles. Objects are deep-copied;
    /// floor/roof squares holding Map::EMPTY_TILE are skipped so a paste never punches holes in what
    /// is under it. `sourceMap` resolves each object's script to its program. The anchor is the
    /// centre of the copied area's hex bounds, snapped to an even column and row so tiles stay
    /// aligned. Returns false (and leaves the clipboard empty) when there was nothing to copy.
    bool capture(const std::vector<const MapObject*>& objects,
        const std::vector<TileSelection>& floorTiles,
        const std::vector<TileSelection>& roofTiles,
        const Map::MapFile& sourceMap);

    /// capture() from a live selection on `elevation`: its objects plus its floor and roof tiles
    /// (ids read from `sourceMap`). Hex selections are ignored.
    bool captureSelection(const selection::SelectionState& selection, const Map::MapFile& sourceMap, int elevation);

    void clear();
    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] int anchorHex() const { return _anchorHex; }
    [[nodiscard]] const std::vector<ClipObject>& objects() const { return _objects; }
    [[nodiscard]] const std::vector<ClipTile>& floor() const { return _floor; }
    [[nodiscard]] const std::vector<ClipTile>& roof() const { return _roof; }

    /// Snap a target hex to an even column and row, the parity of every clipboard anchor, the same
    /// way PatternStamper::plan snaps a stamp target. Placement therefore moves in 2-hex steps and
    /// tiles stay locked to the objects.
    [[nodiscard]] static int snapTarget(int targetHex);

    /// Where each entry lands when the anchor is dropped near `targetHex`. Pure.
    [[nodiscard]] Resolved resolve(int targetHex) const;

    /// Build a paste near `targetHex` on `elevation` of `targetMap`, mutating nothing. Each object is
    /// a fresh deep clone, translated, moved to `elevation`, given a new unique object id and no
    /// combat id (critter_index -1, like any newly placed object). Every copy that had a script gets
    /// a new record running the same program in the same section, with a new SID and its owner's
    /// new id as OID. Exit grid and stair/ladder destinations are kept as they are. Off-grid entries
    /// are dropped and counted.
    [[nodiscard]] Placement place(int targetHex, int elevation, const Map::MapFile& targetMap) const;

private:
    std::vector<ClipObject> _objects;
    std::vector<ClipTile> _floor;
    std::vector<ClipTile> _roof;
    int _anchorHex = 0;
};

} // namespace geck
