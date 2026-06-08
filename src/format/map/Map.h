#pragma once

#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <array>

#include "format/IFile.h"
#include "MapScript.h"
#include "Tile.h"

namespace geck {

struct MapObject;

class Map : public IFile {
public:
    static constexpr unsigned ROWS = 100;
    static constexpr unsigned COLS = 100;
    static constexpr unsigned TILES_PER_ELEVATION = ROWS * COLS;
    static constexpr unsigned TILES_TOTAL = TILES_PER_ELEVATION * 2; // roof + floor

    static constexpr int EMPTY_TILE = 1;
    static constexpr int SCRIPT_SECTIONS = 5;
    static constexpr int FILENAME_LENGTH = 16;

    Map(std::filesystem::path path)
        : IFile(path) { }

    // Header
    struct MapHeader {
        uint32_t version;
        std::string filename;

        uint32_t player_default_position;
        uint32_t player_default_elevation; //!< map elevation where the map is loaded
        uint32_t player_default_orientation;

        uint32_t num_local_vars;
        int32_t script_id; //!< Script id for this map. Value of -1 means no map. Text string is found in MSG file scrname.msg at index [id + 101].
        uint32_t flags;
        uint32_t darkness;
        uint32_t num_global_vars;
        uint32_t map_id;
        uint32_t timestamp;
    };

    struct MapFile {
        MapHeader header;

        // Global and local variables
        std::vector<int32_t> map_local_vars;  // LVARS
        std::vector<int32_t> map_global_vars; // MVARS

        // Tiles
        std::map<int, std::vector<Tile>> tiles;

        // Scripts
        std::vector<MapScript> map_scripts[SCRIPT_SECTIONS];
        std::array<int, SCRIPT_SECTIONS> scripts_in_section;

        // Objects sorted by elevation
        std::unordered_map<int, std::vector<std::shared_ptr<MapObject>>> map_objects;
    };

    const std::unordered_map<int, std::vector<std::shared_ptr<MapObject>>>& objects() const;

    int elevations() const;

    MapFile& getMapFile();
    void setMapFile(std::unique_ptr<MapFile> newMapFile);

private:
    std::unique_ptr<MapFile> mapFile;
};

} // namespace geck
