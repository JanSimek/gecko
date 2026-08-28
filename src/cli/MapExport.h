#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace geck {
namespace resource {
    class GameResources;
}

namespace cli {

    struct ExportOptions {
        /// Maps to walk (VFS paths, e.g. "maps/klamall.map"); empty = every mounted map.
        std::vector<std::string> maps;
        /// Also emit scenery and walls. Off by default: they are 98% of the records and nothing
        /// searches for a wall segment. See the note in exportEntities.
        bool includeScenery = false;
        /// Collapse each exit grid's hexes into one row per destination. A doorway is drawn as a
        /// patch of adjacent exit-grid hexes that all lead to the same place; ungrouped they are 71%
        /// of the index (28,767 rows across the shipped maps) and say nothing 423 rows do not.
        bool groupExits = true;
    };

    /// Walk the mounted maps and emit a flat, searchable index of everything a player might look
    /// for: items, critters and exit grids, each with the map, elevation and hex it sits on.
    ///
    /// The point of this over `analyze` / `dump_grid` is CONTAINER CONTENTS. Those report the objects
    /// standing on a map; this one recurses into inventories, so a pair of boots inside a locker is a
    /// row of its own, carrying the locker as its `holder` and the locker's hex as its position. An
    /// item in an inventory has no meaningful position of its own, so it inherits its holder's.
    ///
    /// Scenery and walls are excluded by default. Across the shipped maps they are roughly 98% of all
    /// object records, and the remaining 2% — items and critters — is what anyone actually searches.
    /// Emitting everything turns a few hundred KB of index into tens of MB for no gain.
    ///
    /// Emits one JSON object to `out`: { maps: [...], entities: [...] }. Returns 0 on success,
    /// non-zero if no maps were found. Maps that fail to parse are reported in `mapsUnreadable`
    /// rather than skipped silently — an absent item is only meaningful if that list is empty.
    int exportEntities(resource::GameResources& resources, const ExportOptions& options, std::ostream& out);

} // namespace cli
} // namespace geck
