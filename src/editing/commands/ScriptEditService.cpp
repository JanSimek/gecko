#include "ScriptEditService.h"

#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "editing/commands/UndoBatcher.h"
#include "util/UndoStack.h"

#include <algorithm>

namespace geck {

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

void ScriptEditService::applyScriptSnapshot(int section, const std::shared_ptr<MapObject>& object,
    const std::vector<MapScript>& sectionScripts, uint32_t oid, int32_t sid) {
    if (!_map || section < 0 || section >= Map::SCRIPT_SECTIONS) {
        return;
    }
    auto& mapFile = _map->getMapFile();
    mapFile.map_scripts[section] = sectionScripts;
    mapFile.scripts_in_section[section] = static_cast<int>(sectionScripts.size());
    if (object) {
        object->unknown0 = oid;
        object->map_scripts_pid = sid;
    }
}

bool ScriptEditService::recordScriptEdit(const std::string& description, int section,
    const std::shared_ptr<MapObject>& object,
    std::vector<MapScript> beforeSection, uint32_t beforeOid, int32_t beforeSid) {
    if (!_map) {
        return false;
    }
    // The caller already applied the edit; capture the resulting "after" state.
    std::vector<MapScript> afterSection = _map->getMapFile().map_scripts[section];
    uint32_t afterOid = object ? object->unknown0 : 0;
    int32_t afterSid = object ? object->map_scripts_pid : -1;

    UndoCommand cmd;
    cmd.description = description;
    cmd.undo = [this, section, object, beforeSection = std::move(beforeSection), beforeOid, beforeSid]() {
        applyScriptSnapshot(section, object, beforeSection, beforeOid, beforeSid);
    };
    cmd.redo = [this, section, object, afterSection = std::move(afterSection), afterOid, afterSid]() {
        applyScriptSnapshot(section, object, afterSection, afterOid, afterSid);
    };
    return _batcher.push(std::move(cmd));
}

bool ScriptEditService::attachScript(const std::shared_ptr<MapObject>& object,
    int scriptType, uint32_t programIndex) {
    if (!_map || !object || scriptType < 0 || scriptType >= Map::SCRIPT_SECTIONS) {
        return false;
    }
    const int section = scriptType;
    auto& mapFile = _map->getMapFile();

    std::vector<MapScript> beforeSection = mapFile.map_scripts[section];
    const uint32_t beforeOid = object->unknown0;
    const int32_t beforeSid = object->map_scripts_pid;

    // Replace any existing script (same object type -> same section).
    removeObjectScript(*object);

    const uint32_t scriptId = allocateScriptId(section);
    const uint32_t oid = allocateObjectId();
    MapScript script = MapScript::makeObjectScript(static_cast<MapScript::ScriptType>(scriptType),
        scriptId, programIndex, oid);
    mapFile.map_scripts[section].push_back(script);
    mapFile.scripts_in_section[section] = static_cast<int>(mapFile.map_scripts[section].size());
    object->unknown0 = oid;
    object->map_scripts_pid = static_cast<int32_t>(script.pid);

    return recordScriptEdit("Attach Script", section, object, std::move(beforeSection), beforeOid, beforeSid);
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
    const uint32_t beforeOid = object->unknown0;
    const int32_t beforeSid = object->map_scripts_pid;

    removeObjectScript(*object);

    return recordScriptEdit("Detach Script", section, object, std::move(beforeSection), beforeOid, beforeSid);
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

    return recordScriptEdit("Add Spatial Script", section, nullptr, std::move(beforeSection), 0, -1);
}

} // namespace geck
