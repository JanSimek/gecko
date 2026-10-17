#include "cli/MapLoad.h"

#include "format/map/Map.h"
#include "resource/MapFileIO.h"

#include <spdlog/spdlog.h>

#include <exception>

namespace geck::cli {

std::unique_ptr<Map> loadMap(resource::GameResources& resources, const std::string& mapPath,
    std::string* error) {
    try {
        return resource::readMap(resources, mapPath);
    } catch (const std::exception& e) {
        spdlog::debug("loadMap: {}: {}", mapPath, e.what());
        if (error != nullptr) {
            *error = e.what();
        }
        return nullptr;
    }
}

} // namespace geck::cli
