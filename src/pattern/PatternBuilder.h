#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "pattern/Pattern.h"

namespace geck {
struct MapObject;
class Map;
namespace selection {
    class SelectionState;
}
} // namespace geck

namespace geck::pattern {

/// Captures a selection into a pattern (the inverse of PatternStamper). Object hexes and
/// tile indices become offsets relative to an anchor hex, so the result can be stamped
/// elsewhere. The capture is verbatim (engine IDs preserved); orientation variants are
/// authored by capturing the same prefab in different facings.
class PatternBuilder {
public:
    using TileSelection = std::pair<int, uint16_t>; ///< (tile index in tile space, tile id)

    /// Build one variant from selected objects and tiles, as offsets from `anchorHex`.
    /// Pure — no map/resource access (tile ids are supplied by the caller).
    static PatternVariant buildVariant(
        const std::vector<const MapObject*>& objects,
        const std::vector<TileSelection>& floorTiles,
        const std::vector<TileSelection>& roofTiles,
        int anchorHex,
        const std::string& label);

    /// Build a single-variant pattern from a live selection: extract objects, read the
    /// selected tiles' ids from the map's `elevation`, and derive an anchor (the first
    /// object's hex, else the first floor/roof tile's hex). Returns std::nullopt when the
    /// selection has neither objects nor tiles.
    static std::optional<Pattern> fromSelection(const selection::SelectionState& selection,
        Map& map,
        int elevation,
        const std::string& name);
};

} // namespace geck::pattern
