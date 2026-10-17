#include "resource/MapFileIO.h"

#include "format/pro/Pro.h"
#include "reader/ReaderExceptions.h"
#include "reader/map/MapEdgeReader.h"
#include "reader/map/MapReader.h"
#include "resource/GameResources.h"
#include "util/Inflate.h"
#include "writer/map/MapEdgeWriter.h"
#include "writer/map/MapWriter.h"

#include <spdlog/spdlog.h>

#include <array>
#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace geck::resource {

namespace {
    // A file straight off disk; nullopt if it isn't a readable regular file.
    std::optional<std::vector<std::uint8_t>> readDiskBytes(const std::filesystem::path& path) {
        if (std::error_code ec; !std::filesystem::is_regular_file(path, ec)) {
            return std::nullopt;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    std::optional<std::vector<std::uint8_t>> readBytes(GameResources& resources, const std::filesystem::path& path,
        MapSource source) {
        return source == MapSource::Disk ? readDiskBytes(path) : resources.files().readRawBytes(path);
    }

    // The engine writes ".EDG"; a case-sensitive filesystem may hold ".edg" instead. The first sibling
    // that parses wins, so a valid ".edg" beside an unparsable ".EDG" still loads.
    std::optional<MapEdge> readSiblingEdge(GameResources& resources, const std::filesystem::path& mapPath, MapSource source) {
        std::filesystem::path lowerVariant = mapPath;
        lowerVariant.replace_extension(".edg");
        const std::array<std::filesystem::path, 2> candidates{ MapEdgeReader::siblingPath(mapPath), lowerVariant };
        for (const auto& edgePath : candidates) {
            if (const auto bytes = readBytes(resources, edgePath, source)) {
                if (auto edge = MapEdgeReader::tryParse(edgePath, *bytes)) {
                    spdlog::debug("readMap: map-edge sidecar {} ({} zones)", edgePath.string(), edge->totalZones());
                    return edge;
                }
            }
        }
        return std::nullopt;
    }
} // namespace

std::function<Pro*(std::uint32_t)> protoLoader(GameResources& resources) {
    return [&resources](std::uint32_t pid) -> Pro* {
        try {
            return resources.tryLoadPro(pid);
        } catch (const FileReaderException& e) { // present, but doesn't parse
            spdlog::debug("protoLoader: pid {} failed: {}", pid, e.what());
            return nullptr;
        }
    };
}

std::unique_ptr<Map> readMap(GameResources& resources, const std::filesystem::path& path, MapSource source) {
    using enum MapSource;
    MapSource foundIn = source == Disk ? Disk : DataPaths;
    auto bytes = readBytes(resources, path, foundIn);
    if (!bytes && source == DataPathsThenDisk) {
        // The VFS only lists what existed when a path was mounted, so a map written since (one
        // `generate` just produced) or one outside the data is read off disk instead.
        foundIn = Disk;
        bytes = readDiskBytes(path);
    }
    if (!bytes) {
        throw IOException(source == DataPathsThenDisk ? "map not found in the mounted data or on disk"
                                                                 : "map not found",
            path);
    }

    // Save slots hold every map gzip-compressed (fallout2-ce loadsave.cc _GameMap2Slot / _SlotMap2Game);
    // the live copies under data/maps are plain.
    if (isGzip(*bytes)) {
        bytes = inflateStream(*bytes, DeflateContainer::Gzip);
    }
    MapReader reader(protoLoader(resources));
    auto map = reader.openFile(path.string(), *bytes);
    if (!map) {
        throw ParseException("map did not parse", path);
    }
    map->setEdge(readSiblingEdge(resources, path, foundIn));
    return map;
}

std::optional<std::size_t> writeMapFile(GameResources& resources, const Map::MapFile& mapFile,
    const std::filesystem::path& path) {
    // Scenery and item objects take their tail layout from the proto, so the writer needs the same
    // provider as the reader.
    const auto loadProto = protoLoader(resources);
    MapWriter writer{ [&loadProto](int32_t pid) { return loadProto(static_cast<std::uint32_t>(pid)); } };
    writer.openFile(path.string());
    if (!writer.write(mapFile)) {
        return std::nullopt;
    }
    return writer.getBytesWritten();
}

std::optional<std::size_t> writeMapEdgeBeside(const std::optional<MapEdge>& edge, const std::filesystem::path& mapPath) {
    // A header-only .EDG does not parse, so an absent or empty edge is a deliberate no-op.
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

std::optional<std::size_t> writeMap(GameResources& resources, const Map& map, const std::filesystem::path& path) {
    const auto bytes = writeMapFile(resources, map.getMapFile(), path);
    if (bytes.has_value()) {
        writeMapEdgeBeside(map.edge(), path);
    }
    return bytes;
}

} // namespace geck::resource
