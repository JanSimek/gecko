#pragma once

#include <array>
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

// The worldmap is a grid of tiles (WM_TILE_WIDTH x WM_TILE_HEIGHT pixels), each split into a
// SUBTILE_GRID_WIDTH x SUBTILE_GRID_HEIGHT lattice of WM_SUBTILE_SIZE-pixel subtiles; every subtile
// has a terrain. Constants mirror fallout2-ce worldmap.cc.
inline constexpr int WM_TILE_WIDTH = 350;
inline constexpr int WM_TILE_HEIGHT = 300;
inline constexpr int WM_SUBTILE_SIZE = 50;
inline constexpr int SUBTILE_GRID_WIDTH = 7;  // subtile columns per tile (the "row" key index, 0-6)
inline constexpr int SUBTILE_GRID_HEIGHT = 6; // subtile rows per tile (the "column" key index, 0-5)

/// One worldmap tile's subtile terrain grid, indexed [row][column] to match the engine's
/// "row_column" keys (row 0-6 across, column 0-5 down).
struct WorldmapTile {
    std::array<std::array<std::string, SUBTILE_GRID_HEIGHT>, SUBTILE_GRID_WIDTH> terrain;
};

/// Parsed `data/worldmap.txt` — the worldmap's terrain types, random-encounter group tables, and the
/// per-subtile terrain grid (`[Tile NN]`).
struct WorldmapTxt {
    std::vector<WorldmapTerrain> terrains;
    std::vector<Encounter> encounters;
    std::vector<WorldmapTile> tiles; ///< the worldmap tile grid, indexed by [Tile NN]
    int numHorizontalTiles = 0;      ///< [Tile Data] num_horizontal_tiles (worldmap width, in tiles)

    const Encounter* findEncounter(const std::string& name) const {
        for (const auto& encounter : encounters) {
            if (encounter.name == name) {
                return &encounter;
            }
        }
        return nullptr;
    }

    /// The terrain at worldmap pixel (x, y), or "" if out of range / the grid isn't parsed. Mirrors
    /// fallout2-ce wmFindCurSubTileFromPos.
    std::string terrainAt(int x, int y) const {
        if (x < 0 || y < 0 || numHorizontalTiles <= 0) {
            return {};
        }
        const int tileColumn = x / WM_TILE_WIDTH;
        if (tileColumn >= numHorizontalTiles) {
            return {};
        }
        const int tile = (y / WM_TILE_HEIGHT) * numHorizontalTiles + tileColumn;
        if (tile < 0 || tile >= static_cast<int>(tiles.size())) {
            return {};
        }
        const int row = (x % WM_TILE_WIDTH) / WM_SUBTILE_SIZE;  // 0-6
        const int col = (y % WM_TILE_HEIGHT) / WM_SUBTILE_SIZE; // 0-5
        return tiles[tile].terrain[row][col];
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
