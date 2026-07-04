#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace geck {

/// In-memory model of a Fallout 2 CE / sfall ".EDG" map-edge sidecar — the big-endian
/// 'EDGE' v1/v2 file the CE engine authors and enforces beside a `.map`
/// (fallout2-ce `map_edge.cc`). It defines, per elevation, scroll-boundary "zones"
/// (tile-space rects) plus, in v2, a square-grid clip rect and per-side clip flags.
///
/// Lossless & round-trippable: parsing an engine-produced file and writing it back
/// reproduces the original bytes exactly (see MapEdgeReader / MapEdgeWriter, which mirror
/// the engine's `mapEdgeLoadFromStream` / `writeEdgStream` byte-for-byte). The engine's
/// runtime-only pixel-space fields (`pixelRect` / `scrollBorderRect`) are screen-size
/// derived and are **not** part of the on-disk format, so this model omits them.
struct MapEdge {
    /// Fallout 2 always has three elevation slots (engine `ELEVATION_COUNT`).
    static constexpr int ELEVATION_COUNT = 3;

    /// File magic: the ASCII bytes "EDGE" read as a big-endian int32 ('EDGE' in the engine).
    static constexpr uint32_t MAGIC = 0x45444745u;

    /// Square (floor/roof) tile grid dimensions — engine `SQUARE_GRID_WIDTH/HEIGHT`.
    /// Used only for the default `squareRect` (matching map_edge.cc:229).
    static constexpr int SQUARE_GRID_WIDTH = 100;
    static constexpr int SQUARE_GRID_HEIGHT = 100;

    /// Per-side clip flags, packed into a single int32 in v2 EDG files:
    /// bottom=bit0, right=bit8, top=bit16, left=bit24 (map_edge.cc pack/unpackClipSides).
    struct ClipSides {
        bool bottom = false;
        bool right = false;
        bool top = false;
        bool left = false;

        bool operator==(const ClipSides&) const = default;
    };

    /// A rect stored in on-disk RECT field order `{left, top, right, bottom}`.
    /// The four ints are preserved verbatim; the engine deliberately stores some rects
    /// with X inverted (left > right), so this type makes no geometric assumptions.
    struct Rect {
        int32_t left = 0;
        int32_t top = 0;
        int32_t right = 0;
        int32_t bottom = 0;

        bool operator==(const Rect&) const = default;
    };

    /// One elevation's edge data. `squareRect` / `clipSides` are meaningful only for v2
    /// files; their defaults mirror the engine's (`map_edge.cc:229`).
    struct Elevation {
        std::vector<Rect> zones; ///< each zone's `tileRect` (tile-space corners)
        Rect squareRect{ SQUARE_GRID_WIDTH - 1, 0, 0, SQUARE_GRID_HEIGHT - 1 };
        ClipSides clipSides;

        bool operator==(const Elevation&) const = default;
    };

    int version = 1; ///< 1 or 2 (v2 adds the per-elevation square/clip block)
    std::array<Elevation, ELEVATION_COUNT> elevations;

    bool isVersion2() const { return version == 2; }

    /// Total zones across all elevations.
    int totalZones() const {
        int total = 0;
        for (const auto& elevation : elevations) {
            total += static_cast<int>(elevation.zones.size());
        }
        return total;
    }

    /// True when there are no zones at all. The engine writes no file in this case
    /// (`mapEdgeSave` returns early) and a zero-zone file does not parse, so callers
    /// must skip writing an empty edge rather than emit a header-only file.
    bool empty() const { return totalZones() == 0; }

    bool operator==(const MapEdge&) const = default;
};

} // namespace geck
