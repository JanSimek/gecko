#pragma once

#include <cstddef>
#include <map>
#include <vector>

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

    /// Parse one object record, with its inventory read recursively, from `data` at `offset`: the
    /// layout the engine's _obj_load_obj reads. Save files embed such records outside any map (the
    /// player's own object in SAVE.DAT), which is why this is public. `endOffset` receives the position
    /// just past the record. Throws like read() on a malformed record.
    std::unique_ptr<MapObject> readObjectAt(const std::vector<uint8_t>& data, std::size_t offset, std::size_t& endOffset);
};

} // namespace geck
