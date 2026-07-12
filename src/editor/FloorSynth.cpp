#include "FloorSynth.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <random>

namespace geck {
namespace floorsynth {

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

        /// Top-left corners of every K x K reference window containing no empty cell, in scan order
        /// (scan order + a seeded pick among ties is what keeps a run reproducible).
        std::vector<std::pair<int, int>> validWindows(const Grid& reference, int K, uint16_t emptyId) {
            std::vector<std::pair<int, int>> windows;
            for (int sy = 0; sy + K <= reference.height; ++sy) {
                for (int sx = 0; sx + K <= reference.width; ++sx) {
                    bool ok = true;
                    for (int dy = 0; ok && dy < K; ++dy)
                        for (int dx = 0; ok && dx < K; ++dx)
                            if (reference.at(sx + dx, sy + dy) == emptyId)
                                ok = false;
                    if (ok)
                        windows.emplace_back(sx, sy);
                }
            }
            return windows;
        }

        /// Weighted draw over `candidates` by reference frequency. Candidates always come from the
        /// model's own keys, so every id has a marginal count >= 1. No std::*_distribution — their
        /// output is implementation-defined, and the same seed must reproduce cross-platform.
        uint16_t weightedPick(const std::vector<uint16_t>& candidates, const AdjacencyModel& model,
            std::mt19937& rng) {
            uint64_t total = 0;
            for (uint16_t id : candidates)
                total += static_cast<uint64_t>(model.marginal.at(id));
            uint64_t r = rng() % total;
            for (uint16_t id : candidates) {
                const auto weight = static_cast<uint64_t>(model.marginal.at(id));
                if (r < weight)
                    return id;
                r -= weight;
            }
            return candidates.back();
        }

        std::vector<uint16_t> intersect(std::vector<uint16_t> a, const std::vector<uint16_t>& b) {
            std::vector<uint16_t> out;
            std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(out));
            return out;
        }

        /// {t : (a, t) observed} — the ids that may sit after `a` in one direction. Sorted (map order).
        std::vector<uint16_t> continuations(const std::map<std::pair<uint16_t, uint16_t>, int>& pairs, uint16_t a) {
            std::vector<uint16_t> out;
            for (auto it = pairs.lower_bound({ a, 0 }); it != pairs.end() && it->first.first == a; ++it)
                out.push_back(it->first.second);
            return out;
        }

