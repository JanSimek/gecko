#pragma once

#include <QPixmap>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class QTreeWidget;

namespace geck {

struct MapObject;
namespace resource {
    class GameResources;
}

namespace ui::inventory {

    struct ItemDetails {
        QString name;
        QString typeName;
        QString pidText;
    };

    uint32_t displayAmount(resource::GameResources& resources, const MapObject& item);
    ItemDetails describeItem(resource::GameResources& resources, uint32_t pid);
    QPixmap loadItemIcon(resource::GameResources& resources, uint32_t pid, int iconSize = 0, bool fixedCanvas = false);
    bool itemExists(resource::GameResources& resources, uint32_t pid);
    std::unique_ptr<MapObject> createMapInventoryItem(resource::GameResources& resources, uint32_t pid, int amount);

    /// Canonical column layout shared by every inventory tree view.
    /// COLUMN_PID is only populated when InventoryTreeOptions::showPidColumn is set.
    enum InventoryColumn {
        COLUMN_ICON = 0,
        COLUMN_NAME = 1,
        COLUMN_TYPE = 2,
        COLUMN_AMOUNT = 3,
        COLUMN_PID = 4,
    };

    /// Per-view knobs for populateInventoryTree. Defaults match a read-only,
    /// index-keyed view; SelectionPanel overrides them for its editable, PID-keyed tree.
    struct InventoryTreeOptions {
        int iconSize = 0;             ///< forwarded to loadItemIcon when no iconProvider is set
        bool fixedCanvas = false;     ///< pad the icon onto a fixed iconSize canvas
        bool showPidColumn = false;   ///< write details.pidText into COLUMN_PID
        bool editable = false;        ///< add Qt::ItemIsEditable to each row
        bool setIconSizeHint = false; ///< pin COLUMN_ICON size hint to iconSize
        bool userRoleIsPid = false;   ///< UserRole payload: item PID (true) or inventory index (false)
        int userRoleColumn = COLUMN_NAME; ///< column carrying the UserRole payload
        std::function<QPixmap(const MapObject&)> iconProvider; ///< optional icon override
    };

    /// Fill `tree` with one row per inventory entry, honouring `options`. The caller
    /// owns column/header setup and empty-state handling; this only appends rows.
    void populateInventoryTree(QTreeWidget* tree,
        resource::GameResources& resources,
        const std::vector<std::unique_ptr<MapObject>>& inventory,
        const InventoryTreeOptions& options);

} // namespace ui::inventory

} // namespace geck
