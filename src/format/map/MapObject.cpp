#include "MapObject.h"
#include "../../util/ResourceManager.h"
#include "../../util/ProHelper.h"
#include "../pro/Pro.h"
#include <spdlog/spdlog.h>

namespace geck {

Pro* MapObject::getProData() const {
    try {
        return ResourceManager::getInstance().loadResource<Pro>(ProHelper::basePath(pro_pid));
    } catch (const std::exception& e) {
        spdlog::warn("Failed to load PRO file for PID {}: {}", pro_pid, e.what());
        return nullptr;
    }
}

bool MapObject::blocksMovement() const {
    // Special case: gap-filling wall blockers always block
    if (isGapFillingWallBlocker()) {
        return true;
    }
    
    // Get PRO file data to check flags
    Pro* pro = getProData();
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

// FIXME: probably incorrect
bool MapObject::isGapFillingWallBlocker() const {
    return pro_pid == (0x05000000 | 620) || pro_pid == (0x05000000 | 621);
}

bool MapObject::isWallObject() const {
    // Extract object type from the protocol ID
    uint32_t typeId = (pro_pid >> 24) & 0x0F;
    return typeId == static_cast<uint32_t>(Pro::OBJECT_TYPE::WALL);
}

} // namespace geck