        /// {t : (t, b) observed} — the ids that may sit before `b` in one direction. Sorted: map order
        /// is (first, second) ascending and each (t, b) is unique, so collected firsts ascend.
        std::vector<uint16_t> predecessors(const std::map<std::pair<uint16_t, uint16_t>, int>& pairs, uint16_t b) {
            std::vector<uint16_t> out;
            for (const auto& entry : pairs)
                if (entry.first.second == b)
                    out.push_back(entry.first.first);
            return out;
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

        const int targetCells = target.width * target.height;

        // Canonical region order (ascending, unique, on-grid) is contractual: it makes the run
        // independent of the caller's ordering, so seeded draws reproduce.
        std::vector<int> cellsToFill = region;
        std::sort(cellsToFill.begin(), cellsToFill.end());
        cellsToFill.erase(std::unique(cellsToFill.begin(), cellsToFill.end()), cellsToFill.end());
        cellsToFill.erase(
            std::remove_if(cellsToFill.begin(), cellsToFill.end(),
                [&](int index) { return index < 0 || index >= targetCells; }),
            cellsToFill.end());
        if (cellsToFill.empty())
            return result;

        const AdjacencyModel model = learnAdjacency(reference, params.emptyId);
        if (model.marginal.empty())
            return result; // an all-empty reference teaches nothing — assign nothing, visibly

        // Pre-existing non-empty cells outside the region are boundary constraints; region cells'
        // current content is deliberately ignored (they are re-synthesized).
        std::vector<int32_t> state(static_cast<size_t>(targetCells), ABSENT);
        std::vector<uint8_t> inRegion(static_cast<size_t>(targetCells), 0);
        for (int i = 0; i < targetCells; ++i)
            if (target.cells[static_cast<size_t>(i)] != params.emptyId)
                state[static_cast<size_t>(i)] = target.cells[static_cast<size_t>(i)];
        int bboxLeft = target.width, bboxTop = target.height, bboxRight = -1, bboxBottom = -1;
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

        std::mt19937 rng(params.seed);

        // ---- Block phase: overlap-matched patch quilting -------------------------------------------
        // Copy K x K reference blocks in raster order, choosing uniformly among the blocks that
        // disagree least with the cells already fixed under their footprint. Write-once: a block only
        // fills still-unassigned region cells; everything else acts as a constraint.
        int K = std::min({ params.blockSize, reference.width, reference.height, target.width, target.height });
        std::vector<std::pair<int, int>> windows;
        for (; K >= 2; --K) {
            windows = validWindows(reference, K, params.emptyId);
            if (!windows.empty())
                break;
        }

        if (!windows.empty()) {
            const int overlap = std::clamp(params.overlap, 0, K - 1);
            const int stride = K - overlap;
            // The schedule extends one overlap-width past the region's bounding box so pre-existing
            // tiles just outside it land under block footprints and constrain the picks — otherwise a
            // block starting exactly on the region edge would ignore the boundary it must blend into.
            for (int by : blockStarts(bboxTop - overlap, bboxBottom + overlap, K, stride, target.height)) {
                for (int bx : blockStarts(bboxLeft - overlap, bboxRight + overlap, K, stride, target.width)) {
                    bool hasWork = false;
                    for (int dy = 0; !hasWork && dy < K; ++dy)
                        for (int dx = 0; !hasWork && dx < K; ++dx)
                            if (state[static_cast<size_t>((by + dy) * target.width + bx + dx)] == UNASSIGNED)
                                hasWork = true;
                    if (!hasWork)
                        continue;

                    // `cap` keeps the tie set exact: a window may stop counting only once it strictly
                    // exceeds the current best, so every true minimum finishes its scan.
                    auto scoreWindow = [&](int sx, int sy, int cap) {
                        int penalty = 0;
                        for (int dy = 0; dy < K; ++dy) {
                            const int rowBase = (by + dy) * target.width + bx;
                            for (int dx = 0; dx < K; ++dx) {
                                const int32_t fixed = state[static_cast<size_t>(rowBase + dx)];
                                if (fixed >= 0 && reference.at(sx + dx, sy + dy) != fixed) {
                                    if (++penalty > cap)
                                        return penalty;
                                }
                            }
                        }
                        return penalty;
                    };

                    int best = std::numeric_limits<int>::max();
                    std::vector<size_t> bestWindows;
                    for (size_t w = 0; w < windows.size(); ++w) {
                        const int penalty = scoreWindow(windows[w].first, windows[w].second, best);
                        if (penalty < best) {
                            best = penalty;
                            bestWindows.assign(1, w);
                        } else if (penalty == best) {
                            bestWindows.push_back(w);
                        }
                    }

                    const auto& [sx, sy] = windows[bestWindows[rng() % bestWindows.size()]];
                    ++result.stats.blocksPlaced;
                    if (best == 0)
                        ++result.stats.perfectBlocks;
                    result.stats.mismatchedCells += best;
                    for (int dy = 0; dy < K; ++dy)
                        for (int dx = 0; dx < K; ++dx) {
                            const auto index = static_cast<size_t>((by + dy) * target.width + bx + dx);
                            if (state[index] == UNASSIGNED)
                                state[index] = reference.at(sx + dx, sy + dy);
                        }
                }
            }
        }

        // ---- Repair phase: per-cell directional fallback ladder -------------------------------------
        // Any cell the blocks left with an unobserved seam (or, with no usable windows, never assigned)
        // is re-drawn once, in ascending order, from the ids the reference allows next to its current
        // neighbours — relaxing constraints (all -> west+north -> west -> north -> marginal) until a
        // candidate exists. One pass, no iteration: repairs must terminate, not oscillate.
        auto presentValue = [&](int col, int row) -> int32_t {
            if (col < 0 || row < 0 || col >= target.width || row >= target.height)
                return ABSENT;
            return state[static_cast<size_t>(row * target.width + col)];
        };

        for (int index : cellsToFill) {
            const int col = index % target.width;
            const int row = index / target.width;
            const int32_t west = presentValue(col - 1, row);
            const int32_t north = presentValue(col, row - 1);
            const int32_t east = presentValue(col + 1, row);
            const int32_t south = presentValue(col, row + 1);

            const int32_t current = state[static_cast<size_t>(index)];
            bool needsRepair = current == UNASSIGNED;
            if (!needsRepair) {
                const auto id = static_cast<uint16_t>(current);
                needsRepair = (west >= 0 && !model.observedEast(static_cast<uint16_t>(west), id))
                    || (north >= 0 && !model.observedSouth(static_cast<uint16_t>(north), id))
                    || (east >= 0 && !model.observedEast(id, static_cast<uint16_t>(east)))
                    || (south >= 0 && !model.observedSouth(id, static_cast<uint16_t>(south)));
            }
            if (!needsRepair)
                continue;

            const std::vector<uint16_t> fromWest = west >= 0 ? continuations(model.east, static_cast<uint16_t>(west)) : std::vector<uint16_t>{};
            const std::vector<uint16_t> fromNorth = north >= 0 ? continuations(model.south, static_cast<uint16_t>(north)) : std::vector<uint16_t>{};
            const std::vector<uint16_t> toEast = east >= 0 ? predecessors(model.east, static_cast<uint16_t>(east)) : std::vector<uint16_t>{};
            const std::vector<uint16_t> toSouth = south >= 0 ? predecessors(model.south, static_cast<uint16_t>(south)) : std::vector<uint16_t>{};

            std::vector<const std::vector<uint16_t>*> available;
            if (west >= 0)
                available.push_back(&fromWest);
            if (north >= 0)
                available.push_back(&fromNorth);
            if (east >= 0)
                available.push_back(&toEast);
            if (south >= 0)
                available.push_back(&toSouth);

            std::vector<uint16_t> candidates;
            int level = 4;
            if (!available.empty()) {
                std::vector<uint16_t> all = *available.front();
                for (size_t i = 1; i < available.size() && !all.empty(); ++i)
                    all = intersect(std::move(all), *available[i]);
                if (!all.empty()) {
                    candidates = std::move(all);
                    level = 0;
                }
            }
            if (candidates.empty() && west >= 0 && north >= 0) {
                auto westAndNorth = intersect(fromWest, fromNorth);
                if (!westAndNorth.empty()) {
                    candidates = std::move(westAndNorth);
                    level = 1;
                }
            }
            if (candidates.empty() && west >= 0 && !fromWest.empty()) {
                candidates = fromWest;
                level = 2;
            }
            if (candidates.empty() && north >= 0 && !fromNorth.empty()) {
                candidates = fromNorth;
                level = 3;
            }
            if (candidates.empty()) {
                for (const auto& entry : model.marginal)
                    candidates.push_back(entry.first);
                level = 4;
            }

            state[static_cast<size_t>(index)] = weightedPick(candidates, model, rng);
            ++result.stats.repairedCells;
            ++result.stats.repairLevel[static_cast<size_t>(level)];
        }

        // ---- Honest accounting: seams the reference never showed us ---------------------------------
        // Count each pair involving at least one region cell exactly once: west/north only against
        // non-region cells (a region west neighbour already counts the pair as its own east seam).
        for (int index : cellsToFill) {
            const int col = index % target.width;
            const int row = index / target.width;
            const auto id = static_cast<uint16_t>(state[static_cast<size_t>(index)]);
            const int32_t west = presentValue(col - 1, row);
            const int32_t north = presentValue(col, row - 1);
            const int32_t east = presentValue(col + 1, row);
            const int32_t south = presentValue(col, row + 1);
            if (west >= 0 && !inRegion[static_cast<size_t>(index - 1)] && !model.observedEast(static_cast<uint16_t>(west), id))
                ++result.stats.unresolvedSeams;
            if (north >= 0 && !inRegion[static_cast<size_t>(index - target.width)] && !model.observedSouth(static_cast<uint16_t>(north), id))
                ++result.stats.unresolvedSeams;
            if (east >= 0 && !model.observedEast(id, static_cast<uint16_t>(east)))
                ++result.stats.unresolvedSeams;
            if (south >= 0 && !model.observedSouth(id, static_cast<uint16_t>(south)))
                ++result.stats.unresolvedSeams;
        }

        result.cells.reserve(cellsToFill.size());
        for (int index : cellsToFill)
            result.cells.emplace_back(index, static_cast<uint16_t>(state[static_cast<size_t>(index)]));
        result.stats.cellsPainted = static_cast<int>(cellsToFill.size());
        return result;
    }

} // namespace floorsynth
} // namespace geck
