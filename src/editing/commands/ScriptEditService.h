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
    bool detachScript(const std::shared_ptr<MapObject>& object);
    bool addSpatialScript(uint32_t programIndex, int tile, int elevation, int radius);

    // Section snapshot/restore + targeted erase, shared with bulk object operations.
    ScriptSections snapshotScripts() const;
    void restoreScripts(const ScriptSections& snapshot);
    /// Removes the script whose pid == sid from its section, updating the count.
    void eraseScript(uint32_t sid);

private:
    uint32_t allocateScriptId(int section) const;
    uint32_t allocateObjectId() const;
    void removeObjectScript(MapObject& object);
    void applyScriptSnapshot(int section, const std::shared_ptr<MapObject>& object,
        const std::vector<MapScript>& sectionScripts, uint32_t oid, int32_t sid);
    bool recordScriptEdit(const std::string& description, int section,
        const std::shared_ptr<MapObject>& object,
        std::vector<MapScript> beforeSection, uint32_t beforeOid, int32_t beforeSid);

    std::unique_ptr<Map>& _map;
    UndoBatcher& _batcher;
};

} // namespace geck
