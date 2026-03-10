#include "ProHelper.h"

#include "format/lst/Lst.h"
#include "format/msg/Msg.h"
#include "reader/ReaderFactory.h"
#include "util/ResourceManager.h"
#include "util/ResourcePaths.h"

namespace geck {

Msg* ProHelper::protoMsgFile() {
    return ResourceManager::getInstance().loadResource<Msg>(std::string(ResourcePaths::Msg::PROTO));
}

Msg* ProHelper::statMsgFile() {
    return ResourceManager::getInstance().loadResource<Msg>(std::string(ResourcePaths::Msg::STAT));
}

Msg* ProHelper::perkMsgFile() {
    return ResourceManager::getInstance().loadResource<Msg>(std::string(ResourcePaths::Msg::PERK));
}

Msg* geck::ProHelper::msgFile(Pro::OBJECT_TYPE type) {

    std::string filename;

    switch (type) {
        case Pro::OBJECT_TYPE::ITEM: {
            filename = ResourcePaths::Msg::PRO_ITEM;
        } break;
        case Pro::OBJECT_TYPE::CRITTER: {
            filename = ResourcePaths::Msg::PRO_CRIT;
        } break;
        case Pro::OBJECT_TYPE::SCENERY: {
            filename = ResourcePaths::Msg::PRO_SCEN;
        } break;
        case Pro::OBJECT_TYPE::WALL: {
            filename = ResourcePaths::Msg::PRO_WALL;
        } break;
        case Pro::OBJECT_TYPE::TILE: {
            filename = ResourcePaths::Msg::PRO_TILE;
        } break;
        case Pro::OBJECT_TYPE::MISC: {
            filename = ResourcePaths::Msg::PRO_MISC;
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
            filename = ResourcePaths::Lst::PROTO_ITEMS;
            break;
        case Pro::OBJECT_TYPE::CRITTER:
            filename = ResourcePaths::Lst::PROTO_CRITTERS;
            break;
        case Pro::OBJECT_TYPE::SCENERY:
            filename = ResourcePaths::Lst::PROTO_SCENERY;
            break;
        case Pro::OBJECT_TYPE::WALL:
            filename = ResourcePaths::Lst::PROTO_WALLS;
            break;
        case Pro::OBJECT_TYPE::TILE:
            filename = ResourcePaths::Lst::PROTO_TILES;
            break;
        case Pro::OBJECT_TYPE::MISC:
            filename = ResourcePaths::Lst::PROTO_MISC;
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
