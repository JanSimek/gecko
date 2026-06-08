#include "MapObject.h"
#include "../pro/Pro.h"

namespace geck {

bool MapObject::isWallObject() const {
    // Extract object type from the protocol ID
    uint32_t typeId = (pro_pid >> 24) & 0x0F;
    return typeId == static_cast<uint32_t>(Pro::OBJECT_TYPE::WALL);
}

} // namespace geck
