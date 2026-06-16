#include "scripting/MapScriptApi.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <unordered_map>

#include "editor/HexGeometry.h"
#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "format/lst/Lst.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/Tile.h"
#include "format/msg/Msg.h"
#include "format/pro/Pro.h"
#include "editor/TileChange.h"
#include "pattern/PatternSprite.h"
#include "reader/map/MapReader.h"
#include "resource/GameResources.h"
#include "resource/ResourcePaths.h"
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
    // A failure to load tiles.lst is a real error (no data mounted) and is raised so the caller
    // sees it; an unknown name in a *loaded* list is a legitimate "not found" -> -1.
    const Lst* lst = _resources.repository().load<Lst>(std::string(ResourcePaths::Lst::TILES));
    if (lst == nullptr) {
        throw ScriptError("tiles.lst is unavailable — are the Fallout 2 data files (master.dat) mounted?");
    }
    // tiles.lst entries are already lowercased/trimmed by the reader; normalise the query the
    // same way so "edg5000", "EDG5000.FRM" and "edg5000.frm" all match.
    std::string needle = name;
    std::ranges::transform(needle, needle.begin(),
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

std::unique_ptr<Map> MapScriptApi::loadReferenceMap(const std::string& mapPath) const {
    // Failing to read/parse the reference is a real error (bad path or no data) — raise it rather
    // than returning null, so the caller isn't handed a silently-empty result.
    const auto bytes = _resources.files().readRawBytes(mapPath);
    if (!bytes) {
        throw ScriptError(std::format("could not read map '{}' — check the path and that Fallout 2 data is mounted", mapPath));
    }
    // The reader needs each object's proto for subtype parsing; resolve them GL-free like the
    // analyzer does. A proto that won't load yields nullptr and the reader skips its extra fields
    // (best-effort per object — not a fatal error for the whole map).
    const std::function<Pro*(uint32_t)> proLoad = [this](uint32_t pid) -> Pro* {
        try {
            return _resources.repository().load<Pro>(ProHelper::basePath(_resources, pid));
        } catch (const std::exception&) {
            return nullptr;
        }
    };
    MapReader reader(proLoad);
    auto map = reader.openFile(mapPath, *bytes); // parse errors propagate
    if (!map) {
        throw ScriptError(std::format("could not parse map '{}'", mapPath));
    }
    return map;
}

bool MapScriptApi::isScatterableScenery(uint32_t pid) const {
    // Flat scenery — the invisible movement-blockers / floor markers (block.frm) carry OBJECT_FLAT,
    // while upright decorations (scrub, trees, rocks) do not — is excluded from a scatter palette.
    const Pro* pro = nullptr;
    try {
        pro = _resources.repository().load<Pro>(ProHelper::basePath(_resources, pid));
    } catch (const std::exception&) {
        return true; // best-effort: keep a proto we can't inspect
    }
    return pro == nullptr || !Pro::hasFlag(pro->header.flags, Pro::ObjectFlags::OBJECT_FLAT);
}

std::map<int, int> MapScriptApi::sceneryCounts(Map& map) const {
    std::map<int, int> counts;
    std::unordered_map<int, bool> eligible; // pid -> scatter-eligible (decided once, then cached)
    for (const auto& [elevation, objects] : map.getMapFile().map_objects) {
        for (const auto& object : objects) {
            if (!object || static_cast<Pro::OBJECT_TYPE>((object->pro_pid >> 24) & 0xFFu) != Pro::OBJECT_TYPE::SCENERY) {
                continue;
            }
            const int pid = static_cast<int>(object->pro_pid);
            auto [it, inserted] = eligible.try_emplace(pid, false);
            if (inserted) {
                it->second = isScatterableScenery(object->pro_pid);
            }
            if (it->second) {
                ++counts[pid];
            }
        }
    }
    return counts;
}

std::vector<int> MapScriptApi::mapScenery(const std::string& mapPath) const {
    const std::unique_ptr<Map> reference = loadReferenceMap(mapPath); // throws if unreadable
    const auto counts = sceneryCounts(*reference);
    std::vector<int> palette;
    palette.reserve(counts.size());
    for (const auto& [pid, count] : counts) {
        palette.push_back(pid);
    }
    return palette;
}

std::map<int, int> MapScriptApi::mapSceneryHistogram(const std::string& mapPath) const {
    const std::unique_ptr<Map> reference = loadReferenceMap(mapPath); // throws if unreadable
    return sceneryCounts(*reference);
}

std::vector<int> MapScriptApi::mapFloorTiles(const std::string& mapPath) const {
    const std::unique_ptr<Map> reference = loadReferenceMap(mapPath); // throws if unreadable

    std::unordered_map<uint16_t, int> counts; // floor tile id -> times used
    for (const auto& [elevation, tiles] : reference->getMapFile().tiles) {
        for (const auto& tile : tiles) {
            if (tile.getFloor() != static_cast<uint16_t>(Map::EMPTY_TILE)) {
                counts[tile.getFloor()]++;
            }
        }
    }

    std::vector<std::pair<uint16_t, int>> sorted(counts.begin(), counts.end());
    std::ranges::sort(sorted, [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first; // most-used first
    });
    std::vector<int> result;
    result.reserve(sorted.size());
    for (const auto& [id, count] : sorted) {
        result.push_back(id);
    }
    return result;
}

std::vector<std::string> MapScriptApi::listMaps() const {
    std::vector<std::string> maps;
    try {
        // Keep the .map files case-insensitively (robust against the DAT/mount's key casing).
        for (const auto& path : _resources.files().list("*")) {
            std::string ext = path.extension().string();
            std::ranges::transform(ext, ext.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext == ".map") {
                maps.push_back(path.generic_string());
            }
        }
        std::ranges::sort(maps);
    } catch (const std::exception&) {
        // no data mounted -> empty list
    }
    return maps;
}

namespace {
    // Smootherstep, for C1-continuous interpolation between lattice values.
    double fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

    // Hash a lattice corner to a stable pseudo-random value in [0,1).
    double latticeValue(int xi, int yi) {
        uint32_t h = static_cast<uint32_t>(xi) * 374761393u + static_cast<uint32_t>(yi) * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= h >> 16;
        return (h & 0xFFFFFFu) / static_cast<double>(0x1000000);
    }
} // namespace

double MapScriptApi::noise2d(double x, double y) const {
    const double xf = std::floor(x);
    const double yf = std::floor(y);
    const int xi = static_cast<int>(xf);
    const int yi = static_cast<int>(yf);
    const double tx = fade(x - xf);
    const double ty = fade(y - yf);

    const double v00 = latticeValue(xi, yi);
    const double v10 = latticeValue(xi + 1, yi);
    const double v01 = latticeValue(xi, yi + 1);
    const double v11 = latticeValue(xi + 1, yi + 1);

    return std::lerp(std::lerp(v00, v10, tx), std::lerp(v01, v11, tx), ty); // bilinear -> [0,1)
}

uint32_t MapScriptApi::proto(const std::string& typeName, int number) const {
    const auto lower = [](std::string s) {
        std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    const std::string wanted = lower(typeName);

    // Match against the engine's own type names (Pro::typeToString) rather than a second hardcoded
    // table — singular ("scenery") or its plural ("walls").
    using enum Pro::OBJECT_TYPE;
    for (const Pro::OBJECT_TYPE type : { ITEM, CRITTER, SCENERY, WALL, TILE, MISC }) {
        const std::string name = lower(Pro::typeToString(type));
        if (wanted == name || wanted == name + "s") {
            // The id occupies the low 24 bits of the PID and is 1-based (proto ids start at 1).
            if (number <= 0 || number > 0x00FFFFFF) {
                throw ScriptError(std::format("proto number out of range (1..16777215): {}", number));
            }
            return (static_cast<uint32_t>(type) << 24) | static_cast<uint32_t>(number);
        }
    }
    throw ScriptError(std::format("unknown proto type '{}' (use item/critter/scenery/wall/tile/misc)", typeName));
}

std::string MapScriptApi::protoName(int pid) const {
    // A proto that can't be loaded (no data / bad pid) is a real error and is raised. A proto that
    // loads but has no name string is a legitimate empty result.
    const Pro* pro = _resources.repository().load<Pro>(ProHelper::basePath(_resources, static_cast<uint32_t>(pid)));
    if (pro == nullptr) {
        throw ScriptError(std::format("proto could not be loaded for pid {}", pid));
    }
    if (Msg* msg = ProHelper::msgFile(_resources, pro->type()); msg != nullptr) {
        return msg->message(pro->header.message_id).text;
    }
    return {};
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
    } catch (const std::exception&) {
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
