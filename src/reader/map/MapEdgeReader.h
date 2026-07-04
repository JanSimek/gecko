#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include "format/map/MapEdge.h"
#include "reader/FileParser.h"

namespace geck {

/// Parses a Fallout 2 CE / sfall ".EDG" map-edge sidecar into a MapEdge.
///
/// Mirrors fallout2-ce `mapEdgeLoadFromStream` (`map_edge.cc`) byte-for-byte: a big-endian
/// `EDGE` header (magic / version 1|2 / reserved 0) followed, per elevation, by an optional
/// v2 square/clip block and a list of zone rects delimited by a trailing "level indicator"
/// that names the elevation the next zone belongs to. The engine's runtime pixel-space
/// fields are not stored, so nothing here recomputes them.
class MapEdgeReader : public FileParser<MapEdge> {
public:
    MapEdgeReader() = default;

    std::unique_ptr<MapEdge> read() override;

    /// The conventional sibling ".EDG" path for a map file (ARROYO.MAP -> ARROYO.EDG),
    /// mirroring the engine's `buildEdgeFileName` (basename + ".EDG").
    static std::filesystem::path siblingPath(const std::filesystem::path& mapPath);

    /// Parses EDG bytes, returning nullopt instead of throwing on any error and dropping an
    /// edge that carries no zones. Mirrors the engine's `mapEdgeLoad`, which silently ignores a
    /// missing or malformed .EDG. `path` is used only for diagnostics.
    static std::optional<MapEdge> tryParse(const std::filesystem::path& path,
        const std::vector<uint8_t>& bytes);

private:
    /// Reads a full `{left, top, right, bottom}` rect, or nullopt at end of stream.
    std::optional<MapEdge::Rect> tryReadRect();

    /// Reads one big-endian int32, or nullopt at end of stream.
    std::optional<int32_t> tryReadInt32();
};

} // namespace geck
