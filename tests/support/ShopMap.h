#pragma once

#include <cstdint>

#include "format/map/Map.h"
#include "format/map/MapScript.h"
#include "format/pro/ProtoTypes.h"

#include "support/MapObjectBuilder.h"

namespace geck::test {

/// A small map for the CLI and MCP tools: a scripted shopkeeper with a wall beside it, a second
/// critter four hexes east and a far wall, all on elevation 0. Critters and walls read back without
/// protos, so the tools run on it with no game data mounted.
struct ShopMap {
    static constexpr uint32_t CRITTER_PID = ProtoId(ObjectType::Critter, 4).pid();
    static constexpr uint32_t WALL_PID = ProtoId(ObjectType::Wall, 1).pid();
    static constexpr int SHOPKEEPER_HEX = 20100;
    static constexpr int WALL_HEX = 20101;
    static constexpr int OTHER_CRITTER_HEX = 20104;
    static constexpr int FAR_WALL_HEX = 30000;
    static constexpr int SHOPKEEPER_PROGRAM = 1; // 0-based: scripts.lst line 2
    static constexpr int OBJECT_COUNT = 4;

    static Map::MapFile build() {
        auto mapFile = Map::createEmptyMapFile();
        const uint32_t sid = MapScript::makeSid(MapScript::ScriptType::CRITTER, 0);
        MapScript script{};
        script.pid = sid;
        script.script_id = SHOPKEEPER_PROGRAM;
        mapFile.map_scripts[static_cast<int>(MapScript::ScriptType::CRITTER)].push_back(script);

        addObject(mapFile, CRITTER_PID, SHOPKEEPER_HEX, 0, static_cast<int32_t>(sid));
        addObject(mapFile, CRITTER_PID, OTHER_CRITTER_HEX);
        addObject(mapFile, WALL_PID, WALL_HEX);
        addObject(mapFile, WALL_PID, FAR_WALL_HEX);
        return mapFile;
    }
};

} // namespace geck::test
