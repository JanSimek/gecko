#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace geck::pattern {

/// A single object captured in a pattern variant, positioned by its hex offset
/// relative to the variant's anchor. PID and direction values are stored verbatim
/// (engine data fidelity — the format carries no display labels).
struct PatternObject {
    int dxHex = 0; ///< Hex-space column offset from the anchor.
    int dyHex = 0; ///< Hex-space row offset from the anchor.
    uint32_t proPid = 0;
    uint32_t frmPid = 0;
    uint32_t direction = 0; ///< Stored verbatim; matches the authored sprite.
    uint32_t flags = 0;
};

/// A floor or roof tile captured in a pattern variant, positioned by its tile offset
/// relative to the anchor. Tile space is a distinct 100x100 grid from hex space —
/// never validate one against the other's range (see CLAUDE.md).
struct PatternTile {
    int dxTile = 0;
    int dyTile = 0;
    uint16_t tileId = 0;
};

/// One authored orientation of a pattern (e.g. "entrance west"). Fallout 2 object art
/// is direction-specific — a wall facing NE is a different PRO/FRM than one facing NW —
/// so orientations are pre-authored variants rather than computed by rotation. Stamping
/// places a variant verbatim, translated from its anchor to the target hex.
struct PatternVariant {
    std::string label;
    int anchorHex = 0; ///< Authoring origin; object/tile offsets are relative to this.
    std::vector<PatternObject> objects;
    std::vector<PatternTile> floor;
    std::vector<PatternTile> roof;
};

/// A reusable prefab captured from a selection, holding one or more orientation
/// variants the editor cycles through at stamp time. Serialized to JSON by
/// PatternSerializer.
struct Pattern {
    static constexpr int CURRENT_VERSION = 1;

    std::string name;
    int version = CURRENT_VERSION;
    std::vector<PatternVariant> variants; ///< At least one.
};

} // namespace geck::pattern
