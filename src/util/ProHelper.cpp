#include "ProHelper.h"

#include "format/lst/Lst.h"
#include "format/msg/Msg.h"
#include "resource/GameResources.h"
#include "resource/ResourcePaths.h"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace geck {

namespace {
    // Where each object type's proto files live — gathered in one table instead of three parallel
    // switch statements (the proto directory, its .lst index, and the .msg that names its protos).
    struct ProtoTypePaths {
        std::string_view directory; // e.g. "proto/items/" (trailing slash)
        std::string_view lst;       // e.g. "proto/items/items.lst"
        std::string_view msg;       // e.g. "text/english/game/pro_item.msg"
    };

    const ProtoTypePaths& protoTypePaths(Pro::OBJECT_TYPE type) {
        namespace RP = ResourcePaths;
        // Indexed by the OBJECT_TYPE ordinal (ITEM = 0 .. MISC = 5).
        static constexpr std::array<ProtoTypePaths, 6> kPaths = { {
            { RP::Directories::PROTO_ITEMS, RP::Lst::PROTO_ITEMS, RP::Msg::PRO_ITEM },
            { RP::Directories::PROTO_CRITTERS, RP::Lst::PROTO_CRITTERS, RP::Msg::PRO_CRIT },
            { RP::Directories::PROTO_SCENERY, RP::Lst::PROTO_SCENERY, RP::Msg::PRO_SCEN },
            { RP::Directories::PROTO_WALLS, RP::Lst::PROTO_WALLS, RP::Msg::PRO_WALL },
            { RP::Directories::PROTO_TILES, RP::Lst::PROTO_TILES, RP::Msg::PRO_TILE },
            { RP::Directories::PROTO_MISC, RP::Lst::PROTO_MISC, RP::Msg::PRO_MISC },
        } };
        const auto index = static_cast<std::size_t>(type);
        if (index >= kPaths.size()) {
            throw std::runtime_error{ "Invalid PRO type: " + std::to_string(index) };
        }
        return kPaths[index];
    }
} // namespace

Msg* ProHelper::protoMsgFile(resource::GameResources& resources) {
    return resources.repository().load<Msg>(std::string(ResourcePaths::Msg::PROTO));
}

Msg* ProHelper::statMsgFile(resource::GameResources& resources) {
    return resources.repository().load<Msg>(std::string(ResourcePaths::Msg::STAT));
}

Msg* ProHelper::perkMsgFile(resource::GameResources& resources) {
    return resources.repository().load<Msg>(std::string(ResourcePaths::Msg::PERK));
}

Msg* ProHelper::msgFile(resource::GameResources& resources, Pro::OBJECT_TYPE type) {
    return resources.repository().load<Msg>(std::string(protoTypePaths(type).msg));
}

Lst* ProHelper::lstFile(resource::GameResources& resources, uint32_t PID) {
    return resources.repository().load<Lst>(std::string(protoTypePaths(Pro::typeOfPid(PID)).lst));
}

std::string ProHelper::basePath(resource::GameResources& resources, uint32_t PID) {
    const ProtoTypePaths& paths = protoTypePaths(Pro::typeOfPid(PID));

    // The low 24 bits of a PID are the 1-based line number in the type's LST (matching the engine's
    // proto-number decode `pid & 0xFFFFFF` in proto.cc and Pro::makePid); index 0 is not a valid proto
    // and would underflow the index - 1 lookup below.
    unsigned int index = 0x00FFFFFF & PID;
    auto lst = ProHelper::lstFile(resources, PID);
    if (index == 0 || index > lst->list().size()) {
        throw std::runtime_error{ "PID index out of range (expected 1.."
            + std::to_string(lst->list().size()) + "): " + std::to_string(PID) };
    }

    std::string pro_filename = lst->list().at(index - 1);
    return std::string(paths.directory) + pro_filename; // directory carries the trailing slash
}

} // namespace geck
