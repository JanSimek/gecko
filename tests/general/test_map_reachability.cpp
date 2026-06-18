#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

#include "cli/MapReachability.h"
#include "editor/HexGeometry.h"
#include "format/pro/Pro.h"

using namespace geck;

namespace {
constexpr int kCount = hexgrid::WIDTH * hexgrid::HEIGHT;
constexpr int kCenter = 100 * hexgrid::WIDTH + 100; // a comfortably interior hex
} // namespace

TEST_CASE("blocksMovementByInstance mirrors the engine's instance-flag rule", "[reachability]") {
    using OT = geck::Pro::OBJECT_TYPE;
    constexpr uint32_t kWall = static_cast<uint32_t>(OT::WALL);
    constexpr uint32_t kScenery = static_cast<uint32_t>(OT::SCENERY);
    constexpr uint32_t kCritter = static_cast<uint32_t>(OT::CRITTER);
    constexpr uint32_t kItem = static_cast<uint32_t>(OT::ITEM);
    constexpr uint32_t kMisc = static_cast<uint32_t>(OT::MISC);
    constexpr uint32_t kHidden = static_cast<uint32_t>(geck::Pro::ObjectFlags::OBJECT_HIDDEN);
    constexpr uint32_t kNoBlock = static_cast<uint32_t>(geck::Pro::ObjectFlags::OBJECT_NO_BLOCK);

    // Walls, scenery and critters block by default.
    CHECK(cli::blocksMovementByInstance(kWall, 0));
    CHECK(cli::blocksMovementByInstance(kScenery, 0));
    CHECK(cli::blocksMovementByInstance(kCritter, 0));

    // Items, misc and tiles never block (the engine's type filter) — this is the key fix.
    CHECK_FALSE(cli::blocksMovementByInstance(kItem, 0));
    CHECK_FALSE(cli::blocksMovementByInstance(kMisc, 0));

    // Per-instance flags override: hidden or explicitly no-block objects don't block.
    CHECK_FALSE(cli::blocksMovementByInstance(kWall, kHidden));
    CHECK_FALSE(cli::blocksMovementByInstance(kScenery, kNoBlock));
}

TEST_CASE("hexNeighbors yields parity-correct, symmetric neighbours", "[reachability]") {
    // An interior hex has all six neighbours.
    const std::vector<int> around = cli::hexNeighbors(kCenter);
    CHECK(around.size() == 6);

    // Neighbour-ness is symmetric: the centre is a neighbour of each of its neighbours.
    for (const int n : around) {
        const std::vector<int> back = cli::hexNeighbors(n);
        CHECK(std::find(back.begin(), back.end(), kCenter) != back.end());
    }

    // Off-grid positions have no neighbours.
    CHECK(cli::hexNeighbors(-1).empty());
    CHECK(cli::hexNeighbors(kCount).empty());
}

TEST_CASE("hexComponents: an open grid is a single walkable region", "[reachability]") {
    const std::vector<char> blocked(kCount, 0);
    std::vector<int> sizes;
    std::vector<int> samples;
    const std::vector<int> component = cli::hexComponents(blocked, sizes, samples);

    REQUIRE(sizes.size() == 1);
    CHECK(sizes[0] == kCount);
    CHECK(component[0] == 0);
    CHECK(component[kCenter] == 0);
}

TEST_CASE("hexComponents: sealing all six neighbours isolates a hex", "[reachability]") {
    // Block the full ring around the centre — in a hex grid the six neighbours fully enclose it, so
    // the centre must become its own one-hex component (this also proves the neighbour ring is correct).
    std::vector<char> blocked(kCount, 0);
    for (const int n : cli::hexNeighbors(kCenter)) {
        blocked[n] = 1;
    }

    std::vector<int> sizes;
    std::vector<int> samples;
    const std::vector<int> component = cli::hexComponents(blocked, sizes, samples);

    // Two components: the big surrounding region and the enclosed centre.
    REQUIRE(sizes.size() == 2);

    const int centreId = component[kCenter];
    REQUIRE(centreId != -1);         // the centre itself is walkable...
    CHECK(sizes[centreId] == 1);     // ...but cut off, a region of one hex
    CHECK(component[0] != centreId); // a far-away hex is in the other (main) region
    CHECK(samples[centreId] == kCenter);

    // The blocked ring hexes belong to no component.
    for (const int n : cli::hexNeighbors(kCenter)) {
        CHECK(component[n] == -1);
    }
}
