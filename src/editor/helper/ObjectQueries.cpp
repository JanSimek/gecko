#include "ObjectQueries.h"

#include "../../format/map/MapObject.h"
#include "../../format/pro/Pro.h"
#include "../../resource/GameResources.h"
#include "../../util/ProHelper.h"

#include <spdlog/spdlog.h>

namespace geck::object_query {
namespace {

    /// Loads the object's PRO data on demand (not cached).
    Pro* getProData(const MapObject& object, resource::GameResources& resources) {
        try {
            return resources.repository().load<Pro>(ProHelper::basePath(resources, object.pro_pid));
        } catch (const std::exception& e) {
            spdlog::warn("Failed to load PRO file for PID {}: {}", object.pro_pid, e.what());
            return nullptr;
        }
    }

} // namespace

bool blocksMovement(const MapObject& object, resource::GameResources& resources) {
    // TODO: cache this value instead of reloading the PRO each call
    Pro* pro = getProData(object, resources);
    if (!pro) {
        spdlog::debug("No PRO data available for object PID {}, assuming non-blocking", object.pro_pid);
        return false;
    }

    // Objects WITHOUT the NoBlock flag (0x00000010) block movement.
    const uint32_t NO_BLOCK_FLAG = 0x00000010;
    bool hasNoBlockFlag = (pro->header.flags & NO_BLOCK_FLAG) != 0;

    spdlog::debug("Object PID {} has flags 0x{:08X}, NoBlock flag: {}, blocks movement: {}",
        object.pro_pid, pro->header.flags, hasNoBlockFlag, !hasNoBlockFlag);

    return !hasNoBlockFlag;
}

bool isWallBlocker(const MapObject& object, resource::GameResources& resources) {
    // Any object that blocks movement is treated as a gap-filling wall blocker.
    return blocksMovement(object, resources);
}

bool isShootThroughWallBlocker(const MapObject& object, resource::GameResources& resources) {
    Pro* pro = getProData(object, resources);
    if (!pro) {
        return false;
    }

    const uint32_t SHOOT_THROUGH_FLAG = 0x80000000;
    return (pro->header.flags & SHOOT_THROUGH_FLAG) != 0;
}

} // namespace geck::object_query
