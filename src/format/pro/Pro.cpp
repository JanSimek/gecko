#include "Pro.h"
#include <cstring>

namespace geck {

Pro::Pro(std::filesystem::path path) : IFile(path) {
    initializeDataStructures();
}

void Pro::initializeDataStructures() {
    // Initialize header
    memset(&header, 0, sizeof(header));
    
    // Initialize all data structures to zero
    memset(&commonItemData, 0, sizeof(commonItemData));
    memset(&armorData, 0, sizeof(armorData));
    memset(&containerData, 0, sizeof(containerData));
    memset(&drugData, 0, sizeof(drugData));
    memset(&weaponData, 0, sizeof(weaponData));
    memset(&ammoData, 0, sizeof(ammoData));
    memset(&miscData, 0, sizeof(miscData));
    memset(&keyData, 0, sizeof(keyData));
    
    _objectSubtypeId = 0;
}

unsigned int Pro::objectSubtypeId() const {
    return _objectSubtypeId;
}

void Pro::setObjectSubtypeId(unsigned int objectSubtypeId) {
    _objectSubtypeId = objectSubtypeId;
}

Pro::OBJECT_TYPE Pro::type() const {
    int32_t type = (header.PID & 0x0F000000) >> 24;
    return static_cast<Pro::OBJECT_TYPE>(type);
}

Pro::ITEM_TYPE Pro::itemType() const {
    if (type() == OBJECT_TYPE::ITEM) {
        return static_cast<Pro::ITEM_TYPE>(_objectSubtypeId);
    }
    return Pro::ITEM_TYPE::MISC; // Default fallback
}

const std::string Pro::typeToString() const {
    switch (type()) {
        case OBJECT_TYPE::ITEM: {
            return "Item";
        } break;
        case OBJECT_TYPE::CRITTER: {
            return "Critter";
        } break;
        case OBJECT_TYPE::SCENERY: {
            return "Scenery";
        } break;
        case OBJECT_TYPE::WALL: {
            return "Wall";
        } break;
        case OBJECT_TYPE::TILE: {
            return "Tile";
        } break;
        case OBJECT_TYPE::MISC: {
            return "Misc";
        } break;
    }
    return "Unknown proto";
}

} // namespace geck
