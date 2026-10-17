#pragma once

#include <array>
#include <cctype>
#include <string>
#include <vector>

/// @file
/// @brief Model for Fallout 2's `data/worldmap.txt` — worldmap terrain types and encounter tables.
///
/// `worldmap.txt` is a large INI-style file (inline `;` comments throughout). This model covers the
/// terrain types, the encounter frequencies, the encounter groups and tables, and the `[Tile NN]`
/// sub-tile grid; `[Random Maps: …]` is not parsed.
///
/// @verbatim
/// [Data]
/// Common=22%                                            ; what each frequency name rolls
/// terrain_types=Desert:1, Mountain:2, City:1, Ocean:1   ; name:draw-weight pairs
/// terrain_short_names=DES, MNT, CTY, OCN                ; aligned to terrain_types by position
///
/// [Encounter: Raiders]                                  ; region-prefixed names (ARRO_*, KLA_*, …)
/// type_00=Ratio:35%, pid:16777252, Item:8{Wielded}, Item:40, Script:255
/// type_01=Dead, pid:16777220, Item:40, Script:256       ; "Dead" = spawned as a corpse
///
/// [Encounter Table 0]                                   ; what a sub-tile's last field names
/// lookup_name=DstN
/// maps=Desert Encounter 1, Desert Encounter 2
/// enc_00=Chance:50%,Enc:(2-5) Raiders AMBUSH Player,Scenery:Normal,If(Player(Level) > 4)
///
/// [Tile 0]
/// 0_0=Ocean,Fill_W,None,None,None,Fish_O                ; terrain, fill, morning/afternoon/night, table
/// @endverbatim
///
/// @see fallout2-ce `worldmap.cc` (`wmConfigInit`)
/// @see https://fallout.wiki/wiki/Worldmap.txt_File_Format

