#pragma once

#include "format/pro/Pro.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace geck {
namespace resource {
    class GameResources;
}

namespace cli {

    struct ProtoExportOptions {
        /// Only this kind of proto (ITEM or CRITTER); nullopt = both.
        std::optional<ObjectType> kind;
        /// Only items of this type; implies kind = ITEM. nullopt = every item type.
        std::optional<ItemType> itemType;
    };

    /// Fill `out` from the filter names both frontends accept: kind "item" | "critter", itemType
    /// "armor" | "container" | "drug" | "weapon" | "ammo" | "misc" | "key". Empty means no filter.
    /// Returns an error message for an unknown name, or for an itemType combined with kind "critter".
    std::optional<std::string> parseProtoFilter(std::string_view kind, std::string_view itemType,
        ProtoExportOptions& out);

    /// Emit every item and critter proto the game can load — every entry of proto/items/items.lst
    /// and proto/critters/critters.lst, not only the ones some map places. A weapon that only ever
    /// appears in a script-stocked shop is in here; export_entities cannot see it.
    ///
    /// Each proto carries the fields its .pro file stores, decoded the way fallout2-ce reads them
    /// (proto.cc protoRead / protoItemDataRead, critter.cc protoCritterDataRead). Values are raw:
    /// no engine formulas are applied. Where the game ships a name for an id (damage type and caliber
    /// in proto.msg, perk in perk.msg, stat in stat.msg, body type in proto.msg) it is reported next
    /// to the id as {id, name}; the name is null when the message is missing.
    ///
    /// Emits one JSON object to `out`:
    ///   { lists: [{kind,path,entries}],
    ///     protoCount: N,
    ///     unreadable: [{pid,kind,file,reason}],
    ///     protos: [{pid,kind,file,name,description,fid,flags,extendedFlags,sid,script, ...}] }
    /// An item decodes extendedFlags as {raw,bigGun,twoHanded,energyWeapon,hiddenItem} (energyWeapon
    /// is sfall's bit, ignored by fallout2-ce) and adds itemType, attackModes, material, size, weight,
    /// cost, inventoryFid, soundId and one object named after its type (weapon, ammo, armor, drug,
    /// container, misc, key). A critter
    /// adds headFid, aiPacket, team, critterFlags, base, bonus, skills, bodyType, xp, killType and
    /// damageType.
    ///
    /// Returns 0 on success, 1 when a requested .lst cannot be read (no data mounted).
    int exportProtos(resource::GameResources& resources, const ProtoExportOptions& options, std::ostream& out);

    /// One loaded proto as exportProtos reports it in `protos` (proto_info shares it, so both tools
    /// decode a .pro the same way and spell its keys the same). nullopt for a proto exportProtos does
    /// not list: neither an item nor a critter, or an item of an unknown type. Throws when the PID's
    /// .lst cannot be read.
    std::optional<nlohmann::ordered_json> protoRow(resource::GameResources& resources, std::uint32_t pid,
        const Pro& pro);

} // namespace cli
} // namespace geck
