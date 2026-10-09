#include "Pro.h"
#include <cstring>

namespace geck {

Pro::Pro(std::filesystem::path path)
    : IFile(path) {
    initializeDataStructures();
}

void Pro::initializeDataStructures() {
    memset(&header, 0, sizeof(header));
    memset(&commonItemData, 0, sizeof(commonItemData));
    memset(&armorData, 0, sizeof(armorData));
    memset(&containerData, 0, sizeof(containerData));
    memset(&drugData, 0, sizeof(drugData));
    memset(&weaponData, 0, sizeof(weaponData));
    memset(&ammoData, 0, sizeof(ammoData));
    memset(&miscData, 0, sizeof(miscData));
    memset(&keyData, 0, sizeof(keyData));
    memset(&critterData, 0, sizeof(critterData));
    memset(&sceneryData, 0, sizeof(sceneryData));

    _objectSubtypeId = 0;
}

unsigned int Pro::objectSubtypeId() const {
    return _objectSubtypeId;
}

void Pro::setObjectSubtypeId(unsigned int objectSubtypeId) {
    _objectSubtypeId = objectSubtypeId;
}

ItemType Pro::itemType() const {
    if (type() == ObjectType::Item) {
        return static_cast<ItemType>(_objectSubtypeId);
    }
    return ItemType::Misc;
}

const std::string Pro::typeToString() const {
    return typeToString(type());
}

std::string Pro::typeToString(ObjectType type) {
    switch (type) {
        case ObjectType::Item:
            return "Item";
        case ObjectType::Critter:
            return "Critter";
        case ObjectType::Scenery:
            return "Scenery";
        case ObjectType::Wall:
            return "Wall";
        case ObjectType::Tile:
            return "Tile";
        case ObjectType::Misc:
            return "Misc";
        default:
            break;
    }
    return "Unknown proto";
}

} // namespace geck
