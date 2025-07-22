#include "ProHelper.h"

#include "format/lst/Lst.h"
#include "format/msg/Msg.h"
#include "reader/ReaderFactory.h"
#include "util/ResourceManager.h"

namespace geck {

Msg* geck::ProHelper::msgFile(Pro::OBJECT_TYPE type) {

    std::string filename;

    switch (type) {
        case Pro::OBJECT_TYPE::ITEM: {
            filename = "text/english/game/pro_item.msg";
        } break;
        case Pro::OBJECT_TYPE::CRITTER: {
            filename = "text/english/game/pro_crit.msg";
        } break;
        case Pro::OBJECT_TYPE::SCENERY: {
            filename = "text/english/game/pro_scen.msg";
        } break;
        case Pro::OBJECT_TYPE::WALL: {
            filename = "text/english/game/pro_wall.msg";
        } break;
        case Pro::OBJECT_TYPE::TILE: {
            filename = "text/english/game/pro_tile.msg";
        } break;
        case Pro::OBJECT_TYPE::MISC: {
            filename = "text/english/game/pro_misc.msg";
        } break;
        default:
            throw std::runtime_error{ "Invalid PRO type" };
    }

    return ResourceManager::getInstance().loadResource<Msg>(filename);
}

Lst* ProHelper::lstFile(uint32_t PID) {

    unsigned int typeId = PID >> 24;
    std::string filename;
    switch (static_cast<Pro::OBJECT_TYPE>(typeId)) {
        case Pro::OBJECT_TYPE::ITEM:
            filename = "proto/items/items.lst";
            break;
        case Pro::OBJECT_TYPE::CRITTER:
            filename = "proto/critters/critters.lst";
            break;
        case Pro::OBJECT_TYPE::SCENERY:
            filename = "proto/scenery/scenery.lst";
            break;
        case Pro::OBJECT_TYPE::WALL:
            filename = "proto/walls/walls.lst";
            break;
        case Pro::OBJECT_TYPE::TILE:
            filename = "proto/tiles/tiles.lst";
            break;
        case Pro::OBJECT_TYPE::MISC:
            filename = "proto/misc/misc.lst";
            break;
        default:
            throw std::runtime_error{ "Wrong PID: " + std::to_string(PID) };
    }

    return ResourceManager::getInstance().loadResource<Lst>(filename);
}

const std::string ProHelper::basePath(uint32_t PID) {
    std::string pro_basepath = "proto/";

    unsigned int typeId = PID >> 24;
    switch (static_cast<Pro::OBJECT_TYPE>(typeId)) {
        case Pro::OBJECT_TYPE::ITEM:
            pro_basepath += "items";
            break;
        case Pro::OBJECT_TYPE::CRITTER:
            pro_basepath += "critters";
            break;
        case Pro::OBJECT_TYPE::SCENERY:
            pro_basepath += "scenery";
            break;
        case Pro::OBJECT_TYPE::WALL:
            pro_basepath += "walls";
            break;
        case Pro::OBJECT_TYPE::TILE:
            pro_basepath += "tiles";
            break;
        case Pro::OBJECT_TYPE::MISC:
            pro_basepath += "misc";
            break;
        default:
            throw std::runtime_error{ "PID out of range: " + std::to_string(PID) };
    }

    unsigned int index = 0x00000FFF & PID;

    auto lst = ProHelper::lstFile(PID);

    if (index > lst->list().size()) {
        throw std::runtime_error{ "LST size < PID: " + std::to_string(PID) };
    }

    std::string pro_filename = lst->list().at(index - 1);

    return pro_basepath + "/" + pro_filename;
}

} // namespace geck
