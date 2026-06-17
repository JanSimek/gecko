#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace geck {

class Map;
class Pro;

namespace resource {
    class GameResources;
}

namespace cli {

    /// A proto loader for MapReader: resolves a PID to its base proto via the repository, returning
    /// nullptr (and logging at debug) on failure. Shared so every headless map load resolves protos
    /// the same way.
    std::function<Pro*(std::uint32_t)> makeProtoLoader(resource::GameResources& resources);

    /// Read and parse a map from the mounted data. Returns nullptr (logging at debug) if the map can't
    /// be read or parsed, so callers can skip or report as they prefer. Shared by analyze and render.
    std::unique_ptr<Map> loadMap(resource::GameResources& resources, const std::string& mapPath);

} // namespace cli
} // namespace geck
