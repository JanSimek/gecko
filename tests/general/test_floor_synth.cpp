#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <functional>
#include <random>
#include <set>

#include "editor/FloorSynth.h"

using namespace geck::floorsynth;

namespace {

constexpr uint16_t EMPTY = 1;

Grid makeGrid(int width, int height, const std::function<uint16_t(int, int)>& cellAt) {
    Grid grid;
    grid.width = width;
    grid.height = height;
    grid.cells.resize(static_cast<size_t>(width) * height);
    for (int row = 0; row < height; ++row)
        for (int col = 0; col < width; ++col)
            grid.cells[static_cast<size_t>(row) * width + col] = cellAt(col, row);
    return grid;
}

// A 4x4-periodic pattern of 16 distinct ids: every KxK window recurs many times, every seam of a
// phase-consistent copy is observed, and distinct block picks produce visibly different phases.
uint16_t periodicId(int col, int row) {
    return static_cast<uint16_t>(100 + (col % 4) + 4 * (row % 4));
}

std::vector<int> rectRegion(const Grid& target, int col0, int row0, int col1, int row1) {
    std::vector<int> region;
    for (int row = row0; row <= row1; ++row)
        for (int col = col0; col <= col1; ++col)
            region.push_back(row * target.width + col);
    return region;
}

// Applies a result to a copy of the target so seams can be walked on the final grid.
Grid applied(const Grid& target, const Result& result) {
    Grid out = target;
    for (const auto& [index, id] : result.cells)
        out.cells[static_cast<size_t>(index)] = id;
    return out;
}

int countUnobservedSeams(const Grid& grid, const AdjacencyModel& model) {
    int bad = 0;
    for (int row = 0; row < grid.height; ++row) {
        for (int col = 0; col < grid.width; ++col) {
            const uint16_t a = grid.at(col, row);
            if (a == EMPTY)
                continue;
            if (col + 1 < grid.width && grid.at(col + 1, row) != EMPTY && !model.observedEast(a, grid.at(col + 1, row)))
                ++bad;
            if (row + 1 < grid.height && grid.at(col, row + 1) != EMPTY && !model.observedSouth(a, grid.at(col, row + 1)))
                ++bad;
        }
    }
    return bad;
}

} // namespace

TEST_CASE("learnAdjacency records ordered directional pairs", "[floorsynth]") {
    // [ 10 11 ]
    // [ 12 10 ]
    const Grid grid = makeGrid(2, 2, [](int col, int row) {
        constexpr uint16_t ids[2][2] = { { 10, 11 }, { 12, 10 } };
        return ids[row][col];
    });
    const AdjacencyModel model = learnAdjacency(grid, EMPTY);

    CHECK(model.east.size() == 2);
    CHECK(model.east.at({ 10, 11 }) == 1);
    CHECK(model.east.at({ 12, 10 }) == 1);
    CHECK(model.south.size() == 2);
    CHECK(model.south.at({ 10, 12 }) == 1);
    CHECK(model.south.at({ 11, 10 }) == 1);
    // Direction is not symmetric: the reversed pairs were never observed.
    CHECK_FALSE(model.observedEast(11, 10));
    CHECK_FALSE(model.observedSouth(12, 10));
    CHECK(model.marginal.at(10) == 2);
    CHECK(model.marginal.at(11) == 1);
    CHECK(model.marginal.at(12) == 1);
}

TEST_CASE("learnAdjacency never counts pairs touching the empty id", "[floorsynth]") {
    // [ 10  E ]
    // [  E 11 ]
    const Grid grid = makeGrid(2, 2, [](int col, int row) {
        constexpr uint16_t ids[2][2] = { { 10, EMPTY }, { EMPTY, 11 } };
        return ids[row][col];
    });
    const AdjacencyModel model = learnAdjacency(grid, EMPTY);

    CHECK(model.east.empty());
    CHECK(model.south.empty());
    CHECK(model.marginal.size() == 2);
    CHECK(model.marginal.count(EMPTY) == 0);
}

TEST_CASE("synthesize covers the region exactly, ascending, without the empty id", "[floorsynth]") {
    const Grid reference = makeGrid(20, 20, periodicId);
    const Grid target = makeGrid(30, 30, [](int, int) { return EMPTY; });
    const std::vector<int> region = rectRegion(target, 0, 0, 29, 29);

    Params params;
    params.seed = 7;
    const Result result = synthesize(reference, target, region, params);

    REQUIRE(result.cells.size() == region.size());
    CHECK(result.stats.cellsPainted == static_cast<int>(region.size()));
    std::set<int> sortedRegion(region.begin(), region.end());
    int previousIndex = -1;
    for (const auto& [index, id] : result.cells) {
        CHECK(index > previousIndex); // ascending, no duplicates
        previousIndex = index;
        CHECK(sortedRegion.count(index) == 1);
        CHECK(id != EMPTY);
        CHECK(id >= 100);
        CHECK(id <= 115);
    }
}

