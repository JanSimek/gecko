#include "scripting/MapScriptApi.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <set>
#include <unordered_map>

#include "editor/Hex.h"
#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "scripting/EditArea.h"
#include "format/lst/Lst.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/Tile.h"
#include "format/msg/Msg.h"
#include "format/pro/Pro.h"
#include "editor/TileChange.h"
#include "pattern/FillPlan.h"
#include "pattern/PatternSprite.h"
#include "pattern/PatternStamper.h"
#include "reader/map/MapReader.h"
#include "resource/GameResources.h"
#include "resource/ResourcePaths.h"
#include "editing/commands/ObjectCommandController.h"
#include "util/Constants.h"
#include "util/ExitGridDirection.h"
#include "util/HexLine.h"
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
    // Parity-correct cube-coordinate neighbours; shared with the editor's exit-grid edge walk.
    return hexline::hexNeighbors(hex);
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
            return _resources.loadPro(pid);
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
        pro = _resources.loadPro(pid);
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
            if (!object || Pro::typeOfPid(object->pro_pid) != Pro::OBJECT_TYPE::SCENERY) {
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

    // 3D form of latticeValue: a stable [0,1) hash of a (xi, yi, zi) lattice corner.
    double latticeValue3(int xi, int yi, int zi) {
        uint32_t h = static_cast<uint32_t>(xi) * 374761393u + static_cast<uint32_t>(yi) * 668265263u
            + static_cast<uint32_t>(zi) * 2246822519u;
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

double MapScriptApi::noise3d(double x, double y, double z) const {
    const double xf = std::floor(x);
    const double yf = std::floor(y);
    const double zf = std::floor(z);
    const int xi = static_cast<int>(xf);
    const int yi = static_cast<int>(yf);
    const int zi = static_cast<int>(zf);
    const double tx = fade(x - xf);
    const double ty = fade(y - yf);
    const double tz = fade(z - zf);

    // Trilinear blend of the 8 cube-corner lattice values -> [0,1).
    const double x00 = std::lerp(latticeValue3(xi, yi, zi), latticeValue3(xi + 1, yi, zi), tx);
    const double x10 = std::lerp(latticeValue3(xi, yi + 1, zi), latticeValue3(xi + 1, yi + 1, zi), tx);
    const double x01 = std::lerp(latticeValue3(xi, yi, zi + 1), latticeValue3(xi + 1, yi, zi + 1), tx);
    const double x11 = std::lerp(latticeValue3(xi, yi + 1, zi + 1), latticeValue3(xi + 1, yi + 1, zi + 1), tx);
    return std::lerp(std::lerp(x00, x10, ty), std::lerp(x01, x11, ty), tz);
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
            return Pro::makePid(type, static_cast<uint32_t>(number));
        }
    }
    throw ScriptError(std::format("unknown proto type '{}' (use item/critter/scenery/wall/tile/misc)", typeName));
}

