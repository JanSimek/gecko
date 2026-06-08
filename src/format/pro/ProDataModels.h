#pragma once

#include "Pro.h"

namespace geck {

/**
 * @brief Editor-facing aliases for the PRO type-specific data structs.
 *
 * Every PRO subtype the editor edits maps exactly onto the canonical Pro
 * on-disk layout, so each name here is simply an alias of the corresponding
 * Pro definition. This keeps a single source of truth: the editor and the
 * reader/writer can never drift, and a field added to a Pro struct is picked
 * up by the editor automatically.
 *
 * Critter and Scenery have no alias because their widgets edit pro->critterData
 * / pro->sceneryData in place rather than through a separate editor struct.
 */

using ProArmorData = Pro::ArmorData;
using ProContainerData = Pro::ContainerData;
using ProDrugData = Pro::DrugData;
using ProWeaponData = Pro::WeaponData;
using ProAmmoData = Pro::AmmoData;
using ProMiscData = Pro::MiscData;
using ProKeyData = Pro::KeyData;
using ProWallData = Pro::WallData;
using ProTileData = Pro::TileData;

} // namespace geck