TEST_CASE("synthesize produces only reference-observed seams on a clean run", "[floorsynth]") {
    const Grid reference = makeGrid(20, 20, periodicId);
    const Grid target = makeGrid(30, 30, [](int, int) { return EMPTY; });
    const std::vector<int> region = rectRegion(target, 0, 0, 29, 29);

    Params params;
    params.seed = 3;
    const Result result = synthesize(reference, target, region, params);
    const AdjacencyModel model = learnAdjacency(reference, EMPTY);

    CHECK(countUnobservedSeams(applied(target, result), model) == 0);
    CHECK(result.stats.unresolvedSeams == 0);
    CHECK(result.stats.blocksPlaced > 0);
    CHECK(result.stats.perfectBlocks == result.stats.blocksPlaced);
    CHECK(result.stats.mismatchedCells == 0);
}

TEST_CASE("synthesize is deterministic for a seed and varies across seeds", "[floorsynth]") {
    const Grid reference = makeGrid(20, 20, periodicId);
    const Grid target = makeGrid(24, 24, [](int, int) { return EMPTY; });
    const std::vector<int> region = rectRegion(target, 0, 0, 23, 23);

    Params params;
    params.seed = 42;
    const Result first = synthesize(reference, target, region, params);
    const Result second = synthesize(reference, target, region, params);
    CHECK(first.cells == second.cells);

    // The periodic reference admits 16 phases, all zero-penalty for the first block, so some
    // other seed must land on a different phase (any single pair may collide by chance; the
    // algorithm is deterministic, so whichever seed differs keeps differing).
    bool anySeedDiffers = false;
    for (uint32_t seed = 43; seed < 51 && !anySeedDiffers; ++seed) {
        params.seed = seed;
        anySeedDiffers = synthesize(reference, target, region, params).cells != first.cells;
    }
    CHECK(anySeedDiffers);
}

TEST_CASE("synthesize result is independent of the caller's region ordering", "[floorsynth]") {
    const Grid reference = makeGrid(20, 20, periodicId);
    const Grid target = makeGrid(16, 16, [](int, int) { return EMPTY; });
    std::vector<int> region = rectRegion(target, 2, 2, 13, 13);

    Params params;
    params.seed = 11;
    const Result sorted = synthesize(reference, target, region, params);

    std::mt19937 shuffler(1234);
    std::shuffle(region.begin(), region.end(), shuffler);
    region.push_back(region.front()); // duplicates are ignored too
    region.push_back(-5);             // off-grid indices are ignored
    region.push_back(target.width * target.height + 3);
    const Result shuffled = synthesize(reference, target, region, params);

    CHECK(sorted.cells == shuffled.cells);
}

TEST_CASE("synthesize blends into pre-existing tiles: hole inpainting restores the pattern", "[floorsynth]") {
    const Grid reference = makeGrid(20, 20, periodicId);
    // Target carries the same pattern (phase 0) with a 10x10 hole; the only zero-penalty
    // continuation of the surrounding constraints is the original content.
    Grid target = makeGrid(30, 30, periodicId);
    const std::vector<int> region = rectRegion(target, 10, 10, 19, 19);
    for (int index : region)
        target.cells[static_cast<size_t>(index)] = EMPTY;

    Params params;
    params.seed = 5;
    const Result result = synthesize(reference, target, region, params);

    for (const auto& [index, id] : result.cells)
        CHECK(id == periodicId(index % target.width, index / target.width));
    CHECK(result.stats.unresolvedSeams == 0);
    CHECK(result.stats.repairedCells == 0);
}

