#pragma once

#include <memory>
#include <vector>

namespace geck {

struct MapObject;
class UndoBatcher;

/**
 * @brief Undoable container/critter inventory editing.
 *
 * One of the aggregate services ObjectCommandController delegates to. The caller
 * applies the edit and passes cloneInventory() snapshots taken around it; the
 * service records the before/after on the shared UndoBatcher.
 */
class InventoryEditService {
public:
    explicit InventoryEditService(UndoBatcher& batcher);

    /// Deep-clones a container/critter inventory into a detached snapshot (the
    /// inventory holds unique_ptrs, so a snapshot must clone).
    static std::vector<std::shared_ptr<MapObject>> cloneInventory(
        const std::vector<std::unique_ptr<MapObject>>& inventory);

    /// Records an undoable inventory change. `before`/`after` are cloneInventory()
    /// snapshots taken around an edit the caller has already applied.
    bool registerInventoryEdit(const std::shared_ptr<MapObject>& container,
        std::vector<std::shared_ptr<MapObject>> before,
        std::vector<std::shared_ptr<MapObject>> after);

private:
    UndoBatcher& _batcher;
};

} // namespace geck
