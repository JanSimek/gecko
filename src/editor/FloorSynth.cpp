#include "FloorSynth.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <random>

namespace geck::floorsynth {

namespace {

    constexpr int32_t UNASSIGNED = -1; // region cell not yet synthesized
    constexpr int32_t ABSENT = -2;     // cell with no content — constrains nothing

    /// Block start positions covering [lo, hi] (inclusive) with K-wide blocks stepping `stride`,
    /// each start clamped to [0, limit - K]; the final block's far edge lands exactly on the
    /// covered extent (the standard quilting edge clamp — edge blocks may overlap more than the
    /// nominal overlap, which only adds constraints).
    std::vector<int> blockStarts(int lo, int hi, int K, int stride, int limit) {
        const int last = std::clamp(hi - K + 1, 0, limit - K);
        const int first = std::clamp(std::min(lo, last), 0, limit - K);
        std::vector<int> starts;
        for (int s = first; s < last; s += stride)
            starts.push_back(s);
        starts.push_back(last);
        return starts;
    }

    bool windowIsClear(const Grid& reference, int sx, int sy, int K, uint16_t emptyId) {
        for (int dy = 0; dy < K; ++dy)
            for (int dx = 0; dx < K; ++dx)
                if (reference.at(sx + dx, sy + dy) == emptyId)
                    return false;
        return true;
    }

    /// Top-left corners of every K x K reference window containing no empty cell, in scan order
    /// (scan order + a seeded pick among ties is what keeps a run reproducible).
    std::vector<std::pair<int, int>> validWindows(const Grid& reference, int K, uint16_t emptyId) {
        std::vector<std::pair<int, int>> windows;
        for (int sy = 0; sy + K <= reference.height; ++sy)
            for (int sx = 0; sx + K <= reference.width; ++sx)
                if (windowIsClear(reference, sx, sy, K, emptyId))
                    windows.emplace_back(sx, sy);
        return windows;
    }

    std::vector<uint16_t> intersect(const std::vector<uint16_t>& a, const std::vector<uint16_t>& b) {
        std::vector<uint16_t> out;
        std::ranges::set_intersection(a, b, std::back_inserter(out));
        return out;
    }

    /// {t : (a, t) observed} — the ids that may sit after `a` in one direction. Sorted (map order).
    std::vector<uint16_t> continuations(const std::map<std::pair<uint16_t, uint16_t>, int>& pairs, uint16_t a) {
        std::vector<uint16_t> out;
        for (auto it = pairs.lower_bound({ a, 0 }); it != pairs.end() && it->first.first == a; ++it)
            out.push_back(it->first.second);
        return out;
    }

    /// The reverse index of a directional pair map: b -> every id observed before b, built once
    /// per run so repair lookups don't rescan the whole pair map per cell. Each per-b list is
    /// sorted: map order is (first, second) ascending, so firsts are collected ascending.
    std::map<uint16_t, std::vector<uint16_t>> predecessorIndex(const std::map<std::pair<uint16_t, uint16_t>, int>& pairs) {
        std::map<uint16_t, std::vector<uint16_t>> index;
        for (const auto& [pair, count] : pairs)
            index[pair.second].push_back(pair.first);
        return index;
    }

    /// One synthesis run's working state, split into phase methods so each stays readable:
    /// seed -> place blocks -> repair -> count unresolved seams (see synthesize()).
    struct Synthesis {
        const Grid& reference;
        const Grid& target;
        const Params& params;
        std::vector<int> cellsToFill; ///< sanitized region: ascending, unique, on-grid
        AdjacencyModel model;
        std::map<uint16_t, std::vector<uint16_t>> eastBefore;  ///< predecessorIndex(model.east)
        std::map<uint16_t, std::vector<uint16_t>> southBefore; ///< predecessorIndex(model.south)
        std::vector<int32_t> state;                            ///< per target cell: an id, UNASSIGNED, or ABSENT
        std::vector<uint8_t> inRegion;
        // Region bounding box, in tile columns/rows.
        int bboxLeft;
        int bboxTop;
        int bboxRight;
        int bboxBottom;
        std::mt19937 rng; // NOSONAR: seeded for reproducible terrain, not a security-sensitive use
        Stats stats;

        Synthesis(const Grid& ref, const Grid& tgt, std::vector<int> region, const Params& p)
            : reference(ref)
            , target(tgt)
            , params(p)
            , cellsToFill(std::move(region))
            , model(learnAdjacency(ref, p.emptyId))
            , eastBefore(predecessorIndex(model.east))
            , southBefore(predecessorIndex(model.south))
            , state(static_cast<size_t>(tgt.width) * tgt.height, ABSENT)
            , inRegion(static_cast<size_t>(tgt.width) * tgt.height, 0)
            , bboxLeft(tgt.width)
            , bboxTop(tgt.height)
            , bboxRight(-1)
            , bboxBottom(-1)
            , rng(p.seed) { // NOSONAR: seeded for reproducible terrain, not a security-sensitive use
            seedState();
        }

