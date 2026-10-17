#pragma once

#include <memory>
#include <string>

namespace geck {

class Map;

namespace resource {
    class GameResources;
}

namespace cli {

    /// resource::readMap for the CLI tools: the mounted data first, then the path on disk. Returns
    /// nullptr (logging at debug) if the map can't be read or parsed, so callers can skip or report
    /// as they prefer. Pass `error` to recover the reason, so a skipped map can be reported rather
    /// than silently dropped.
    std::unique_ptr<Map> loadMap(resource::GameResources& resources, const std::string& mapPath,
        std::string* error = nullptr);

} // namespace cli
} // namespace geck
