#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

#include "format/map/Map.h"

namespace geck {

class Pro;

namespace resource {

    class GameResources;

    /// Where readMap looks for a map, and for the .EDG sidecar beside it.
    enum class MapSource {
        DataPathsThenDisk, ///< the mounted data, then the path on disk (e.g. a map written after mounting)
        DataPaths,
        Disk,
    };

    /// PID -> base proto for MapReader and MapWriter, or nullptr when it can't be loaded. The reader
    /// and writer turn a missing proto into an error naming the PID for the object types whose
    /// layout depends on it, and need nothing from it for the others.
    [[nodiscard]] std::function<Pro*(std::uint32_t)> protoLoader(GameResources& resources);

    /// Reads and parses a map, inflating a save-slot copy (those are gzip files), and attaches the
    /// sibling ".EDG" (or ".edg") read from the same place. Shared by the editor and gecko-cli/MCP.
    /// Throws IOException when the map isn't found and ParseException when it doesn't parse.
    [[nodiscard]] std::unique_ptr<Map> readMap(GameResources& resources, const std::filesystem::path& path,
        MapSource source = MapSource::DataPathsThenDisk);

    /// Writes the map file. Returns its byte count, or nullopt when MapWriter::write reports failure;
    /// propagates FileWriterException and the other errors raised while writing.
    std::optional<std::size_t> writeMapFile(GameResources& resources, const Map::MapFile& mapFile,
        const std::filesystem::path& path);

    /// Writes `edge` as the ".EDG" beside `mapPath` (ARROYO.MAP -> ARROYO.EDG). Like the engine's
    /// mapEdgeSave, it writes nothing for a missing or zero-zone edge and returns nullopt then.
    std::optional<std::size_t> writeMapEdgeBeside(const std::optional<MapEdge>& edge, const std::filesystem::path& mapPath);

    /// writeMapFile, then writeMapEdgeBeside for the map's own edge.
    std::optional<std::size_t> writeMap(GameResources& resources, const Map& map, const std::filesystem::path& path);

} // namespace resource
} // namespace geck