        /// Pre-existing non-empty cells outside the region are boundary constraints; region
        /// cells' current content is deliberately ignored (they are re-synthesized).
        void seedState() {
            const int cells = target.width * target.height;
            for (int i = 0; i < cells; ++i)
                if (target.cells[static_cast<size_t>(i)] != params.emptyId)
                    state[static_cast<size_t>(i)] = target.cells[static_cast<size_t>(i)];
            for (int index : cellsToFill) {
                state[static_cast<size_t>(index)] = UNASSIGNED;
                inRegion[static_cast<size_t>(index)] = 1;
                const int col = index % target.width;
                const int row = index / target.width;
                bboxLeft = std::min(bboxLeft, col);
                bboxRight = std::max(bboxRight, col);
                bboxTop = std::min(bboxTop, row);
                bboxBottom = std::max(bboxBottom, row);
            }
        }

        int32_t presentValue(int col, int row) const {
            if (col < 0 || row < 0 || col >= target.width || row >= target.height)
                return ABSENT;
            return state[static_cast<size_t>(row * target.width + col)];
        }

        /// A window's disagreement with the fixed cells under a block footprint. `cap` keeps
        /// the tie set exact: a window may stop counting only once it strictly exceeds the
        /// current best, so every true minimum finishes its scan.
        int scoreWindow(int bx, int by, int sx, int sy, int K, int cap) const {
            int penalty = 0;
            for (int dy = 0; dy < K; ++dy) {
                const int rowBase = (by + dy) * target.width + bx;
                for (int dx = 0; dx < K; ++dx) {
                    const int32_t fixed = state[static_cast<size_t>(rowBase + dx)];
                    if (fixed >= 0 && reference.at(sx + dx, sy + dy) != fixed && ++penalty > cap)
                        return penalty;
                }
            }
            return penalty;
        }

        /// Copy one block: choose uniformly among the windows that disagree least with the
        /// cells already fixed under the footprint, then fill only the still-unassigned
        /// region cells (write-once — everything else is a constraint).
        void placeBlock(const std::vector<std::pair<int, int>>& windows, int bx, int by, int K) {
            // Seed the tie set with window 0 so it is non-empty by construction.
            int best = scoreWindow(bx, by, windows[0].first, windows[0].second, K,
                std::numeric_limits<int>::max());
            std::vector<size_t> bestWindows{ 0 };
            for (size_t w = 1; w < windows.size(); ++w) {
                const int penalty = scoreWindow(bx, by, windows[w].first, windows[w].second, K, best);
                if (penalty < best) {
                    best = penalty;
                    bestWindows.assign(1, w);
                } else if (penalty == best) {
                    bestWindows.push_back(w);
                }
            }

            const auto& [sx, sy] = windows[bestWindows[rng() % bestWindows.size()]];
            ++stats.blocksPlaced;
            if (best == 0)
                ++stats.perfectBlocks;
            stats.mismatchedCells += best;
            for (int dy = 0; dy < K; ++dy)
                for (int dx = 0; dx < K; ++dx) {
                    const auto index = static_cast<size_t>((by + dy) * target.width + bx + dx);
                    if (state[index] == UNASSIGNED)
                        state[index] = reference.at(sx + dx, sy + dy);
                }
        }

        /// The quilting pass: raster K x K blocks over the region. The schedule extends one
        /// overlap-width past the region's bounding box so pre-existing tiles just outside it
        /// land under block footprints and constrain the picks — otherwise a block starting
        /// exactly on the region edge would ignore the boundary it must blend into.
        void placeBlocks() {
            int K = std::min({ params.blockSize, reference.width, reference.height, target.width, target.height });
            std::vector<std::pair<int, int>> windows;
            for (; K >= 2; --K) {
                windows = validWindows(reference, K, params.emptyId);
                if (!windows.empty())
                    break;
            }
            if (windows.empty())
                return; // no usable reference windows: the repair ladder covers every cell

            const int overlap = std::clamp(params.overlap, 0, K - 1);
            const int stride = K - overlap;
            for (int by : blockStarts(bboxTop - overlap, bboxBottom + overlap, K, stride, target.height)) {
                for (int bx : blockStarts(bboxLeft - overlap, bboxRight + overlap, K, stride, target.width)) {
                    if (blockHasWork(bx, by, K))
                        placeBlock(windows, bx, by, K);
                }
            }
        }

