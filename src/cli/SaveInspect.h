#pragma once

#include <iosfwd>
#include <string>

namespace geck::resource {
class GameResources;
}

namespace geck::cli {

struct DescribeSaveOptions {
    /// A save slot directory (…/data/SAVEGAME/SLOT10) or the SAVE.DAT inside it.
    std::string slotPath;
};

/// Decode a Fallout 2 CE save slot, reading SAVE.DAT in the order fallout2-ce loadsave.cc's
/// master_save_list writes it: header, player combat id, global variables, the slot's map list, the
/// player object, player proto data, kills, tagged skills, perks and the combat block. It stops after
/// the combat block. The combat list is joined to the critters on the slot's current map (its gzip .SAV),
/// so combatant/non-combatant membership comes back with each critter's live combat state.
///
/// Two block lengths come from the mounted data rather than the save, exactly as in the engine:
/// the global-variable count (data/vault13.gam) and the party-member count (data/party.txt). A mismatch
/// with the data the save was written with is detected and reported, not read past.
///
/// Emits a JSON object to `out`; returns 0 on success, nonzero with a message on a hard error.
int describeSave(resource::GameResources& resources, const DescribeSaveOptions& options, std::ostream& out);

} // namespace geck::cli
