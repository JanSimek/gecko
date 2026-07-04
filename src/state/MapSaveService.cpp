#include "state/MapSaveService.h"

#include "format/pro/Pro.h"
#include "reader/map/MapEdgeReader.h"
#include "resource/GameResources.h"
#include "writer/map/MapEdgeWriter.h"
#include "writer/map/MapWriter.h"

namespace geck {

std::optional<std::size_t> saveMapToFile(resource::GameResources& resources,
    const Map::MapFile& mapFile,
    const std::filesystem::path& path) {
    MapWriter writer{ [&resources](int32_t pid) {
        return resources.loadPro(pid);
    } };

    writer.openFile(path.string());
    if (!writer.write(mapFile)) {
        return std::nullopt;
    }
    return writer.getBytesWritten();
}

std::optional<std::size_t> saveMapEdgeBeside(const std::optional<MapEdge>& edge,
    const std::filesystem::path& mapPath) {
    // The engine writes no .EDG for a zero-zone edge (mapEdgeSave), and a header-only file
    // does not parse — so an absent or empty edge is a deliberate no-op, not a failure.
    if (!edge || edge->empty()) {
        return std::nullopt;
    }

    MapEdgeWriter writer;
    writer.openFile(MapEdgeReader::siblingPath(mapPath));
    if (!writer.write(*edge)) {
        return std::nullopt;
    }
    return writer.getBytesWritten();
}

} // namespace geck