        bool blockHasWork(int bx, int by, int K) const {
            for (int dy = 0; dy < K; ++dy)
                for (int dx = 0; dx < K; ++dx)
                    if (state[static_cast<size_t>((by + dy) * target.width + bx + dx)] == UNASSIGNED)
                        return true;
            return false;
        }

        bool needsRepair(int index, int32_t west, int32_t north, int32_t east, int32_t south) const {
            const int32_t current = state[static_cast<size_t>(index)];
            if (current == UNASSIGNED)
                return true;
            const auto id = static_cast<uint16_t>(current);
            return (west >= 0 && !model.observedEast(static_cast<uint16_t>(west), id))
                || (north >= 0 && !model.observedSouth(static_cast<uint16_t>(north), id))
                || (east >= 0 && !model.observedEast(id, static_cast<uint16_t>(east)))
                || (south >= 0 && !model.observedSouth(id, static_cast<uint16_t>(south)));
        }

        /// The four directional candidate sets at a cell; a set exists only when its
        /// neighbour is present (has* false means "no constraint", not "empty set").
        struct ConstraintSets {
            std::vector<uint16_t> fromWest, fromNorth, toEast, toSouth;
            bool hasWest = false, hasNorth = false, hasEast = false, hasSouth = false;

            std::vector<const std::vector<uint16_t>*> available() const {
                std::vector<const std::vector<uint16_t>*> out;
                if (hasWest)
                    out.push_back(&fromWest);
                if (hasNorth)
                    out.push_back(&fromNorth);
                if (hasEast)
                    out.push_back(&toEast);
                if (hasSouth)
                    out.push_back(&toSouth);
                return out;
            }
        };

        std::vector<uint16_t> predecessorsOf(const std::map<uint16_t, std::vector<uint16_t>>& index, int32_t b) const {
            const auto it = index.find(static_cast<uint16_t>(b));
            return it != index.end() ? it->second : std::vector<uint16_t>{};
        }

        ConstraintSets constraintSets(int32_t west, int32_t north, int32_t east, int32_t south) const {
            ConstraintSets sets;
            if (west >= 0) {
                sets.hasWest = true;
                sets.fromWest = continuations(model.east, static_cast<uint16_t>(west));
            }
            if (north >= 0) {
                sets.hasNorth = true;
                sets.fromNorth = continuations(model.south, static_cast<uint16_t>(north));
            }
            if (east >= 0) {
                sets.hasEast = true;
                sets.toEast = predecessorsOf(eastBefore, east);
            }
            if (south >= 0) {
                sets.hasSouth = true;
                sets.toSouth = predecessorsOf(southBefore, south);
            }
            return sets;
        }

        /// Intersection of every available constraint set (the ladder's top rung); empty when
        /// no id satisfies them all — or when there are no constraints at all.
        static std::vector<uint16_t> intersectAll(const std::vector<const std::vector<uint16_t>*>& available) {
            if (available.empty())
                return {};
            std::vector<uint16_t> all = *available.front();
            for (size_t i = 1; i < available.size() && !all.empty(); ++i)
                all = intersect(all, *available[i]);
            return all;
        }

        std::vector<uint16_t> marginalCandidates() const {
            std::vector<uint16_t> everything;
            everything.reserve(model.marginal.size());
            for (const auto& [id, count] : model.marginal)
                everything.push_back(id);
            return everything;
        }

        /// The fallback ladder: candidate ids satisfying the cell's constraints, relaxing
        /// all-available -> west+north -> west -> north -> marginal until non-empty. Writes
        /// the rung taken to `level` (the Stats histogram index).
        std::vector<uint16_t> ladderCandidates(const ConstraintSets& c, int& level) const {
            if (auto all = intersectAll(c.available()); !all.empty()) {
                level = 0;
                return all;
            }
            if (c.hasWest && c.hasNorth) {
                auto westAndNorth = intersect(c.fromWest, c.fromNorth);
                if (!westAndNorth.empty()) {
                    level = 1;
                    return westAndNorth;
                }
            }
            if (c.hasWest && !c.fromWest.empty()) {
                level = 2;
                return c.fromWest;
            }
            if (c.hasNorth && !c.fromNorth.empty()) {
                level = 3;
                return c.fromNorth;
            }
            level = 4;
            return marginalCandidates();
        }

        /// Weighted draw over `candidates` by reference frequency. Candidates always come from
        /// the model's own keys, so every id has a marginal count >= 1. No std::*_distribution
        /// — their output is implementation-defined, and a seed must reproduce cross-platform.
        uint16_t weightedPick(const std::vector<uint16_t>& candidates) {
            uint64_t total = 0;
            for (uint16_t id : candidates)
                total += static_cast<uint64_t>(model.marginal.at(id));
            if (total == 0)
                return candidates.back(); // unreachable (weights >= 1); guards the modulo
            uint64_t r = rng() % total;
            for (uint16_t id : candidates) {
                const auto weight = static_cast<uint64_t>(model.marginal.at(id));
                if (r < weight)
                    return id;
                r -= weight;
            }
            return candidates.back();
        }

