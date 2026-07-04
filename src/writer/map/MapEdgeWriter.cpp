#include "writer/map/MapEdgeWriter.h"

namespace geck {

namespace {

    // Packs clip-sides into the EDG v2 bitfield (inverse of the reader's unpackClipSides;
    // map_edge.cc:199 packClipSides).
    int32_t packClipSides(const MapEdge::ClipSides& clip) {
        int32_t value = 0;
        if (clip.bottom)
            value |= 1;
        if (clip.right)
            value |= 1 << 8;
        if (clip.top)
            value |= 1 << 16;
        if (clip.left)
            value |= 1 << 24;
        return value;
    }

    // Index of the next elevation (>= from) that has zones, or ELEVATION_COUNT if none.
    // This is the level indicator that advances the reader past empty elevations
    // (map_edge.cc:298 nextElevationWithZones).
    int nextElevationWithZones(const MapEdge& edge, int from) {
        for (int elevation = from; elevation < MapEdge::ELEVATION_COUNT; ++elevation) {
            if (!edge.elevations[elevation].zones.empty()) {
                return elevation;
            }
        }
        return MapEdge::ELEVATION_COUNT;
    }

    void writeRect(BinaryWriteUtils& utils, const MapEdge::Rect& rect) {
        utils.writeBE32Signed(rect.left);
        utils.writeBE32Signed(rect.top);
        utils.writeBE32Signed(rect.right);
        utils.writeBE32Signed(rect.bottom);
    }

} // namespace

bool MapEdgeWriter::write(const MapEdge& edge) {
    // Refuse a zero-zone edge: the engine writes no file in that case and a header-only file does
    // not parse back, so emitting one would produce a malformed sidecar. Callers skip empty edges
    // before opening the file (see saveMapEdgeBeside); this guards direct misuse too.
    if (edge.empty()) {
        return false;
    }

    BinaryWriteUtils& utils = getBinaryUtils();

    utils.writeBE32(MapEdge::MAGIC);
    utils.writeBE32(edge.isVersion2() ? 2u : 1u);
    utils.writeBE32(0u); // reserved

    const bool version2 = edge.isVersion2();

    for (int elevation = 0; elevation < MapEdge::ELEVATION_COUNT; ++elevation) {
        const MapEdge::Elevation& data = edge.elevations[elevation];

        if (version2) {
            writeRect(utils, data.squareRect);
            utils.writeBE32Signed(packClipSides(data.clipSides));
        }

        const int zoneCount = static_cast<int>(data.zones.size());
        for (int i = 0; i < zoneCount; ++i) {
            writeRect(utils, data.zones[i]);

            // The file's final zone omits its trailing level indicator when it lies on the last
            // elevation — the layout the original mapper and every shipped .edg uses (N zones,
            // N-1 separators), which the reader recovers from end-of-stream. A final zone on an
            // earlier elevation still needs the terminator so the reader knows the higher
            // elevations are empty rather than mis-reading the next bytes as another zone.
            const bool isFinalZoneOnLastElevation = elevation == MapEdge::ELEVATION_COUNT - 1
                && i == zoneCount - 1;
            if (isFinalZoneOnLastElevation) {
                continue;
            }

            const int levelIndicator = (i + 1 < zoneCount)
                ? elevation
                : nextElevationWithZones(edge, elevation + 1);
            utils.writeBE32Signed(levelIndicator);
        }
    }

    flush();
    return true;
}

} // namespace geck
