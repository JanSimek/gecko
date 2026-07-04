#include "reader/map/MapEdgeReader.h"

#include <string>

#include <spdlog/spdlog.h>

#include "reader/ReaderExceptions.h"

namespace geck {

namespace {

    // Unpacks the EDG v2 clip-sides bitfield (map_edge.cc:189 unpackClipSides).
    MapEdge::ClipSides unpackClipSides(int32_t raw) {
        MapEdge::ClipSides clip;
        clip.bottom = (raw & 1) != 0;
        clip.right = ((raw >> 8) & 1) != 0;
        clip.top = ((raw >> 16) & 1) != 0;
        clip.left = ((raw >> 24) & 1) != 0;
        return clip;
    }

} // namespace

std::optional<int32_t> MapEdgeReader::tryReadInt32() {
    if (_stream.size() - _stream.position() < sizeof(int32_t)) {
        return std::nullopt;
    }
    return read_be_i32();
}

std::optional<MapEdge::Rect> MapEdgeReader::tryReadRect() {
    // A rect is four int32s; treat a partial rect at EOF as no rect at all.
    if (_stream.size() - _stream.position() < 4 * sizeof(int32_t)) {
        return std::nullopt;
    }
    MapEdge::Rect rect;
    rect.left = read_be_i32();
    rect.top = read_be_i32();
    rect.right = read_be_i32();
    rect.bottom = read_be_i32();
    return rect;
}

std::unique_ptr<MapEdge> MapEdgeReader::read() { // Flawfinder: ignore (bounds-checked; see header)
    auto edge = std::make_unique<MapEdge>();

    // Header: 'EDGE' magic, version (1 or 2), reserved (0). Big-endian int32 throughout.
    const uint32_t magic = read_be_u32();
    if (magic != MapEdge::MAGIC) {
        throw ParseException("not an EDGE file (bad magic)", _path, 0);
    }

    const uint32_t version = read_be_u32();
    if (version != 1 && version != 2) {
        throw ParseException("unsupported EDGE version " + std::to_string(version), _path, 4);
    }
    edge->version = static_cast<int>(version);

    const int32_t reserved = read_be_i32();
    if (reserved != 0) {
        throw ParseException("EDGE reserved field must be 0", _path, 8);
    }

    const bool version2 = (version == 2);

    // `levelIndicator` names the elevation the next zone belongs to. It starts at 0 so
    // elevation 0's zones are read first, and each zone's trailing indicator advances it.
    int levelIndicator = 0;

    for (int elevation = 0; elevation < MapEdge::ELEVATION_COUNT; ++elevation) {
        MapEdge::Elevation& data = edge->elevations[elevation];

        if (version2) {
            auto square = tryReadRect();
            auto clip = tryReadInt32();
            if (!square || !clip) {
                throw ParseException("truncated EDGE v2 square/clip block", _path, _stream.position());
            }
            data.squareRect = *square;
            data.clipSides = unpackClipSides(*clip);
        }

        if (levelIndicator != elevation) {
            continue; // no zone data for this elevation
        }

        while (true) {
            auto zone = tryReadRect();
            if (!zone) {
                // End of stream while expecting a zone. The engine accepts this only on the
                // last elevation (a natural EOF) and treats it as corruption otherwise.
                if (elevation == MapEdge::ELEVATION_COUNT - 1) {
                    return edge;
                }
                throw ParseException("truncated EDGE file (zone rect)", _path, _stream.position());
            }
            data.zones.push_back(*zone);

            auto indicator = tryReadInt32();
            if (!indicator) {
                if (elevation == MapEdge::ELEVATION_COUNT - 1) {
                    return edge;
                }
                throw ParseException("truncated EDGE file (level indicator)", _path, _stream.position());
            }
            levelIndicator = *indicator;

            if (levelIndicator != elevation) {
                break; // the next zone belongs to a later elevation
            }
        }
    }

    return edge;
}

std::filesystem::path MapEdgeReader::siblingPath(const std::filesystem::path& mapPath) {
    std::filesystem::path edgePath = mapPath;
    edgePath.replace_extension(".EDG");
    return edgePath;
}

std::optional<MapEdge> MapEdgeReader::tryParse(const std::filesystem::path& path,
    const std::vector<uint8_t>& bytes) {
    try {
        MapEdgeReader reader;
        auto edge = reader.openFile(path, bytes);
        if (!edge || edge->empty()) {
            return std::nullopt;
        }
        return *edge;
    } catch (const std::exception& e) {
        spdlog::warn("Ignoring malformed map-edge file {}: {}", path.string(), e.what());
        return std::nullopt;
    }
}

} // namespace geck
