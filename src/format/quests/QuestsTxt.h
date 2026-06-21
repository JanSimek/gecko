#pragma once

#include <vector>

/// @file
/// @brief Model for Fallout 2's `data/quests.txt` — the quest registry the Pip-Boy reads.
///
/// `quests.txt` is a plain table: one quest per line, five comma/space/tab-separated integers, with
/// `#` starting a comment. The engine (fallout2-ce pipboy.cc `questInit`) reads exactly these fields:
///
/// @verbatim
/// # location  description  gvar  displayThreshold  completedThreshold
/// 1500, 130, 480, 0, 1
/// @endverbatim
///
/// - location: index into **map.msg** for the area/location name (the 1500+ named-location range —
///   NOT the per-(map,elevation) name formula).
/// - description: index into **quests.msg** for the quest's text.
/// - gvar: the global variable (vault13.gam GAME_GLOBAL_VARS ordinal) whose value tracks this quest.
/// - displayThreshold / completedThreshold: the gvar value at which the quest appears in / is marked
///   complete in the Pip-Boy. Staged quests share a gvar across rows with rising thresholds.
///
/// @see fallout2-ce `pipboy.cc` (QuestDescription, questInit)
/// @see https://fallout.wiki/wiki/QUESTS.TXT_File_Format

namespace geck {

struct Quest {
    int location = -1;          ///< map.msg index for the area/location name
    int description = -1;       ///< quests.msg index for the quest text
    int gvar = -1;              ///< the global variable that tracks this quest
    int displayThreshold = 0;   ///< gvar value at which the quest becomes visible
    int completedThreshold = 0; ///< gvar value at which the quest is complete
};

/// Parsed `data/quests.txt`: the quest table, in file order.
struct QuestsTxt {
    std::vector<Quest> quests;
};

} // namespace geck
