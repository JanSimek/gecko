#include "state/MapSaveService.h"

#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "util/ProHelper.h"
#include "writer/map/MapWriter.h"

namespace geck {

std::optional<std::size_t> saveMapToFile(resource::GameResources& resources,
    const Map::MapFile& mapFile,
    const std::filesystem::path& path) {
    MapWriter writer{ [&resources](int32_t pid) {
        return resources.repository().load<Pro>(ProHelper::basePath(resources, pid));
    } };

    writer.openFile(path.string());
    if (!writer.write(mapFile)) {
        return std::nullopt;
    }
    return writer.getBytesWritten();
}

} // namespace geck