        /// Any cell the blocks left with an unobserved seam (or, with no usable windows, never
        /// assigned) is re-drawn once, in ascending order, from the ids the reference allows
        /// next to its current neighbours. One pass, no iteration: repairs must terminate,
        /// not oscillate.
        void repairPass() {
            for (int index : cellsToFill) {
                const int col = index % target.width;
                const int row = index / target.width;
                const int32_t west = presentValue(col - 1, row);
                const int32_t north = presentValue(col, row - 1);
                const int32_t east = presentValue(col + 1, row);
                const int32_t south = presentValue(col, row + 1);
                if (!needsRepair(index, west, north, east, south))
                    continue;
                int level = 4;
                const auto candidates = ladderCandidates(constraintSets(west, north, east, south), level);
                state[static_cast<size_t>(index)] = weightedPick(candidates);
                ++stats.repairedCells;
                ++stats.repairLevel[static_cast<size_t>(level)];
            }
        }

        /// Honest accounting: seams the reference never showed us. Each pair involving at
        /// least one region cell counts exactly once — west/north only against non-region
        /// cells (a region west neighbour already counts the pair as its own east seam).
        void countUnresolvedSeams() {
            for (int index : cellsToFill) {
                const int col = index % target.width;
                const int row = index / target.width;
                const auto id = static_cast<uint16_t>(state[static_cast<size_t>(index)]);
                const int32_t west = presentValue(col - 1, row);
                const int32_t north = presentValue(col, row - 1);
                const int32_t east = presentValue(col + 1, row);
                const int32_t south = presentValue(col, row + 1);
                if (west >= 0 && !inRegion[static_cast<size_t>(index - 1)] && !model.observedEast(static_cast<uint16_t>(west), id))
                    ++stats.unresolvedSeams;
                if (north >= 0 && !inRegion[static_cast<size_t>(index - target.width)] && !model.observedSouth(static_cast<uint16_t>(north), id))
                    ++stats.unresolvedSeams;
                if (east >= 0 && !model.observedEast(id, static_cast<uint16_t>(east)))
                    ++stats.unresolvedSeams;
                if (south >= 0 && !model.observedSouth(id, static_cast<uint16_t>(south)))
                    ++stats.unresolvedSeams;
            }
        }
    };

    /// Canonical region order (ascending, unique, on-grid) is contractual: it makes the run
    /// independent of the caller's ordering, so seeded draws reproduce.
    std::vector<int> sanitizeRegion(const std::vector<int>& region, int targetCells) {
        std::vector<int> cells = region;
        std::ranges::sort(cells);
        const auto duplicates = std::ranges::unique(cells);
        cells.erase(duplicates.begin(), duplicates.end());
        std::erase_if(cells, [&](int index) { return index < 0 || index >= targetCells; });
        return cells;
    }

} // namespace

AdjacencyModel learnAdjacency(const Grid& reference, uint16_t emptyId) {
    AdjacencyModel model;
    for (int row = 0; row < reference.height; ++row) {
        for (int col = 0; col < reference.width; ++col) {
            const uint16_t a = reference.at(col, row);
            if (a == emptyId)
                continue;
            ++model.marginal[a];
            if (col + 1 < reference.width) {
                const uint16_t b = reference.at(col + 1, row);
                if (b != emptyId)
                    ++model.east[{ a, b }];
            }
            if (row + 1 < reference.height) {
                const uint16_t b = reference.at(col, row + 1);
                if (b != emptyId)
                    ++model.south[{ a, b }];
            }
        }
    }
    return model;
}

Result synthesize(const Grid& reference, const Grid& target, const std::vector<int>& region,
    const Params& params) {
    Result result;
    if (reference.width <= 0 || reference.height <= 0 || target.width <= 0 || target.height <= 0)
        return result;

    std::vector<int> cellsToFill = sanitizeRegion(region, target.width * target.height);
    if (cellsToFill.empty())
        return result;

    Synthesis run(reference, target, std::move(cellsToFill), params);
    if (run.model.marginal.empty())
        return result; // an all-empty reference teaches nothing — assign nothing, visibly

    run.placeBlocks();
    run.repairPass();
    run.countUnresolvedSeams();

    result.cells.reserve(run.cellsToFill.size());
    for (int index : run.cellsToFill)
        result.cells.emplace_back(index, static_cast<uint16_t>(run.state[static_cast<size_t>(index)]));
    run.stats.cellsPainted = static_cast<int>(run.cellsToFill.size());
    result.stats = run.stats;
    return result;
}

} // namespace geck::floorsynth
