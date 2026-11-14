#include "MapClipboard.h"

#include <algorithm>
#include <array>
#include <limits>
#include <unordered_map>

#include "editor/HexGeometry.h"
#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "format/map/MapObject.h"
#include "format/map/Tile.h"
#include "selection/SelectionState.h"

namespace geck {

namespace {

    using ScriptSlots = std::vector<std::optional<MapClipboard::ScriptLink>>;
    using NextScriptIndex = std::array<uint32_t, Map::SCRIPT_SECTIONS>;

    // The program `object`'s script runs, read from its record in `map`. Spatial scripts belong to
    // no object and are never copied; a SID with no record is dropped rather than carried along.
    std::optional<MapClipboard::ScriptLink> findScriptLink(const MapObject& object, const Map::MapFile& map) {
        if (object.map_scripts_pid == -1) {
            return std::nullopt;
        }
        const auto sid = static_cast<uint32_t>(object.map_scripts_pid);
        const int section = MapScript::sidSection(sid);
        if (section < 0 || section >= Map::SCRIPT_SECTIONS || section == static_cast<int>(MapScript::ScriptType::SPATIAL)) {
            return std::nullopt;
        }
        for (const MapScript& script : map.map_scripts[section]) {
            if (script.pid == sid) {
                return MapClipboard::ScriptLink{ section, script.script_id };
            }
        }
        return std::nullopt;
    }

    // Record each object's script (pre-order) and strip the source SID: it only means something in
    // the source map, and place() hands every scripted copy a record of its own.
    void captureScripts(MapObject& object, const Map::MapFile& map, ScriptSlots& out) {
        out.push_back(findScriptLink(object, map));
        object.map_scripts_pid = -1;
        for (const auto& item : object.inventory) {
            if (item) {
                captureScripts(*item, map, out);
            }
        }
    }

    // Same walk as captureScripts: a fresh object id for every object in the copy, and a new record
    // (next SID in its section, owner's new id as OID) wherever the source had a script.
    void assignIds(MapObject& object, const ScriptSlots& slots, std::size_t& slot, uint32_t& nextId,
        NextScriptIndex& nextScript, std::vector<MapScript>& out) {
        object.unknown0 = nextId++;
        if (slot < slots.size() && slots[slot].has_value()) {
            const MapClipboard::ScriptLink& link = *slots[slot];
            const MapScript script = MapScript::makeObjectScript(static_cast<MapScript::ScriptType>(link.section),
                nextScript[link.section]++, link.programIndex, object.unknown0);
            object.map_scripts_pid = static_cast<int32_t>(script.pid);
            out.push_back(script);
        }
        ++slot;
        for (const auto& item : object.inventory) {
            if (item) {
                assignIds(*item, slots, slot, nextId, nextScript, out);
            }
        }
    }

    void maxObjectId(const MapObject& object, uint32_t& maxId) {
        if (object.unknown0 != MapScript::NONE) {
            maxId = std::max(maxId, object.unknown0);
        }
        for (const auto& item : object.inventory) {
            if (item) {
                maxObjectId(*item, maxId);
            }
        }
    }

    // One past the highest object id in use, as ScriptEditService::allocateObjectId counts it (every
    // object and every script's OID), also counting inventory items so a copy can't reuse theirs.
    // The engine's -1 sentinel is skipped so it can't wrap the next id round to 0.
    uint32_t nextObjectId(const Map::MapFile& map) {
        uint32_t maxId = 0;
        for (const auto& [elevation, objects] : map.map_objects) {
            for (const auto& object : objects) {
                if (object) {
                    maxObjectId(*object, maxId);
                }
            }
        }
        for (const auto& section : map.map_scripts) {
            for (const MapScript& script : section) {
                if (script.script_oid != MapScript::NONE) {
                    maxId = std::max(maxId, script.script_oid);
                }
            }
        }
        return maxId + 1;
    }

    // One past the highest script index in each section (ScriptEditService::allocateScriptId).
    NextScriptIndex nextScriptIndices(const Map::MapFile& map) {
        NextScriptIndex next{};
        for (int section = 0; section < Map::SCRIPT_SECTIONS; ++section) {
            for (const MapScript& script : map.map_scripts[section]) {
                next[section] = std::max(next[section], MapScript::sidIndex(script.pid) + 1);
            }
        }
        return next;
    }

    uint16_t tileAt(const Map::MapFile& map, int elevation, int tileIndex, bool isRoof) {
        const auto it = map.tiles.find(elevation);
        if (it == map.tiles.end() || tileIndex < 0 || tileIndex >= static_cast<int>(it->second.size())) {
            return static_cast<uint16_t>(Map::EMPTY_TILE);
        }
        return isRoof ? it->second[tileIndex].getRoof() : it->second[tileIndex].getFloor();
    }

