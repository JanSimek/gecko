#include "cli/PatternExtract.h"

#include "cli/MapLoad.h"
#include "cli/PatternJson.h"
#include "editor/HexGeometry.h"
#include "editor/HexagonGrid.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/Tile.h"
#include "pattern/Pattern.h"
#include "pattern/PatternBuilder.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <memory>
#include <ostream>
#include <vector>

namespace geck::cli {

namespace {
    // A hex-space bounding box (inclusive), in columns/rows of the 200x200 hex grid.
    struct HexBox {
        int minCol = 0;
        int maxCol = 0;
        int minRow = 0;
        int maxRow = 0;
        [[nodiscard]] bool contains(int hex) const {
            const int c = hexgrid::columnOf(hex);
            const int r = hexgrid::rowOf(hex);
            return c >= minCol && c <= maxCol && r >= minRow && r <= maxRow;
        }
    };

    std::vector<const MapObject*> objectsOnElevation(Map& map, int elevation) {
        std::vector<const MapObject*> objects;
        const auto& byElevation = map.getMapFile().map_objects;
        if (const auto it = byElevation.find(elevation); it != byElevation.end()) {
            for (const auto& object : it->second) {
                if (object) {
                    objects.push_back(object.get());
                }
            }
        }
        return objects;
    }

    // The bounding box of the objects matching `pids`, grown by `radius`.
    HexBox regionForPids(const std::vector<const MapObject*>& objects,
        const std::vector<std::uint32_t>& pids, int radius, bool& found) {
        HexBox box;
        found = false;
        for (const MapObject* object : objects) {
            if (std::ranges::find(pids, object->pro_pid) == pids.end()) {
                continue;
            }
            const int col = hexgrid::columnOf(object->position);
            const int row = hexgrid::rowOf(object->position);
            if (!found) {
                box = { col, col, row, row };
                found = true;
            } else {
                box.minCol = std::min(box.minCol, col);
                box.maxCol = std::max(box.maxCol, col);
                box.minRow = std::min(box.minRow, row);
                box.maxRow = std::max(box.maxRow, row);
            }
        }
        box.minCol -= radius;
        box.maxCol += radius;
        box.minRow -= radius;
        box.maxRow += radius;
        return box;
    }

    // The hex of the first object matching `pids` — used as the stamp anchor when none is given.
    int firstMatchedHex(const std::vector<const MapObject*>& objects, const std::vector<std::uint32_t>& pids) {
        for (const MapObject* object : objects) {
            if (std::ranges::find(pids, object->pro_pid) != pids.end()) {
                return object->position;
            }
        }
        return 0;
    }

    // Floor/roof tiles whose tile cell falls under the hex region (tile space is hex/2). Each layer
    // is captured only when its flag is set, so a stamp can take a roof without the ground beneath it.
    void captureTiles(Map& map, int elevation, const HexBox& region, bool wantFloor, bool wantRoof,
        std::vector<pattern::PatternBuilder::TileSelection>& floor,
        std::vector<pattern::PatternBuilder::TileSelection>& roof) {
        const auto& tilesByElevation = map.getMapFile().tiles;
        const auto it = tilesByElevation.find(elevation);
        if (it == tilesByElevation.end()) {
            return;
        }
        const auto empty = static_cast<uint16_t>(Map::EMPTY_TILE);
        const int width = HexagonGrid::TILE_GRID_WIDTH;
        for (std::size_t i = 0; i < it->second.size(); ++i) {
            const int tcol = static_cast<int>(i) % width;
            const int trow = static_cast<int>(i) / width;
            if (tcol < region.minCol / 2 || tcol > region.maxCol / 2 || trow < region.minRow / 2 || trow > region.maxRow / 2) {
                continue;
            }
            if (wantFloor && it->second[i].getFloor() != empty) {
                floor.emplace_back(static_cast<int>(i), it->second[i].getFloor());
            }
            if (wantRoof && it->second[i].getRoof() != empty) {
                roof.emplace_back(static_cast<int>(i), it->second[i].getRoof());
            }
        }
    }
} // namespace

int extractPattern(resource::GameResources& resources, const ExtractOptions& options, std::ostream& out) {
    const std::unique_ptr<Map> map = loadMap(resources, options.mapPath);
    if (!map) {
        out << "extract: could not read or parse map: " << options.mapPath << "\n";
        return 1;
    }

    const std::vector<const MapObject*> objects = objectsOnElevation(*map, options.elevation);

    bool found = false;
    HexBox region = regionForPids(objects, options.pids, options.radius, found);
    if (!found) {
        if (options.anchorHex < 0) {
            out << "extract: no objects matched the given PIDs (and no anchorHex given)\n";
            return 1;
        }
        const int c = hexgrid::columnOf(options.anchorHex);
        const int r = hexgrid::rowOf(options.anchorHex);
        region = { c - options.radius, c + options.radius, r - options.radius, r + options.radius };
    }

    const int anchorHex = options.anchorHex >= 0 ? options.anchorHex
                                                 : firstMatchedHex(objects, options.pids);

    // Capture every object inside the region (the structure plus its immediate props), verbatim.
    std::vector<const MapObject*> captured;
    std::ranges::copy_if(objects, std::back_inserter(captured),
        [&region](const MapObject* object) { return region.contains(object->position); });

    std::vector<pattern::PatternBuilder::TileSelection> floorTiles;
    std::vector<pattern::PatternBuilder::TileSelection> roofTiles;
    if (options.includeFloor || options.includeRoof) {
        captureTiles(*map, options.elevation, region, options.includeFloor, options.includeRoof, floorTiles, roofTiles);
    }

    if (captured.empty()) {
        out << "extract: the region contains no objects to capture\n";
        return 1;
    }

    pattern::Pattern result;
    result.name = options.name;
    result.variants.push_back(
        pattern::PatternBuilder::buildVariant(captured, floorTiles, roofTiles, anchorHex, "default"));

    std::ofstream file(options.outPath, std::ios::binary);
    if (!file) {
        out << "extract: failed to open output file: " << options.outPath << "\n";
        return 1;
    }
    file << serializePattern(result);
    out << "wrote " << options.outPath << " (" << captured.size() << " objects, "
        << floorTiles.size() << " floor, " << roofTiles.size() << " roof tiles)\n";
    return 0;
}

} // namespace geck::cli
