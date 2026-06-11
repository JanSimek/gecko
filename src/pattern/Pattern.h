#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace geck::pattern {

/// A single object captured in a pattern, positioned by its hex offset relative to
/// the pattern's anchor. PID and direction values are stored verbatim (engine data
/// fidelity — the format carries no display labels).
struct PatternObject {
    int dxHex = 0; ///< Hex-space column offset from the anchor.
    int dyHex = 0; ///< Hex-space row offset from the anchor.
    uint32_t proPid = 0;
    uint32_t frmPid = 0;
    uint32_t direction = 0; ///< 0-5 facing; rotated by step at stamp time.
    uint32_t flags = 0;
};

/// A floor or roof tile captured in a pattern, positioned by its tile offset
/// relative to the anchor. Tile space is a distinct 100x100 grid from hex space —
/// never validate one against the other's range (see CLAUDE.md).
struct PatternTile {
    int dxTile = 0;
    int dyTile = 0;
    uint16_t tileId = 0;
};

/// A reusable prefab captured from a selection: relative object and tile placements
/// anchored at an origin hex, replayable elsewhere (optionally rotated) by the
/// stamper. Serialized to JSON by PatternSerializer.
struct Pattern {
    static constexpr int CURRENT_VERSION = 1;

    std::string name;
    int version = CURRENT_VERSION;
    int anchorHex = 0; ///< Authoring origin; object/tile offsets are relative to this.
    int sizeHexW = 0;  ///< Bounding-box width in hexes (informational).
    int sizeHexH = 0;  ///< Bounding-box height in hexes (informational).
    bool rotatable = false;

    std::vector<PatternObject> objects;
    std::vector<PatternTile> floor;
    std::vector<PatternTile> roof;
};

} // namespace geck::pattern
