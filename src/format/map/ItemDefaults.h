#pragma once

#include "format/map/MapObject.h"
#include "format/pro/Pro.h"

namespace geck {

// A new item's own data, from its proto, as the engine sets it when it makes
// one (fallout2-ce proto.cc _proto_update_init): a weapon loaded to capacity
// with its ammo type, a full box of ammo, a misc item's charges, a key's code.
// Without this every item gecko made held nothing: ammo boxes with 0 rounds
// (shown as an amount of 0, and an empty box in game) and unloaded weapons.
inline void applyItemDefaults(MapObject& object, const Pro& pro) {
    if (pro.type() != Pro::OBJECT_TYPE::ITEM) {
        return;
    }
    switch (pro.itemType()) {
        case Pro::ITEM_TYPE::WEAPON:
            object.ammo = pro.weaponData.ammoCapacity;
            object.ammo_pid = static_cast<uint32_t>(pro.weaponData.ammoPID);
            break;
        case Pro::ITEM_TYPE::AMMO:
            object.ammo = pro.ammoData.quantity;
            break;
        case Pro::ITEM_TYPE::MISC:
            object.ammo = pro.miscData.charges;
            break;
        case Pro::ITEM_TYPE::KEY:
            object.keycode = pro.keyData.keyId;
            break;
        default:
            break;
    }
}

} // namespace geck
