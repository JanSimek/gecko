#include "Pro.h"

namespace geck {

unsigned int Pro::objectSubtypeId() {
    return _objectSubtypeId;
}

void Pro::setObjectSubtypeId(unsigned int objectSubtypeId) {
    _objectSubtypeId = objectSubtypeId;
}

Pro::OBJECT_TYPE Pro::type() const {
    int32_t type = (header.PID & 0x0F000000) >> 24;
    return static_cast<Pro::OBJECT_TYPE>(type);
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
