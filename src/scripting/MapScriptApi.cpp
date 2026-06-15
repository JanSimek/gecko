#include "scripting/MapScriptApi.h"

#include <algorithm>
#include <array>
#include <cctype>

#include "editor/HexGeometry.h"
#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "format/lst/Lst.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/Tile.h"
#include "format/pro/Pro.h"
#include "pattern/PatternSprite.h"
#include "resource/GameResources.h"
#include "resource/ResourcePaths.h"
#include "ui/core/TileChange.h"
#include "ui/editing/ObjectCommandController.h"
#include "util/ProHelper.h"

namespace geck {

MapScriptApi::MapScriptApi(resource::GameResources& resources, const HexagonGrid& hexgrid,
    ObjectCommandController& controller, Map& map, int elevation, bool buildSprites)
    : _resources(resources)
    , _hexgrid(hexgrid)
    , _controller(controller)
    , _map(map)
    , _elevation(elevation)
    , _buildSprites(buildSprites) {
}

bool MapScriptApi::isValidHex(int hex) const {
    return hex >= 0 && hex < HexagonGrid::POSITION_COUNT;
}

std::vector<int> MapScriptApi::hexNeighbors(int hex) const {
    using namespace geck::hexgrid;
    // The six cube-coordinate neighbour directions; converting through cube space gives
    // the parity-correct offset neighbours regardless of the hex's column parity.
    static constexpr std::array<Cube, 6> kDirs = {
        { { 1, -1, 0 }, { 1, 0, -1 }, { 0, 1, -1 }, { -1, 1, 0 }, { -1, 0, 1 }, { 0, -1, 1 } }
    };
    std::vector<int> result;
    if (!isValidHex(hex)) {
        return result;
    }
    const Cube c = cubeOfPosition(hex);
    for (const Cube& d : kDirs) {
        const ColRow cr = cubeToOffset(c + d);
        if (cr.col >= 0 && cr.col < WIDTH && cr.row >= 0 && cr.row < HEIGHT) {
            result.push_back(cr.row * WIDTH + cr.col);
        }
    }
    return result;
}

uint16_t MapScriptApi::getFloor(int tileIndex) const {
    const auto& tiles = _map.getMapFile().tiles;
    const auto it = tiles.find(_elevation);
    if (it == tiles.end() || tileIndex < 0 || tileIndex >= static_cast<int>(it->second.size())) {
        return static_cast<uint16_t>(Map::EMPTY_TILE);
    }
    return it->second[tileIndex].getFloor();
}

uint16_t MapScriptApi::getRoof(int tileIndex) const {
    const auto& tiles = _map.getMapFile().tiles;
    const auto it = tiles.find(_elevation);
    if (it == tiles.end() || tileIndex < 0 || tileIndex >= static_cast<int>(it->second.size())) {
        return static_cast<uint16_t>(Map::EMPTY_TILE);
    }
    return it->second[tileIndex].getRoof();
}

int MapScriptApi::tileId(const std::string& name) const {
    const Lst* lst = nullptr;
    try {
        lst = _resources.repository().load<Lst>(std::string(ResourcePaths::Lst::TILES));
    } catch (...) {
        return -1;
    }
    if (lst == nullptr) {
        return -1;
    }
    // tiles.lst entries are already lowercased/trimmed by the reader; normalise the query the
    // same way so "edg5000", "EDG5000.FRM" and "edg5000.frm" all match.
    std::string needle = name;
    std::transform(needle.begin(), needle.end(), needle.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (needle.size() < 4 || needle.compare(needle.size() - 4, 4, ".frm") != 0) {
        needle += ".frm";
    }
    const auto& entries = lst->list();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i] == needle) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void MapScriptApi::beginBatch(const std::string& description) {
    _controller.beginBatch(description);
}

void MapScriptApi::endBatch() {
    _controller.endBatch();
}

bool MapScriptApi::placeObject(uint32_t proPid, uint32_t frmPid, int hex, uint32_t direction) {
    if (!isValidHex(hex)) {
        return false;
    }
    auto mapObject = std::make_shared<MapObject>();
    mapObject->position = hex;
    mapObject->elevation = static_cast<uint32_t>(_elevation);
    mapObject->direction = direction;
    if (auto h = _hexgrid.getHexByPosition(static_cast<uint32_t>(hex)); h.has_value()) {
        mapObject->x = static_cast<uint32_t>(h->get().x());
        mapObject->y = static_cast<uint32_t>(h->get().y());
    }
    mapObject->pro_pid = proPid;
    mapObject->frm_pid = frmPid;

    // Headless: record the MapObject as data only. The .map stores just these ids; the engine
    // and editor resolve the art (frmPid) when the map is loaded, so no sprite or GL is needed
    // and placement does not require the FRM to be present in the mounted data.
    if (!_buildSprites) {
        if (_controller.registerObjectData(mapObject)) {
            ++_placedObjects;
            return true;
        }
        return false;
    }

    // GUI: the object draws a sprite, so it requires resolvable art; without it there is
    // nothing to place (the same skip the prefab stamper makes when a fid can't load).
    auto object = pattern::buildSpriteObject(_resources, _hexgrid, frmPid, hex, direction);
    if (!object) {
        return false;
    }
    object->setMapObject(mapObject);
    if (_controller.registerObjectPlacement(mapObject, object)) {
        ++_placedObjects;
        return true;
    }
    return false;
}

bool MapScriptApi::placeProto(uint32_t proPid, int hex, uint32_t direction) {
    if (!isValidHex(hex)) {
        return false;
    }
    // The proto header carries the default art FID the engine uses for this object; resolve it
    // so callers place by PID alone. A proto we can't load has no art to place.
    uint32_t frmPid = 0;
    try {
        if (const Pro* pro = _resources.repository().load<Pro>(ProHelper::basePath(_resources, proPid));
            pro != nullptr) {
            frmPid = static_cast<uint32_t>(pro->header.FID);
        }
    } catch (...) {
        return false;
    }
    if (frmPid == 0) {
        return false;
    }
    return placeObject(proPid, frmPid, hex, direction);
}

bool MapScriptApi::paintFloor(int tileIndex, uint16_t tileId) {
    return paintTile(tileIndex, tileId, false);
}

bool MapScriptApi::paintRoof(int tileIndex, uint16_t tileId) {
    return paintTile(tileIndex, tileId, true);
}

bool MapScriptApi::paintTile(int tileIndex, uint16_t tileId, bool isRoof) {
    if (tileIndex < 0 || tileIndex >= HexagonGrid::TILE_COUNT) {
        return false;
    }
    const uint16_t before = isRoof ? getRoof(tileIndex) : getFloor(tileIndex);
    std::vector<TileChange> changes{ { _elevation, tileIndex, isRoof, before, tileId } };
    _controller.applyTileChanges(changes, true);
    _controller.registerTileEdit(isRoof ? "Script: paint roof" : "Script: paint floor", changes);
    ++_paintedTiles;
    return true;
}

} // namespace geck