namespace geck {

/// A worldmap terrain type from worldmap.txt `[Data]` (`terrain_types` / `terrain_short_names`),
/// e.g. {"Desert", "DES"}. `difficulty` is the engine's terrain difficulty — the `:N` in
/// `terrain_types` (Desert:1, Mountain:2, …) — i.e. the per-step cost it adds to worldmap travel.
struct WorldmapTerrain {
    std::string name;
    std::string shortName;
    int difficulty = 0;
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

/// One `enc_NN` group of an encounter table entry: `(min-max) NAME [SITUATION]`, NAME being an
/// `[Encounter: NAME]` group or `Player`. Defaults mirror fallout2-ce `wmEncounterSubEncSlotInit`.
struct EncounterTableGroup {
    int minCount = 1;
    int maxCount = 1;
    std::string name;
    std::string situation = "nothing"; ///< lowercased: nothing, ambush, fighting or and
};

/// One `enc_NN` line of an `[Encounter Table N]`. Defaults mirror fallout2-ce `wmEncounterTypeSlotInit`.
struct EncounterTableEntry {
    int chance = 0;                          ///< Chance:NN%, the entry's weight in the pick
    int counter = -1;                        ///< Counter:N, how many times it may happen (-1 = unlimited)
    bool special = false;                    ///< Special: a special encounter
    std::string map;                         ///< Map:NAME, a maps.txt lookup_name; empty = the table's maps
    std::vector<EncounterTableGroup> groups; ///< Enc:…
    std::string scenery = "normal";          ///< Scenery:…, lowercased: none, light, normal or heavy
    std::string condition;                   ///< the If(…) chain as written, empty for none
};

/// One `[Encounter Table N]`: the encounters rolled on the sub-tiles that name its `lookup_name`.
struct EncounterTable {
    int index = 0;
    std::string lookupName;
    std::vector<std::string> maps; ///< the maps the encounter may load (maps.txt lookup_names)
    std::vector<EncounterTableEntry> entries;
    /// enc_NN lines past a gap in the numbering: the engine stops at the first missing NN, so these
    /// never roll (the shipped Wild6_M has no enc_03).
    int skippedEntries = 0;
};

/// A sub-tile's encounter frequency, in fallout2-ce `wmFreqStrs` order; the `[Data]` key of the same
/// name gives its percentage.
enum class EncounterFrequency {
    None,
    Rare,
    Uncommon,
    Common,
    Frequent,
    Forced,
};
inline constexpr std::array<const char*, 6> ENCOUNTER_FREQUENCY_NAMES = { "none", "rare", "uncommon", "common",
    "frequent", "forced" };

/// The parts of the day a sub-tile gives its own frequency for (fallout2-ce `DAY_PART_*`): morning
/// runs 6:00-11:59, afternoon 12:00-17:59, night the rest.
inline constexpr std::array<const char*, 3> DAY_PART_NAMES = { "morning", "afternoon", "night" };

/// One sub-tile line of a `[Tile NN]`: `terrain, fill, morning, afternoon, night, table`
/// (fallout2-ce `wmParseSubTileInfo`).
struct WorldmapSubtile {
    std::string terrain;
    /// Lowercased: no_fill, fill_n, fill_s, fill_e, fill_w, fill_nw, fill_ne, fill_sw or fill_se.
    std::string fill;
    /// The frequency for each of DAY_PART_NAMES.
    std::array<EncounterFrequency, DAY_PART_NAMES.size()> chance{};
    /// lookup_name of the `[Encounter Table N]` rolled here.
    std::string encounterTable;
};

// The worldmap is a grid of tiles (WM_TILE_WIDTH x WM_TILE_HEIGHT pixels), each split into a
// SUBTILE_GRID_WIDTH x SUBTILE_GRID_HEIGHT lattice of WM_SUBTILE_SIZE-pixel subtiles; every subtile
// has a terrain. Constants mirror fallout2-ce worldmap.cc.
inline constexpr int WM_TILE_WIDTH = 350;
inline constexpr int WM_TILE_HEIGHT = 300;
inline constexpr int WM_SUBTILE_SIZE = 50;
inline constexpr int SUBTILE_GRID_WIDTH = 7;  // subtile columns per tile (the "row" key index, 0-6)
inline constexpr int SUBTILE_GRID_HEIGHT = 6; // subtile rows per tile (the "column" key index, 0-5)

// Where the worldmap view sits inside the engine's 640x480 interface window (WM_VIEW_X/WM_VIEW_Y).
// This matters outside the interface too: city.txt's world_pos is measured from the window origin,
// not the view's, so a marker's true position on the worldmap is world_pos minus these. fallout2-ce
// applies the same bias when drawing (wmInterfaceRefresh) and when hit-testing
// (wmMatchWorldPosToArea), so the two agree.
inline constexpr int WM_VIEW_X = 22;
inline constexpr int WM_VIEW_Y = 21;

/// One worldmap tile: its background art plus the sub-tile grid, indexed [row][column] to
/// match the engine's "row_column" keys (row 0-6 across, column 0-5 down).
struct WorldmapTile {
    /// `art_idx` — an intrface.lst index, i.e. FID `buildFid(OBJ_TYPE_INTERFACE, artIndex)`. The
    /// shipped tiles resolve to `art/intrface/wrldmp00.frm` … `wrldmp19.frm`, each 350x300 with the
    /// yellow subtile grid already drawn in. -1 when the section had no art_idx.
    int artIndex = -1;
    /// `walk_mask_name` — the stem of a `data/<name>.msk` travel mask (1 bit per pixel, 44-byte
    /// rows), empty when the tile is walkable throughout.
    std::string walkMaskName;
    /// `encounter_difficulty` — added to the tile's encounter difficulty rolls.
    int encounterDifficulty = 0;
    std::array<std::array<WorldmapSubtile, SUBTILE_GRID_HEIGHT>, SUBTILE_GRID_WIDTH> subtiles;
};

/// Parsed `data/worldmap.txt` — the worldmap's terrain types, encounter frequencies, random-encounter
/// groups and tables, and the sub-tile grid (`[Tile NN]`).
struct WorldmapTxt {
    std::vector<WorldmapTerrain> terrains;
    /// `[Data]` percentage per EncounterFrequency. The engine reads them in order and stops at the
    /// first missing one, leaving the rest 0.
    std::array<int, ENCOUNTER_FREQUENCY_NAMES.size()> frequencyPercent{};
    std::vector<Encounter> encounters;
    /// [Encounter Table 0] up to the first missing one.
    std::vector<EncounterTable> encounterTables;
    std::vector<WorldmapTile> tiles; ///< the worldmap tile grid, indexed by [Tile NN]
    int numHorizontalTiles = 0;      ///< [Tile Data] num_horizontal_tiles (worldmap width, in tiles)

