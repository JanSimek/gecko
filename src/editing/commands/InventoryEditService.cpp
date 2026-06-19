#include "InventoryEditService.h"

#include "format/map/MapObject.h"
#include "editing/commands/UndoBatcher.h"
#include "util/UndoStack.h"

namespace geck {

namespace {
    void restoreInventory(MapObject& container, const std::vector<std::shared_ptr<MapObject>>& snapshot) {
        container.inventory.clear();
        container.inventory.reserve(snapshot.size());
        for (const auto& item : snapshot) {
            if (item) {
                container.inventory.push_back(item->cloneDeep());
            }
        }
        container.objects_in_inventory = static_cast<uint32_t>(container.inventory.size());
    }
} // namespace

InventoryEditService::InventoryEditService(UndoBatcher& batcher)
    : _batcher(batcher) {
}

std::vector<std::shared_ptr<MapObject>> InventoryEditService::cloneInventory(
    const std::vector<std::unique_ptr<MapObject>>& inventory) {
    std::vector<std::shared_ptr<MapObject>> out;
    out.reserve(inventory.size());
    for (const auto& item : inventory) {
        if (item) {
            out.push_back(std::shared_ptr<MapObject>(item->cloneDeep()));
        }
    }
    return out;
}

bool InventoryEditService::registerInventoryEdit(const std::shared_ptr<MapObject>& container,
    std::vector<std::shared_ptr<MapObject>> before,
    std::vector<std::shared_ptr<MapObject>> after) {
    if (!container) {
        return false;
    }

    UndoCommand cmd;
    cmd.description = "Edit Inventory";
    cmd.undo = [container, before = std::move(before)]() { restoreInventory(*container, before); };
    cmd.redo = [container, after = std::move(after)]() { restoreInventory(*container, after); };
    // The caller already applied the edit, so do not run redo() here.
    return _batcher.push(std::move(cmd));
}

} // namespace geck
