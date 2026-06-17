#include "cli/MapLoad.h"

#include "format/map/Map.h"
#include "format/pro/Pro.h"
#include "reader/map/MapReader.h"
#include "resource/GameResources.h"
#include "util/ProHelper.h"

#include <spdlog/spdlog.h>

#include <exception>

namespace geck::cli {

std::function<Pro*(std::uint32_t)> makeProtoLoader(resource::GameResources& resources) {
    return [&resources](std::uint32_t pid) -> Pro* {
        try {
            return resources.repository().load<Pro>(ProHelper::basePath(resources, pid));
        } catch (const std::exception& e) {
            spdlog::debug("proLoad: pid {} failed: {}", pid, e.what());
            return nullptr;
        }
    };
}

std::unique_ptr<Map> loadMap(resource::GameResources& resources, const std::string& mapPath) {
    const auto bytes = resources.files().readRawBytes(mapPath);
    if (!bytes) {
        return nullptr;
    }
    try {
        MapReader reader(makeProtoLoader(resources));
        return reader.openFile(mapPath, *bytes);
    } catch (const std::exception& e) {
        spdlog::debug("loadMap: parse failed for {}: {}", mapPath, e.what());
        return nullptr;
    }
}

} // namespace geck::cli