    /// Tile rows, i.e. `tiles.size() / numHorizontalTiles` (the engine's
    /// `wmMaxTileNum / wmNumHorizontalTiles`). 0 when the tile grid isn't loaded.
    int numVerticalTiles() const {
        if (numHorizontalTiles <= 0) {
            return 0;
        }
        return static_cast<int>(tiles.size()) / numHorizontalTiles;
    }

    /// The whole worldmap's size in pixels — 1400x1500 for the shipped 4x5 grid of 350x300 tiles.
    int widthPixels() const { return numHorizontalTiles * WM_TILE_WIDTH; }
    int heightPixels() const { return numVerticalTiles() * WM_TILE_HEIGHT; }

    const Encounter* findEncounter(const std::string& name) const {
        for (const auto& encounter : encounters) {
            if (encounter.name == name) {
                return &encounter;
            }
        }
        return nullptr;
    }

    /// The `[Tile NN]` index holding worldmap pixel (x, y), or -1 if out of range / the grid isn't
    /// parsed.
    int tileIndexAt(int x, int y) const {
        if (x < 0 || y < 0 || numHorizontalTiles <= 0) {
            return -1;
        }
        const int tileColumn = x / WM_TILE_WIDTH;
        if (tileColumn >= numHorizontalTiles) {
            return -1;
        }
        const int tile = (y / WM_TILE_HEIGHT) * numHorizontalTiles + tileColumn;
        return tile < static_cast<int>(tiles.size()) ? tile : -1;
    }

    /// The sub-tile at worldmap pixel (x, y), or nullptr if out of range / the grid isn't parsed.
    /// Mirrors fallout2-ce wmFindCurSubTileFromPos.
    const WorldmapSubtile* subtileAt(int x, int y) const {
        const int tile = tileIndexAt(x, y);
        if (tile < 0) {
            return nullptr;
        }
        const int row = (x % WM_TILE_WIDTH) / WM_SUBTILE_SIZE;  // 0-6
        const int col = (y % WM_TILE_HEIGHT) / WM_SUBTILE_SIZE; // 0-5
        return &tiles[tile].subtiles[row][col];
    }

    /// The terrain at worldmap pixel (x, y), or "" if out of range / the grid isn't parsed.
    std::string terrainAt(int x, int y) const {
        const WorldmapSubtile* subtile = subtileAt(x, y);
        return subtile != nullptr ? subtile->terrain : std::string{};
    }

    /// The encounter table with this lookup_name, matched case-insensitively as the engine does
    /// (wmParseFindEncounterTypeMatch), or nullptr.
    const EncounterTable* findEncounterTable(const std::string& lookupName) const {
        const auto lower = [](std::string s) {
            for (char& c : s) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return s;
        };
        const std::string wanted = lower(lookupName);
        for (const auto& table : encounterTables) {
            if (lower(table.lookupName) == wanted) {
                return &table;
            }
        }
        return nullptr;
    }

    /// The difficulty of a terrain type by name (exact match), or 0 if unknown.
    int difficultyOf(const std::string& terrainName) const {
        for (const auto& terrain : terrains) {
            if (terrain.name == terrainName) {
                return terrain.difficulty;
            }
        }
        return 0;
    }
};

} // namespace geck