    bool tileOnGrid(int col, int row) {
        return col >= 0 && col < HexagonGrid::TILE_GRID_WIDTH && row >= 0 && row < HexagonGrid::TILE_GRID_HEIGHT;
    }

} // namespace

std::size_t MapClipboard::Placement::scriptCount() const {
    std::size_t count = 0;
    for (const Entry& entry : objects) {
        count += entry.scripts.size();
    }
    return count;
}

bool MapClipboard::capture(const std::vector<const MapObject*>& objects,
    const std::vector<TileSelection>& floorTiles,
    const std::vector<TileSelection>& roofTiles,
    const Map::MapFile& sourceMap) {
    using namespace geck::hexgrid;
    clear();

    // Bounds of everything copied, in hex space; a tile covers a 2x2 block of hexes.
    int minCol = std::numeric_limits<int>::max();
    int minRow = std::numeric_limits<int>::max();
    int maxCol = std::numeric_limits<int>::min();
    int maxRow = std::numeric_limits<int>::min();
    const auto extend = [&](int col, int row) {
        minCol = std::min(minCol, col);
        minRow = std::min(minRow, row);
        maxCol = std::max(maxCol, col);
        maxRow = std::max(maxRow, row);
    };

    std::vector<const MapObject*> kept;
    for (const MapObject* object : objects) {
        if (object == nullptr || object->position < 0 || object->position >= WIDTH * HEIGHT) {
            continue;
        }
        kept.push_back(object);
        extend(columnOf(object->position), rowOf(object->position));
    }

    const auto keepTiles = [&](const std::vector<TileSelection>& src) {
        std::vector<TileSelection> out;
        for (const auto& [tileIndex, tileId] : src) {
            if (tileId == static_cast<uint16_t>(Map::EMPTY_TILE) || tileIndex < 0 || tileIndex >= HexagonGrid::TILE_COUNT) {
                continue;
            }
            const int col = tileIndex % HexagonGrid::TILE_GRID_WIDTH;
            const int row = tileIndex / HexagonGrid::TILE_GRID_WIDTH;
            extend(col * 2, row * 2);
            extend(col * 2 + 1, row * 2 + 1);
            out.emplace_back(tileIndex, tileId);
        }
        return out;
    };
    const std::vector<TileSelection> floor = keepTiles(floorTiles);
    const std::vector<TileSelection> roof = keepTiles(roofTiles);

    if (kept.empty() && floor.empty() && roof.empty()) {
        return false;
    }

    // Centre of the bounds, rounded down to an even column and row so the anchor sits on a tile
    // corner and tile offsets divide exactly.
    int anchorCol = (minCol + maxCol) / 2;
    int anchorRow = (minRow + maxRow) / 2;
    anchorCol -= anchorCol & 1;
    anchorRow -= anchorRow & 1;
    _anchorHex = anchorRow * WIDTH + anchorCol;

    _objects.reserve(kept.size());
    for (const MapObject* object : kept) {
        std::unique_ptr<MapObject> copy = object->cloneDeep();
        ClipObject clip;
        clip.dCol = columnOf(object->position) - anchorCol;
        clip.dRow = rowOf(object->position) - anchorRow;
        captureScripts(*copy, sourceMap, clip.scripts);
        clip.object = std::move(copy);
        _objects.push_back(std::move(clip));
    }

    const int anchorTileCol = anchorCol / 2;
    const int anchorTileRow = anchorRow / 2;
    const auto addTiles = [&](const std::vector<TileSelection>& src, std::vector<ClipTile>& dst) {
        dst.reserve(src.size());
        for (const auto& [tileIndex, tileId] : src) {
            dst.push_back(ClipTile{ tileIndex % HexagonGrid::TILE_GRID_WIDTH - anchorTileCol,
                tileIndex / HexagonGrid::TILE_GRID_WIDTH - anchorTileRow, tileId });
        }
    };
    addTiles(floor, _floor);
    addTiles(roof, _roof);
    return true;
}

bool MapClipboard::captureSelection(const selection::SelectionState& selection, const Map::MapFile& sourceMap, int elevation) {
    std::vector<const MapObject*> objects;
    for (const auto& object : selection.getObjects()) {
        if (object && object->hasMapObject()) {
            objects.push_back(object->getMapObjectPtr().get());
        }
    }

    // Keep the map's own object order, not the order things were clicked in, so objects sharing a
    // hex still stack the same way after a paste.
    if (const auto it = sourceMap.map_objects.find(elevation); it != sourceMap.map_objects.end()) {
        std::unordered_map<const MapObject*, std::size_t> order;
        for (std::size_t i = 0; i < it->second.size(); ++i) {
            order.emplace(it->second[i].get(), i);
        }
        const auto rank = [&order](const MapObject* object) {
            const auto found = order.find(object);
            return found == order.end() ? order.size() : found->second;
        };
        std::ranges::stable_sort(objects, {}, rank);
    }

    const auto tilesIt = sourceMap.tiles.find(elevation);
    const auto readTiles = [&](const std::vector<int>& indices, bool isRoof) {
        std::vector<TileSelection> out;
        out.reserve(indices.size());
        for (int index : indices) {
            uint16_t tileId = static_cast<uint16_t>(Map::EMPTY_TILE);
            if (tilesIt != sourceMap.tiles.end() && index >= 0 && index < static_cast<int>(tilesIt->second.size())) {
                tileId = isRoof ? tilesIt->second[index].getRoof() : tilesIt->second[index].getFloor();
            }
            out.emplace_back(index, tileId);
        }
        return out;
    };

    return capture(objects, readTiles(selection.getFloorTileIndices(), false),
        readTiles(selection.getRoofTileIndices(), true), sourceMap);
}

void MapClipboard::clear() {
    _objects.clear();
    _floor.clear();
    _roof.clear();
    _anchorHex = 0;
}

bool MapClipboard::isEmpty() const {
    return _objects.empty() && _floor.empty() && _roof.empty();
}

int MapClipboard::snapTarget(int targetHex) {
    using namespace geck::hexgrid;
    // PatternStamper::plan's rule with an always-even anchor: an odd column or row steps one hex
    // towards zero (never below it, since an odd coordinate is at least 1).
    int col = columnOf(targetHex);
    int row = rowOf(targetHex);
    col -= col & 1;
    row -= row & 1;
    return row * WIDTH + col;
}

MapClipboard::Resolved MapClipboard::resolve(int targetHex) const {
    using namespace geck::hexgrid;
    Resolved r;
    r.anchorHex = snapTarget(targetHex);
    const int col = columnOf(r.anchorHex);
    const int row = rowOf(r.anchorHex);

    // Both anchors have an even column, so a plain offset keeps the shape exactly (it is the same
    // translation hexgrid::translate makes in cube space).
    r.objectHexes.reserve(_objects.size());
    for (const ClipObject& clip : _objects) {
        const int c = col + clip.dCol;
        const int rr = row + clip.dRow;
        r.objectHexes.push_back(c >= 0 && c < WIDTH && rr >= 0 && rr < HEIGHT ? rr * WIDTH + c : -1);
    }

    const int tileCol = col / 2;
    const int tileRow = row / 2;
    const auto resolveTiles = [&](const std::vector<ClipTile>& src, std::vector<int>& dst) {
        dst.reserve(src.size());
        for (const ClipTile& tile : src) {
            const int c = tileCol + tile.dCol;
            const int rr = tileRow + tile.dRow;
            dst.push_back(tileOnGrid(c, rr) ? rr * HexagonGrid::TILE_GRID_WIDTH + c : -1);
        }
    };
    resolveTiles(_floor, r.floorTiles);
    resolveTiles(_roof, r.roofTiles);
    return r;
}

MapClipboard::Placement MapClipboard::place(int targetHex, int elevation, const Map::MapFile& targetMap) const {
    Placement p;
    const Resolved r = resolve(targetHex);

    uint32_t nextId = nextObjectId(targetMap);
    NextScriptIndex nextScript = nextScriptIndices(targetMap);

    p.objects.reserve(_objects.size());
    for (std::size_t i = 0; i < _objects.size(); ++i) {
        if (r.objectHexes[i] < 0) {
            ++p.objectsDropped;
            continue;
        }
        const ClipObject& clip = _objects[i];
        std::shared_ptr<MapObject> object = clip.object->cloneDeep();
        // Only where it sits changes. Exit grid and stair/ladder destinations point at other places,
        // so they stay exactly as copied.
        object->position = r.objectHexes[i];
        object->elevation = static_cast<uint32_t>(elevation);
        object->critter_index = -1;

        Placement::Entry entry;
        std::size_t slot = 0;
        assignIds(*object, clip.scripts, slot, nextId, nextScript, entry.scripts);
        entry.object = std::move(object);
        p.objects.push_back(std::move(entry));
    }

    const auto placeTiles = [&](const std::vector<ClipTile>& src, const std::vector<int>& targets, bool isRoof) {
        for (std::size_t i = 0; i < src.size(); ++i) {
            if (targets[i] < 0) {
                ++p.tilesDropped;
                continue;
            }
            p.tiles.push_back({ elevation, targets[i], isRoof, tileAt(targetMap, elevation, targets[i], isRoof), src[i].tileId });
        }
    };
    placeTiles(_floor, r.floorTiles, false);
    placeTiles(_roof, r.roofTiles, true);
    return p;
}

} // namespace geck
