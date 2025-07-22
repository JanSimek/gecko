#pragma once

#include <cstdint>
#include <functional>

#include "../FileWriter.h"
#include "../../format/map/Map.h"
#include "../../format/map/MapScript.h"
#include "../../format/map/MapObject.h"

namespace geck {

class Pro;

class MapWriter : public FileWriter<Map::MapFile> {
public:
    MapWriter(std::function<Pro*(int32_t PID)> loadProCallback)
        : _loadProCallback(loadProCallback) { }
    virtual ~MapWriter() = default;

    bool write(const Map::MapFile& map) override;

private:
    void writeScript(const MapScript& script);
    void writeObject(const MapObject& object);

    std::function<Pro*(int32_t PID)> _loadProCallback;
};

} // namespace geck
