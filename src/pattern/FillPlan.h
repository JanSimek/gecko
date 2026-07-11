#pragma once

#include <memory>
#include <vector>

#include "editor/TileChange.h"

namespace geck {
class Object;
struct MapObject;
} // namespace geck

namespace geck::pattern {

/// A fully-resolved, fully-built edit that has NOT been committed to the map: every object is
/// already a constructed MapObject (and, when sprites are built, its visual Object), and every
/// tile is a ready TileChange with its before/after captured at plan time. Producing a FillPlan
/// mutates nothing — PlacementBatch::replay() applies it as a single undo entry.
///
/// This separation is what lets a preview be byte-identical to the apply (record once into a plan,
/// replay to commit — no second run, so a seeded/noise-based fill cannot drift between the two) and
/// what collapses a whole stamp/fill into one Ctrl-Z under the UndoStack command cap.
struct FillPlan {
    /// One placed object: its map data plus, when buildSprites is true, the visual Object. The
    /// Object is null in headless/data-only mode (replay then records the MapObject as data) and
    /// also when GUI art could not be resolved (replay then counts the entry as failed).
    struct Entry {
        std::shared_ptr<MapObject> mapObject;
        std::shared_ptr<Object> object; ///< null when sprites are not built or art failed to load
    };

    std::vector<Entry> objects;
    std::vector<TileChange> tiles;
    int dropped = 0; ///< objects/tiles refused at record time: off-grid targets, or surplus past the per-run sink cap
};

} // namespace geck::pattern
