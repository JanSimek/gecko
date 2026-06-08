#pragma once

#include <map>

#include "reader/FileParser.h"
#include "format/map/MapObject.h"
#include "format/map/MapScript.h"
#include "format/lst/Lst.h"

namespace geck {

class Map;
class Pro;
class Tile;

class MapReader : public FileParser<Map> {
public:
    MapReader(std::function<Pro*(uint32_t PID)> proLoadCallback);

private:
    std::unique_ptr<MapObject> readMapObject();
    MapScript::ScriptType fromPid(uint32_t val);

    std::function<Pro*(uint32_t PID)> _proLoadCallback;

public:
    std::unique_ptr<Map> read() override;
};

} // namespace geck
