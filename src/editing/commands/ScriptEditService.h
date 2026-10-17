#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "format/map/MapScript.h"

namespace geck {

class Map;
struct MapObject;
class UndoBatcher;

/**
 * @brief Undoable map-script editing (attach/detach/spatial scripts).
 *
 * One of the aggregate services ObjectCommandController delegates to. Owns the
 * script-id/object-id allocation and the section snapshot/restore machinery.
 * snapshotScripts()/restoreScripts()/eraseScript() are public because bulk
 * object operations (clearing an elevation) must prune the scripts attached to
 * the objects they remove.
 */
class ScriptEditService {
public:
    // Number of map_scripts sections; mirrors Map::SCRIPT_SECTIONS (asserted in .cpp).
    static constexpr int SCRIPT_SECTIONS = 5;

    /// Snapshot of all map_scripts sections and their counts, for undoing bulk
    /// edits that touch scripts across sections (e.g. clearing an elevation).
    struct ScriptSections {
        std::array<std::vector<MapScript>, SCRIPT_SECTIONS> sections;
        std::array<int, SCRIPT_SECTIONS> counts{};
    };

    ScriptEditService(std::unique_ptr<Map>& map, UndoBatcher& batcher);

    // Script attachment (undoable). `scriptType` is the MapScript section/type
    // (ITEM for items/scenery/walls, CRITTER for critters); `programIndex` is the
    // scripts.lst line. attachScript replaces any existing script on the object.
    bool attachScript(const std::shared_ptr<MapObject>& object, int scriptType, uint32_t programIndex);
    // Give a new object the script its proto names (`protoSid`, Pro::sid()), as the engine's
    // objectSetScriptFromProto does when it creates one: the SID's type picks the section, its low
    // 24 bits are the program index. Undoable; false for -1 or a type with no section.
    bool attachProtoScript(const std::shared_ptr<MapObject>& object, uint32_t protoSid);
    bool detachScript(const std::shared_ptr<MapObject>& object);
    bool addSpatialScript(uint32_t programIndex, int tile, int elevation, int radius);
    // Edit an existing spatial script in place, keeping its SID (pid) stable so a live selection
    // survives the edit; updates program index, position (tile+elevation) and radius. Undoable.
    bool editSpatialScript(uint32_t sid, uint32_t programIndex, int tile, int elevation, int radius);
    // Remove the spatial script with this SID. Undoable. No-op (returns false) if none matches.
    bool removeSpatialScript(uint32_t sid);
    // The spatial script with this SID, or nullptr — for reading its current values (e.g. to
    // pre-fill an edit dialog). The pointer is invalidated by any script-section mutation.
    const MapScript* findSpatialScript(uint32_t sid) const;

    // Section snapshot/restore + targeted erase, shared with bulk object operations.
    ScriptSections snapshotScripts() const;
    void restoreScripts(const ScriptSections& snapshot);
    /// Removes the script whose pid == sid from its section, updating the count.
    void eraseScript(uint32_t sid);

private:
    // The owner-side fields a script edit changes: OID, SID and (for critters) the engine's
    // obj->scriptIndex. Defaults describe "no owner" for spatial scripts.
    struct OwnerState {
        uint32_t oid = 0;
        int32_t sid = -1;
        int32_t scriptIndex = -1;
    };
    static OwnerState ownerState(const MapObject* object);

    bool attach(const std::shared_ptr<MapObject>& object, MapScript::ScriptType type,
        uint32_t programIndex, const std::string& description);
    uint32_t allocateScriptId(int section) const;
    uint32_t allocateObjectId() const;
    void removeObjectScript(MapObject& object);
    void applyScriptSnapshot(int section, const std::shared_ptr<MapObject>& object,
        const std::vector<MapScript>& sectionScripts, const OwnerState& owner);
    bool recordScriptEdit(const std::string& description, int section,
        const std::shared_ptr<MapObject>& object,
        std::vector<MapScript> beforeSection, const OwnerState& before);

    std::unique_ptr<Map>& _map;
    UndoBatcher& _batcher;
};

} // namespace geck
