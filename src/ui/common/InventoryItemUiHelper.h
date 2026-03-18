#pragma once

#include <QPixmap>
#include <QString>

#include <cstdint>
#include <memory>

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

} // namespace ui::inventory

} // namespace geck
