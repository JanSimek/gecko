#pragma once

#include <string>
#include <vector>

/// @file
/// @brief Model for Fallout 2's `data/worldmap.txt` — worldmap terrain types and encounter tables.
///
/// `worldmap.txt` is a large INI-style file (inline `;` comments throughout). This model covers two
/// of its section kinds; the bulky `[Tile NN]` sub-tile grid (per-position terrain and encounter
/// placement) is intentionally not parsed yet.
///
/// @verbatim
/// [Data]
/// terrain_types=Desert:1, Mountain:2, City:1, Ocean:1   ; name:draw-weight pairs
/// terrain_short_names=DES, MNT, CTY, OCN                ; aligned to terrain_types by position
///
/// [Encounter: Raiders]                                  ; region-prefixed names (ARRO_*, KLA_*, …)
/// type_00=Ratio:35%, pid:16777252, Item:8{Wielded}, Item:40, Script:255
/// type_01=Dead, pid:16777220, Item:40, Script:256       ; "Dead" = spawned as a corpse
/// @endverbatim
///
/// @see fallout2-ce `worldmap.cc` (`wmConfigInit`)
/// @see https://fallout.wiki/wiki/Worldmap.txt_File_Format

namespace geck {

/// A worldmap terrain type from worldmap.txt `[Data]` (`terrain_types` / `terrain_short_names`),
/// e.g. {"Desert", "DES"}. The `weight` is the per-type number the engine uses when drawing the
/// worldmap (Desert:1, Mountain:2, …).
struct WorldmapTerrain {
    std::string name;
    std::string shortName;
    int weight = 0;
};

/// One spawn entry of an `[Encounter: …]` group (a `type_NN = …` line): the proto pid to spawn, its
/// selection ratio (percent), and the flags the table carries. Items are kept as raw pids.
struct EncounterEntry {
    int pid = -1;           ///< spawned critter/object proto pid
    int ratioPercent = 0;   ///< Ratio:NN% (0 if unspecified)
    bool dead = false;      ///< the "Dead" flag (spawned as a corpse)
    int script = -1;        ///< Script:NN (-1 if none)
    int distance = -1;      ///< Distance:NN (-1 if none)
    std::vector<int> items; ///< Item:NN pids carried
};

/// One `[Encounter: NAME]` group. The shipped names are region-prefixed (ARRO_*, KLA_*, …), so they
/// double as the worldmap's "location sections": which encounters belong to which area.
struct Encounter {
    std::string name;
    std::vector<EncounterEntry> entries;
};

/// Parsed `data/worldmap.txt` — the worldmap's terrain types and random-encounter group tables.
///
/// NOTE: this intentionally omits the `[Tile NN]` sub-tile grid (per-position terrain and encounter
/// placement) — a large, geometry-heavy layer left for a follow-up; without it there is no
/// terrain-at-position lookup or terrain-weighted travel cost yet.
struct WorldmapTxt {
    std::vector<WorldmapTerrain> terrains;
    std::vector<Encounter> encounters;

    const Encounter* findEncounter(const std::string& name) const {
        for (const auto& encounter : encounters) {
            if (encounter.name == name) {
                return &encounter;
            }
        }
        return nullptr;
    }
};

} // namespace geck
