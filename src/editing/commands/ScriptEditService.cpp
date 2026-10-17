#include "ScriptEditService.h"

#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "editing/commands/UndoBatcher.h"
#include "util/BuiltTile.h"
#include "util/UndoStack.h"

#include <algorithm>
#include <memory>

namespace geck {

namespace {
    // The map_scripts section index for spatial scripts (== the SPATIAL script type).
    constexpr int SPATIAL_SECTION = static_cast<int>(MapScript::ScriptType::SPATIAL);
} // namespace

ScriptEditService::ScriptEditService(std::unique_ptr<Map>& map, UndoBatcher& batcher)
    : _map(map)
    , _batcher(batcher) {
}

uint32_t ScriptEditService::allocateScriptId(int section) const {
    if (!_map || section < 0 || section >= Map::SCRIPT_SECTIONS) {
        return 0;
    }
    uint32_t nextId = 0;
    for (const auto& s : _map->getMapFile().map_scripts[section]) {
        nextId = std::max(nextId, MapScript::sidIndex(s.pid) + 1);
    }
    return nextId;
}

uint32_t ScriptEditService::allocateObjectId() const {
    if (!_map) {
        return 1;
    }
    uint32_t maxId = 0;
    const auto& mapFile = _map->getMapFile();
    for (const auto& [elevation, objects] : mapFile.map_objects) {
        for (const auto& obj : objects) {
            if (obj) {
                maxId = std::max(maxId, obj->unknown0);
            }
        }
    }
    for (int section = 0; section < Map::SCRIPT_SECTIONS; ++section) {
        for (const auto& s : mapFile.map_scripts[section]) {
            maxId = std::max(maxId, s.script_oid);
        }
    }
    return maxId + 1;
}

ScriptEditService::ScriptSections ScriptEditService::snapshotScripts() const {
    static_assert(SCRIPT_SECTIONS == Map::SCRIPT_SECTIONS,
        "ScriptSections snapshot size must match Map::SCRIPT_SECTIONS");
    ScriptSections snapshot;
    const auto& mapFile = _map->getMapFile();
    for (int section = 0; section < Map::SCRIPT_SECTIONS; ++section) {
        snapshot.sections[section] = mapFile.map_scripts[section];
        snapshot.counts[section] = mapFile.scripts_in_section[section];
    }
    return snapshot;
}

void ScriptEditService::restoreScripts(const ScriptSections& snapshot) {
    auto& mapFile = _map->getMapFile();
    for (int section = 0; section < Map::SCRIPT_SECTIONS; ++section) {
        mapFile.map_scripts[section] = snapshot.sections[section];
        mapFile.scripts_in_section[section] = snapshot.counts[section];
    }
}

void ScriptEditService::eraseScript(uint32_t sid) {
    const int section = MapScript::sidSection(sid);
    if (section < 0 || section >= Map::SCRIPT_SECTIONS) {
        return;
    }
    auto& vec = _map->getMapFile().map_scripts[section];
    std::erase_if(vec, [sid](const MapScript& s) { return s.pid == sid; });
    _map->getMapFile().scripts_in_section[section] = static_cast<int>(vec.size());
}

void ScriptEditService::removeObjectScript(MapObject& object) {
    if (!_map || object.map_scripts_pid == -1) {
        return;
    }
    eraseScript(static_cast<uint32_t>(object.map_scripts_pid));
    object.map_scripts_pid = -1;
}

ScriptEditService::OwnerState ScriptEditService::ownerState(const MapObject* object) {
    return object ? OwnerState{ object->unknown0, object->map_scripts_pid, object->script_id } : OwnerState{};
}

void ScriptEditService::applyScriptSnapshot(int section, const std::shared_ptr<MapObject>& object,
    const std::vector<MapScript>& sectionScripts, const OwnerState& owner) {
    if (!_map || section < 0 || section >= Map::SCRIPT_SECTIONS) {
        return;
    }
    auto& mapFile = _map->getMapFile();
    mapFile.map_scripts[section] = sectionScripts;
    mapFile.scripts_in_section[section] = static_cast<int>(sectionScripts.size());
    if (object) {
        object->unknown0 = owner.oid;
        object->map_scripts_pid = owner.sid;
        object->script_id = owner.scriptIndex;
    }
}

bool ScriptEditService::recordScriptEdit(const std::string& description, int section,
    const std::shared_ptr<MapObject>& object,
    std::vector<MapScript> beforeSection, const OwnerState& before) {
    if (!_map) {
        return false;
    }
    // The caller already applied the edit; capture the resulting "after" state.
    std::vector<MapScript> afterSection = _map->getMapFile().map_scripts[section];
    const OwnerState after = ownerState(object.get());

    UndoCommand cmd;
    cmd.description = description;
    cmd.undo = [this, section, object, beforeSection = std::move(beforeSection), before]() {
        applyScriptSnapshot(section, object, beforeSection, before);
    };
    cmd.redo = [this, section, object, afterSection = std::move(afterSection), after]() {
        applyScriptSnapshot(section, object, afterSection, after);
    };
    return _batcher.push(std::move(cmd));
}

bool ScriptEditService::attachScript(const std::shared_ptr<MapObject>& object,
    int scriptType, uint32_t programIndex) {
    if (scriptType < 0 || scriptType >= Map::SCRIPT_SECTIONS) {
        return false;
    }
    return attach(object, static_cast<MapScript::ScriptType>(scriptType), programIndex, "Attach Script");
}

bool ScriptEditService::attachProtoScript(const std::shared_ptr<MapObject>& object, uint32_t protoSid) {
    const int section = MapScript::sidSection(protoSid);
    if (protoSid == MapScript::NONE || section >= Map::SCRIPT_SECTIONS) {
        return false;
    }
    return attach(object, static_cast<MapScript::ScriptType>(section), MapScript::sidIndex(protoSid), "Attach Proto Script");
}

bool ScriptEditService::attach(const std::shared_ptr<MapObject>& object, MapScript::ScriptType type,
    uint32_t programIndex, const std::string& description) {
    if (!_map || !object) {
        return false;
    }
    const auto section = static_cast<int>(type);
    auto& mapFile = _map->getMapFile();

    std::vector<MapScript> beforeSection = mapFile.map_scripts[section];
    const OwnerState before = ownerState(object.get());

    // Replace any existing script (same object type -> same section).
    removeObjectScript(*object);

    const uint32_t scriptId = allocateScriptId(section);
    const uint32_t oid = allocateObjectId();
    MapScript script = MapScript::makeObjectScript(type, scriptId, programIndex, oid);
    if (type == MapScript::ScriptType::SPATIAL) {
        // fallout2-ce objectSetScript / objectSetScriptFromProto: a spatial script sits on its owner's hex.
        script.timer = built_tile::create(static_cast<uint32_t>(object->position), object->elevation);
        script.spatial_radius = 3;
    }
    mapFile.map_scripts[section].push_back(script);
    mapFile.scripts_in_section[section] = static_cast<int>(mapFile.map_scripts[section].size());
    object->unknown0 = oid;
    object->map_scripts_pid = static_cast<int32_t>(script.pid);
    if (object->objectType() == ObjectType::Critter) {
        object->script_id = static_cast<int32_t>(programIndex); // the engine's obj->scriptIndex
    }

    return recordScriptEdit(description, section, object, std::move(beforeSection), before);
}

bool ScriptEditService::detachScript(const std::shared_ptr<MapObject>& object) {
    if (!_map || !object || object->map_scripts_pid == -1) {
        return false;
    }
    const int section = MapScript::sidSection(static_cast<uint32_t>(object->map_scripts_pid));
    if (section < 0 || section >= Map::SCRIPT_SECTIONS) {
        return false;
    }

    std::vector<MapScript> beforeSection = _map->getMapFile().map_scripts[section];
    const OwnerState before = ownerState(object.get());

    removeObjectScript(*object);

    return recordScriptEdit("Detach Script", section, object, std::move(beforeSection), before);
}

bool ScriptEditService::addSpatialScript(uint32_t programIndex, int tile, int elevation, int radius) {
    if (!_map) {
        return false;
    }
    const int section = static_cast<int>(MapScript::ScriptType::SPATIAL);
    auto& mapFile = _map->getMapFile();

    std::vector<MapScript> beforeSection = mapFile.map_scripts[section];

    const uint32_t scriptId = allocateScriptId(section);
    const uint32_t oid = allocateObjectId();
    MapScript script = MapScript::makeSpatialScript(scriptId, programIndex,
        static_cast<uint32_t>(tile), static_cast<uint32_t>(elevation), static_cast<uint32_t>(radius), oid);
    mapFile.map_scripts[section].push_back(script);
    mapFile.scripts_in_section[section] = static_cast<int>(mapFile.map_scripts[section].size());

    return recordScriptEdit("Add Spatial Script", section, nullptr, std::move(beforeSection), {});
}

const MapScript* ScriptEditService::findSpatialScript(uint32_t sid) const {
    if (!_map || MapScript::fromPid(sid) != MapScript::ScriptType::SPATIAL) {
        return nullptr;
    }
    const auto& vec = _map->getMapFile().map_scripts[SPATIAL_SECTION];
    const auto it = std::ranges::find_if(vec, [sid](const MapScript& s) { return s.pid == sid; });
    return it == vec.end() ? nullptr : std::to_address(it);
}

bool ScriptEditService::editSpatialScript(uint32_t sid, uint32_t programIndex,
    int tile, int elevation, int radius) {
    if (!_map || MapScript::fromPid(sid) != MapScript::ScriptType::SPATIAL) {
        return false;
    }
    auto& vec = _map->getMapFile().map_scripts[SPATIAL_SECTION];
    const auto it = std::ranges::find_if(vec, [sid](const MapScript& s) { return s.pid == sid; });
    if (it == vec.end()) {
        return false;
    }

    std::vector<MapScript> beforeSection = vec;
    // In-place: keep pid and script_oid, update program index, packed position and radius.
    it->script_id = programIndex;
    it->timer = built_tile::create(static_cast<uint32_t>(tile), static_cast<uint32_t>(elevation));
    it->spatial_radius = static_cast<uint32_t>(radius);

    return recordScriptEdit("Edit Spatial Script", SPATIAL_SECTION, nullptr, std::move(beforeSection), {});
}

bool ScriptEditService::removeSpatialScript(uint32_t sid) {
    if (!_map || MapScript::fromPid(sid) != MapScript::ScriptType::SPATIAL) {
        return false;
    }
    const auto& vec = _map->getMapFile().map_scripts[SPATIAL_SECTION];
    std::vector<MapScript> beforeSection = vec;
    const std::size_t sizeBefore = vec.size();
    eraseScript(sid);
    if (vec.size() == sizeBefore) {
        return false; // nothing matched
    }
    return recordScriptEdit("Delete Spatial Script", SPATIAL_SECTION, nullptr, std::move(beforeSection), {});
}

} // namespace geck
