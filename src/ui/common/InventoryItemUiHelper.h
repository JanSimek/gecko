#pragma once

#include <QPixmap>
#include <QString>

#include <cstdint>
#include <memory>

namespace geck {

struct MapObject;

namespace ui::inventory {

struct ItemDetails {
    QString name;
    QString typeName;
    QString pidText;
};

ItemDetails describeItem(uint32_t pid);
QPixmap loadItemIcon(uint32_t pid, int iconSize = 0, bool fixedCanvas = false);
bool itemExists(uint32_t pid);
std::unique_ptr<MapObject> createMapInventoryItem(uint32_t pid, int amount);

} // namespace ui::inventory

} // namespace geck