std::string MapScriptApi::protoName(int pid) const {
    // A proto that can't be loaded (no data / bad pid) is a real error and is raised. A proto that
    // loads but has no name string is a legitimate empty result.
    const Pro* pro = _resources.loadPro(static_cast<uint32_t>(pid));
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

bool MapScriptApi::registerObject(const std::shared_ptr<MapObject>& mapObject, int hex, uint32_t frmPid, uint32_t direction) {
    // Plan-sink active (preview / area fill): build the object exactly as the commit paths do
    // (GUI needs a resolvable sprite — a fid that won't load is "not placed", same as below;
    // headless records data with a null visual) but RECORD it into the plan instead of committing.
    // PlacementBatch::replay applies the plan later, so the result matches a direct run.
    if (_planSink != nullptr) {
        std::shared_ptr<Object> object;
        if (_buildSprites) {
            object = pattern::buildSpriteObject(_resources, _hexgrid, frmPid, hex, direction);
            if (!object) {
                return false;
            }
            object->setMapObject(mapObject);
        }
        _planSink->objects.push_back({ mapObject, std::move(object) });
        ++_placedObjects;
        return true;
    }

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

    return registerObject(mapObject, hex, frmPid, direction);
}

bool MapScriptApi::placeProto(uint32_t proPid, int hex, uint32_t direction) {
    if (!isValidHex(hex)) {
        return false;
    }
    // The proto header carries the default art FID the engine uses for this object; resolve it
    // so callers place by PID alone. A proto we can't load has no art to place.
    uint32_t frmPid = 0;
    try {
        if (const Pro* pro = _resources.loadPro(proPid);
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
    // Plan-sink active: record the tile change (before captured from the committed map) instead of
    // applying it; PlacementBatch::replay applies it later as part of the one-entry batch.
    if (_planSink != nullptr) {
        _planSink->tiles.push_back({ _elevation, tileIndex, isRoof, before, tileId });
        ++_paintedTiles;
        return true;
    }
    std::vector<TileChange> changes{ { _elevation, tileIndex, isRoof, before, tileId } };
    _controller.applyTileChanges(changes, true);
    _controller.registerTileEdit(isRoof ? "Script: paint roof" : "Script: paint floor", changes);
    ++_paintedTiles;
    return true;
}

// --- Coordinates -------------------------------------------------------------
int MapScriptApi::hexIndex(int col, int row) const {
    if (col < 0 || col >= HexagonGrid::GRID_WIDTH || row < 0 || row >= HexagonGrid::GRID_HEIGHT) {
        return -1;
    }
    return row * HexagonGrid::GRID_WIDTH + col;
}

int MapScriptApi::tileIndex(int col, int row) const {
    if (col < 0 || col >= HexagonGrid::TILE_GRID_WIDTH || row < 0 || row >= HexagonGrid::TILE_GRID_HEIGHT) {
        return -1;
    }
    return row * HexagonGrid::TILE_GRID_WIDTH + col;
}

int MapScriptApi::hexCol(int hex) const {
    return (hex >= 0 && hex < HexagonGrid::POSITION_COUNT) ? hex % HexagonGrid::GRID_WIDTH : -1;
}

int MapScriptApi::hexRow(int hex) const {
    return (hex >= 0 && hex < HexagonGrid::POSITION_COUNT) ? hex / HexagonGrid::GRID_WIDTH : -1;
}

int MapScriptApi::tileCol(int tile) const {
    return (tile >= 0 && tile < HexagonGrid::TILE_COUNT) ? tile % HexagonGrid::TILE_GRID_WIDTH : -1;
}

int MapScriptApi::tileRow(int tile) const {
    return (tile >= 0 && tile < HexagonGrid::TILE_COUNT) ? tile / HexagonGrid::TILE_GRID_WIDTH : -1;
}

uint16_t MapScriptApi::getFloorXY(int col, int row) const {
    return getFloor(tileIndex(col, row));
}

uint16_t MapScriptApi::getRoofXY(int col, int row) const {
    return getRoof(tileIndex(col, row));
}

// --- Selection area ----------------------------------------------------------
bool MapScriptApi::hasArea() const {
    return _area != nullptr;
}

std::vector<int> MapScriptApi::areaHexes() const {
    return _area != nullptr ? _area->hexes : std::vector<int>{};
}

std::vector<int> MapScriptApi::areaFloorTiles() const {
    return _area != nullptr ? _area->floorTiles : std::vector<int>{};
}

std::vector<int> MapScriptApi::areaRoofTiles() const {
    return _area != nullptr ? _area->roofTiles : std::vector<int>{};
}

bool MapScriptApi::areaContainsHex(int hex) const {
    // The area lists are sorted ascending by contract (see EditArea), so membership is a binary search.
    return _area != nullptr && std::ranges::binary_search(_area->hexes, hex);
}

bool MapScriptApi::areaContainsTile(int tileIndex) const {
    return _area != nullptr && std::ranges::binary_search(_area->floorTiles, tileIndex);
}

// --- Deterministic helpers ---------------------------------------------------
uint32_t MapScriptApi::objectAt(int hex) const {
    const auto& byElevation = _map.getMapFile().map_objects;
    const auto it = byElevation.find(_elevation);
    if (it == byElevation.end()) {
        return 0;
    }
    for (const auto& object : it->second) {
        if (object && static_cast<int>(object->position) == hex) {
            return object->pro_pid;
        }
    }
    return 0;
}

double MapScriptApi::rng() {
    // Top 24 bits of the mt19937 word mapped to [0,1). mt19937's sequence is standardised and the
    // shift/divide are integer-exact, so the same seed gives the same draws on every platform —
    // unlike std::uniform_real_distribution, whose output is implementation-defined.
    return (static_cast<uint32_t>(_rng()) >> 8) * (1.0 / 16777216.0);
}

int MapScriptApi::rngInt(int lo, int hi) {
    if (hi < lo) {
        std::swap(lo, hi);
    }
    const uint32_t range = static_cast<uint32_t>(hi - lo) + 1u;
    return lo + static_cast<int>(static_cast<uint32_t>(_rng()) % range);
}

bool MapScriptApi::placeObjectXY(uint32_t proPid, uint32_t frmPid, int col, int row, uint32_t direction) {
    return placeObject(proPid, frmPid, hexIndex(col, row), direction);
}

bool MapScriptApi::placeProtoXY(uint32_t proPid, int col, int row, uint32_t direction) {
    return placeProto(proPid, hexIndex(col, row), direction);
}

bool MapScriptApi::paintFloorXY(int col, int row, uint16_t tileId) {
    return paintFloor(tileIndex(col, row), tileId);
}

bool MapScriptApi::paintRoofXY(int col, int row, uint16_t tileId) {
    return paintRoof(tileIndex(col, row), tileId);
}

void MapScriptApi::addStamp(const std::string& name, pattern::Pattern pattern) {
    _stamps[name] = std::move(pattern);
}

int MapScriptApi::placeStamp(const std::string& name, int anchorHex, int variant) {
    const auto it = _stamps.find(name);
    if (it == _stamps.end()) {
        throw ScriptError("placeStamp: unknown stamp '" + name + "' — register it first: gecko-cli --stamp "
            + name + "=<file>, the MCP generate 'stamps' arg, or save a pattern named '" + name
            + "' in the editor's pattern library (for the Script Console)");
    }
    const pattern::Pattern& pattern = it->second;
    if (variant < 0 || variant >= static_cast<int>(pattern.variants.size())) {
        throw ScriptError("placeStamp: variant " + std::to_string(variant) + " out of range for stamp '" + name + "'");
    }
    pattern::PatternStamper stamper(_resources, _hexgrid, _controller, _map, _buildSprites);
    // Plan-sink active: capture the stamp's built objects/tiles into the plan (committing nothing),
    // so a stamp scattered by a fill is previewed and lands in the fill's single undo entry. Without
    // this, placeStamp would open its own ScopedUndoBatch and mutate the live map during preview.
    if (_planSink != nullptr) {
        const std::size_t objBefore = _planSink->objects.size();
        const std::size_t tileBefore = _planSink->tiles.size();
        stamper.planInto(*_planSink, pattern.variants[variant], anchorHex, _elevation);
        const int placed = static_cast<int>(_planSink->objects.size() - objBefore);
        _placedObjects += placed;
        _paintedTiles += static_cast<int>(_planSink->tiles.size() - tileBefore);
        return placed;
    }
    const pattern::PatternStamper::Result result = stamper.stamp(pattern.variants[variant], anchorHex, _elevation);
    _placedObjects += result.objectsPlaced;
    _paintedTiles += result.tilesPainted;
    return result.objectsPlaced;
}

void MapScriptApi::newMap() {
    // These whole-map/header mutators can't be expressed as a deferred FillPlan (replay only
    // applies object/tile changes), so they must never run while a sink is recording a fill
    // preview — doing so would mutate the live map directly and break preview==apply. Reject with
    // a clear error instead; a fill script paints/scatters, it does not reset the map.
    if (_planSink != nullptr) {
        throw ScriptError("newMap() is not allowed in a fill — it would reset the live map");
    }
    _controller.newEmptyMap();
    _mutatedDirectly = true;
}

void MapScriptApi::setPlayerStart(int hex, int orientation, int elevation) {
    if (_planSink != nullptr) {
        throw ScriptError("setPlayerStart() is not allowed in a fill — header edits aren't part of a fill plan");
    }
    if (!isValidHex(hex)) {
        throw ScriptError("setPlayerStart: hex " + std::to_string(hex) + " is off the 200x200 hex grid");
    }
    if (orientation < 0 || orientation > 5) {
        throw ScriptError("setPlayerStart: orientation must be 0..5, got " + std::to_string(orientation));
    }
    if (elevation < 0 || elevation >= Map::ELEVATION_COUNT) {
        throw ScriptError("setPlayerStart: elevation must be 0.." + std::to_string(Map::ELEVATION_COUNT - 1)
            + ", got " + std::to_string(elevation));
    }
    auto& header = _map.getMapFile().header;
    header.player_default_position = static_cast<uint32_t>(hex);
    header.player_default_orientation = static_cast<uint32_t>(orientation);
    header.player_default_elevation = static_cast<uint32_t>(elevation);
    _mutatedDirectly = true;
}

namespace {
    // The destination fields of an exit grid (hex is validated by the caller via isValidHex). Throws
    // ScriptError on any out-of-range value, so a bad exit can't be written silently.
    void validateExitGridDest(int destMapId, int destHex, int destElevation, int orientation) {
        // -2 (worldmap) and -1 (town map) are the engine's special destinations; a real map id is >= 0.
        if (destMapId < -2) {
            throw ScriptError("placeExitGrid: destMapId must be a map id, -1 (town map) or -2 (worldmap), got "
                + std::to_string(destMapId));
        }
        if (destHex < 0 || destHex >= HexagonGrid::POSITION_COUNT) {
            throw ScriptError("placeExitGrid: destHex must be 0..39999, got " + std::to_string(destHex));
        }
        if (destElevation < 0 || destElevation >= Map::ELEVATION_COUNT) {
            throw ScriptError("placeExitGrid: destElevation must be 0.." + std::to_string(Map::ELEVATION_COUNT - 1)
                + ", got " + std::to_string(destElevation));
        }
        if (orientation < 0 || orientation > 5) {
            throw ScriptError("placeExitGrid: orientation must be 0..5, got " + std::to_string(orientation));
        }
    }

    // The art family for a destination: an inter-map exit (a real map id) is green, a world/town
    // exit is brown. The proto encodes direction, the frm green/brown by this kind.
    ExitGridDestinationKind destinationKindFor(uint32_t dest) {
        return (dest == ExitGrid::WORLD_MAP_EXIT || dest == ExitGrid::TOWN_MAP_EXIT)
            ? ExitGridDestinationKind::WorldMap
            : ExitGridDestinationKind::InterMap;
    }

    // The on-grid hex whose screen position is nearest (sx, sy), or -1 if off the map.
    int nearestHex(const HexagonGrid& grid, int sx, int sy) {
        if (sx < 0 || sy < 0) {
            return -1;
        }
        const uint32_t pos = grid.positionAt(static_cast<uint32_t>(sx), static_cast<uint32_t>(sy));
        return pos == Hex::HEX_OUT_OF_MAP ? -1 : static_cast<int>(pos);
    }
} // namespace

bool MapScriptApi::placeExitGridMarker(int hex, uint32_t proPid, uint32_t frmPid, const ExitDest& dest) {
    auto mapObject = std::make_shared<MapObject>();
    mapObject->position = hex;
    mapObject->elevation = static_cast<uint32_t>(_elevation);
    mapObject->direction = 0;
    if (auto h = _hexgrid.getHexByPosition(static_cast<uint32_t>(hex)); h.has_value()) {
        mapObject->x = static_cast<uint32_t>(h->get().x());
        mapObject->y = static_cast<uint32_t>(h->get().y());
    }
    mapObject->pro_pid = proPid;
    mapObject->frm_pid = frmPid;
    mapObject->exit_map = dest.map;
    mapObject->exit_position = static_cast<uint32_t>(dest.hex);
    mapObject->exit_elevation = static_cast<uint32_t>(dest.elevation);
    mapObject->exit_orientation = static_cast<uint32_t>(dest.orientation);
    return registerObject(mapObject, hex, frmPid, 0);
}

bool MapScriptApi::placeExitGrid(int hex, int destMapId, int destHex, int destElevation, int orientation) {
    if (!isValidHex(hex)) {
        throw ScriptError("placeExitGrid: hex " + std::to_string(hex) + " is off the 200x200 hex grid");
    }
    validateExitGridDest(destMapId, destHex, destElevation, orientation);

    const auto dest = static_cast<uint32_t>(destMapId);
    // A lone scripted exit grid has no edge orientation, so it uses the bottom-edge marker in the
    // family the destination implies (green inter-map / brown world-town).
    const ExitGridArt art = exitGridArtForDirection(ExitGrid::DIR_BOTTOM, destinationKindFor(dest));
    return placeExitGridMarker(hex, art.proPid, art.frmPid, { dest, destHex, destElevation, orientation });
}

int MapScriptApi::placeExitGridRect(int centerHex, int screenHalfWidth, int screenHalfHeight,
    int destMapId, int destHex, int destElevation, int orientation) {
    if (!isValidHex(centerHex)) {
        throw ScriptError("placeExitGridRect: centerHex " + std::to_string(centerHex) + " is off the 200x200 hex grid");
    }
    if (screenHalfWidth <= 0 || screenHalfHeight <= 0) {
        throw ScriptError("placeExitGridRect: screenHalfWidth/Height must be positive");
    }
    validateExitGridDest(destMapId, destHex, destElevation, orientation);

    const auto centre = _hexgrid.getHexByPosition(static_cast<uint32_t>(centerHex));
    if (!centre.has_value()) {
        return 0;
    }
    const int cx = centre->get().x();
    const int cy = centre->get().y();
    const int left = cx - screenHalfWidth;
    const int right = cx + screenHalfWidth;
    const int top = cy - screenHalfHeight;
    const int bottom = cy + screenHalfHeight;
    const auto dstMap = static_cast<uint32_t>(destMapId);
    const ExitDest dest{ dstMap, destHex, destElevation, orientation };
    const ExitGridDestinationKind kind = destinationKindFor(dstMap);

    // The four screen corners; each edge is the gap-free hex line between two of them, walked along
    // the iso staircase so the vertical sides are continuous (not a sparse single column).
    const int tl = nearestHex(_hexgrid, left, top);
    const int tr = nearestHex(_hexgrid, right, top);
    const int bl = nearestHex(_hexgrid, left, bottom);
    const int br = nearestHex(_hexgrid, right, bottom);

    struct Edge {
        int from, to;
        ExitGridArt art;
    };
    // Each rectangle edge uses its cardinal directional art (proto = direction, frm = green/brown by
    // destination kind): the top/bottom edges run as iso-horizontal lines, the left/right as vertical.
    const std::array<Edge, 4> edges{ {
        { tl, tr, exitGridArtForDirection(ExitGrid::DIR_TOP, kind) },
        { bl, br, exitGridArtForDirection(ExitGrid::DIR_BOTTOM, kind) },
        { tl, bl, exitGridArtForDirection(ExitGrid::DIR_LEFT, kind) },
        { tr, br, exitGridArtForDirection(ExitGrid::DIR_RIGHT, kind) },
    } };

    std::set<int> placedHexes; // corners are shared between two edges — place each hex once.
    int placed = 0;
    for (const Edge& edge : edges) {
        for (const int hex : hexline::hexLine(_hexgrid, edge.from, edge.to)) {
            if (placedHexes.insert(hex).second
                && placeExitGridMarker(hex, edge.art.proPid, edge.art.frmPid, dest)) {
                ++placed;
            }
        }
    }
    return placed;
}

} // namespace geck