TEST_CASE("synthesize never bridges vocabularies an empty stripe separates", "[floorsynth]") {
    // Left 10 columns: vocabulary A (periodic). Column 10: empty stripe. Right 10 columns:
    // vocabulary B (periodic, disjoint ids). No A|B pair exists in the reference.
    const Grid reference = makeGrid(21, 20, [](int col, int row) -> uint16_t {
        if (col == 10)
            return EMPTY;
        if (col < 10)
            return periodicId(col, row);
        return static_cast<uint16_t>(200 + ((col - 11) % 4) + 4 * (row % 4));
    });
    const AdjacencyModel model = learnAdjacency(reference, EMPTY);
    for (const auto& entry : model.east) {
        const bool aLeft = entry.first.first < 200;
        const bool bLeft = entry.first.second < 200;
        CHECK(aLeft == bLeft);
    }

    const Grid target = makeGrid(20, 20, [](int, int) { return EMPTY; });
    const std::vector<int> region = rectRegion(target, 0, 0, 19, 19);
    Params params;
    params.seed = 9;
    const Result result = synthesize(reference, target, region, params);
    const Grid out = applied(target, result);

    // Overlap matching keeps the whole output in one vocabulary — and in particular no A id ever
    // neighbours a B id.
    CHECK(countUnobservedSeams(out, model) == 0);
    const bool firstLeft = out.at(0, 0) < 200;
    for (const auto& [index, id] : result.cells)
        CHECK((id < 200) == firstLeft);
}

TEST_CASE("synthesize fills an irregular region and touches nothing else", "[floorsynth]") {
    const Grid reference = makeGrid(20, 20, periodicId);
    const Grid target = makeGrid(20, 20, [](int, int) { return EMPTY; });

    // A diamond blob around (10, 10).
    std::vector<int> region;
    for (int row = 0; row < target.height; ++row)
        for (int col = 0; col < target.width; ++col)
            if (std::abs(col - 10) + std::abs(row - 10) <= 6)
                region.push_back(row * target.width + col);

    Params params;
    params.seed = 21;
    const Result result = synthesize(reference, target, region, params);

    REQUIRE(result.cells.size() == region.size());
    const std::set<int> regionSet(region.begin(), region.end());
    for (const auto& [index, id] : result.cells) {
        CHECK(regionSet.count(index) == 1);
        CHECK(id != EMPTY);
    }
}

TEST_CASE("synthesize degrades the block size for a tiny reference and still completes", "[floorsynth]") {
    // 3x3 reference: blockSize 5 must degrade to K=3 windows.
    const Grid reference = makeGrid(3, 3, [](int col, int row) {
        return static_cast<uint16_t>(50 + col + 3 * row);
    });
    const Grid target = makeGrid(10, 10, [](int, int) { return EMPTY; });
    const std::vector<int> region = rectRegion(target, 0, 0, 9, 9);

    Params params;
    params.seed = 13;
    const Result result = synthesize(reference, target, region, params);

    REQUIRE(result.cells.size() == region.size());
    for (const auto& [index, id] : result.cells) {
        (void)index;
        CHECK(id >= 50);
        CHECK(id <= 58);
    }
}

TEST_CASE("synthesize reports honest stats when the reference cannot satisfy the seams", "[floorsynth]") {
    // A 1x1 reference has no windows and no observed pairs: every cell falls to the marginal rung
    // and every generated seam stays unobserved — the stats must say so, loudly.
    const Grid reference = makeGrid(1, 1, [](int, int) { return static_cast<uint16_t>(77); });
    const Grid target = makeGrid(4, 4, [](int, int) { return EMPTY; });
    const std::vector<int> region = rectRegion(target, 0, 0, 3, 3);

    Params params;
    params.seed = 1;
    const Result result = synthesize(reference, target, region, params);

    REQUIRE(result.cells.size() == region.size());
    for (const auto& [index, id] : result.cells) {
        (void)index;
        CHECK(id == 77);
    }
    CHECK(result.stats.blocksPlaced == 0);
    CHECK(result.stats.repairedCells == 16);
    CHECK(result.stats.repairLevel[4] == 16);
    CHECK(result.stats.unresolvedSeams == 24); // all 2*4*3 internal pairs, (77,77) never observed
}

TEST_CASE("synthesize assigns nothing for an all-empty reference or an empty region", "[floorsynth]") {
    const Grid emptyReference = makeGrid(5, 5, [](int, int) { return EMPTY; });
    const Grid target = makeGrid(6, 6, [](int, int) { return EMPTY; });

    const Result noVocabulary = synthesize(emptyReference, target, rectRegion(target, 0, 0, 5, 5), Params{});
    CHECK(noVocabulary.cells.empty());
    CHECK(noVocabulary.stats.cellsPainted == 0);

    const Grid reference = makeGrid(20, 20, periodicId);
    const Result noRegion = synthesize(reference, target, {}, Params{});
    CHECK(noRegion.cells.empty());
    CHECK(noRegion.stats.cellsPainted == 0);
}
