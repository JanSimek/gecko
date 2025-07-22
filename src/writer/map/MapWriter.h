#pragma once

#include <cstdint>
#include <functional>

#include "../FileWriter.h"
#include "../../format/map/Map.h"

namespace geck {

class Pro;

class MapWriter : public FileWriter<Map::MapFile> {
public:
    MapWriter(std::function<Pro*(int32_t PID)> loadProCallback)
        : _loadProCallback(loadProCallback) { }
    virtual ~MapWriter() = default;

    [[nodiscard]] bool write(const Map::MapFile& map);

private:
    void writeScript(const MapScript& script);
    void writeObject(const MapObject& object);

    std::function<Pro*(int32_t PID)> _loadProCallback;
};

} // namespace geck
