#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

/**
 * Seeded floor-tile synthesis over plain label grids (no Map/VFS dependency), shared by the
 * generation scripts (`MapScriptApi::quiltFloor*`) and `map analyze`'s directional adjacency
 * digest so the learner and the reporter can never disagree.
 *
 * The synthesizer reproduces a reference map's hand-authored tile blending by patch quilting:
 * it copies small blocks out of the reference grid, choosing at each step among the blocks that
 * best agree with the cells already fixed around them, then repairs any remaining seam with a
 * per-cell draw constrained to neighbour pairs observed in the reference. Naive per-cell
 * weighted-random sampling is exactly what this replaces — it cannot reproduce multi-tile
 * features, so it only ever appears here as the last rung of the repair ladder.
 */
namespace geck::floorsynth {

/// A row-major label grid of floor-tile ids; width * height == cells.size().
struct Grid {
    std::vector<uint16_t> cells;
    int width = 0;
    int height = 0;

    uint16_t at(int col, int row) const { return cells[static_cast<size_t>(row) * width + col]; }
};

struct Params {
    int blockSize = 5;    ///< quilt patch edge K, in cells (clamped to the grids involved)
    int overlap = 2;      ///< cells a patch shares with already-fixed neighbours (stride = K - overlap)
    uint16_t emptyId = 1; ///< Map::EMPTY_TILE — never learned across, never emitted
    uint32_t seed = 0;    ///< a run is a pure function of (reference, target, region, params)
};

/// Ordered neighbour statistics observed in a grid: (a, b) is counted when b sits immediately
/// EAST (col+1) or SOUTH (row+1) of a. Pairs touching `emptyId` are never counted, so map-edge
/// padding and holes contribute nothing (an un-authored straight map edge must not be learned
/// as a feature). Same-id pairs ARE counted — the repair ladder needs to know whether a tile
/// may sit next to itself; reporting consumers filter them out.
struct AdjacencyModel {
    std::map<std::pair<uint16_t, uint16_t>, int> east;
    std::map<std::pair<uint16_t, uint16_t>, int> south;
    std::map<uint16_t, int> marginal; ///< non-empty cell counts (the last-resort pick pool)

    bool observedEast(uint16_t a, uint16_t b) const { return east.contains({ a, b }); }
    bool observedSouth(uint16_t a, uint16_t b) const { return south.contains({ a, b }); }
};

AdjacencyModel learnAdjacency(const Grid& reference, uint16_t emptyId = 1);

/// How faithfully a run reproduced the reference statistics. Surfaced (not hidden) so callers
/// can report degraded output instead of silently shipping it.
struct Stats {
    int cellsPainted = 0;
    int blocksPlaced = 0;
    int perfectBlocks = 0;   ///< placements whose best candidate disagreed with no fixed cell
    int mismatchedCells = 0; ///< already-fixed cells the chosen blocks disagreed with
    int repairedCells = 0;   ///< cells (re-)sampled by the per-cell repair ladder
    int unresolvedSeams = 0; ///< ordered adjacent pairs still unobserved in the reference

    /// Repairs by ladder rung: all-constraints / west+north / west / north / marginal.
    std::array<int, 5> repairLevel{};
};

struct Result {
    /// (row-major cell index into the target grid, chosen tile id), ascending by index.
    std::vector<std::pair<int, uint16_t>> cells;
    Stats stats;
};

/// Assign every cell of `region` (row-major indices into `target`; duplicates and off-grid
/// indices are ignored) a tile id sampled from `reference`, so that seams — between
/// synthesized cells and against pre-existing non-empty `target` cells bordering the region —
/// reproduce neighbour pairs observed in the reference. Region cells' current content is
/// ignored; cells outside the region are never assigned. `target` is not mutated: the caller
/// paints `Result::cells`. Deterministic for fixed inputs and `Params::seed`.
Result synthesize(const Grid& reference, const Grid& target, const std::vector<int>& region,
    const Params& params);

} // namespace geck::floorsynth
