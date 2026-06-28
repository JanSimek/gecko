#include "pattern/PlacementBatch.h"

#include "pattern/FillPlan.h"
#include "editing/commands/ObjectCommandController.h"

namespace geck::pattern {

PlacementBatch::Result PlacementBatch::replay(ObjectCommandController& controller, const FillPlan& plan,
    bool buildSprites, const std::string& description) {
    Result r;
    if (plan.objects.empty() && plan.tiles.empty()) {
        return r;
    }

    // One batch for the whole plan: register*() buffer their commands and endBatch() collapses
    // them into a single UndoStack entry, so a thousand-tile fill is one Ctrl-Z, not one per edit.
    ScopedUndoBatch batch(controller, description);

    for (const FillPlan::Entry& entry : plan.objects) {
        // GUI: the object draws, so it needs a built visual; a null one (art that wouldn't load)
        // is counted as failed and dropped, like the prefab stamper always did. Headless: record
        // the MapObject as data — the .map stores only these ids, so no sprite or GL is needed.
        const bool ok = buildSprites
            ? (entry.object != nullptr && controller.registerObjectPlacement(entry.mapObject, entry.object))
            : controller.registerObjectData(entry.mapObject);
        if (ok) {
            ++r.objectsPlaced;
        } else {
            ++r.objectsFailed;
        }
    }

    if (!plan.tiles.empty()) {
        // before/after were captured when the plan was built; apply the after-state now and record
        // the same changes for undo (the controller applies, then registerTileEdit records).
        controller.applyTileChanges(plan.tiles, true);
        controller.registerTileEdit(description + " (tiles)", plan.tiles);
        r.tilesPainted = static_cast<int>(plan.tiles.size());
    }

    return r;
}

} // namespace geck::pattern
