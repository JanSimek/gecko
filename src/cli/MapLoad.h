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
    /// Pass `error` to recover the reason — the parse exception's message, or a note that the file was
    /// not found — so a skipped map can be reported rather than silently dropped.
    std::unique_ptr<Map> loadMap(resource::GameResources& resources, const std::string& mapPath,
        std::string* error = nullptr);

} // namespace cli
} // namespace geck
