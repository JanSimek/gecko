#include "MapObject.h"
#include "../../resource/GameResources.h"
#include "../../util/ProHelper.h"
#include "../pro/Pro.h"
#include <spdlog/spdlog.h>

namespace geck {

bool MapObject::isShootThroughWallBlocker(resource::GameResources& resources) {

    // Get PRO file data to check flags
    Pro* pro = getProData(resources);
    if (!pro) {
        return false;
    }

    const uint32_t SHOOT_THROUGH_FLAG = 0x80000000;
    bool is_shoot_through = ((pro->header.flags & SHOOT_THROUGH_FLAG) != 0);
    return is_shoot_through;
}

Pro* MapObject::getProData(resource::GameResources& resources) const {
    try {
        return resources.repository().load<Pro>(ProHelper::basePath(resources, pro_pid));
    } catch (const std::exception& e) {
        spdlog::warn("Failed to load PRO file for PID {}: {}", pro_pid, e.what());
        return nullptr;
    }
}

bool MapObject::blocksMovement(resource::GameResources& resources) const {

    // TODO: save it the value

    // Get PRO file data to check flags
    Pro* pro = getProData(resources);
    if (!pro) {
        spdlog::debug("No PRO data available for object PID {}, assuming non-blocking", pro_pid);
        return false;
    }

    // Check if object has NoBlock flag (0x00000010)
    // Objects WITHOUT this flag block movement
    const uint32_t NO_BLOCK_FLAG = 0x00000010;
    bool hasNoBlockFlag = (pro->header.flags & NO_BLOCK_FLAG) != 0;

    spdlog::debug("Object PID {} has flags 0x{:08X}, NoBlock flag: {}, blocks movement: {}",
        pro_pid, pro->header.flags, hasNoBlockFlag, !hasNoBlockFlag);

    return !hasNoBlockFlag;
}

bool MapObject::isWallObject() const {
    // Extract object type from the protocol ID
    uint32_t typeId = (pro_pid >> 24) & 0x0F;
    return typeId == static_cast<uint32_t>(Pro::OBJECT_TYPE::WALL);
}

} // namespace geck
