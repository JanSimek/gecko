#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

#include "editor/HexGeometry.h"
#include "editor/Reachability.h"
#include "format/map/MapObject.h"
#include "format/pro/Pro.h"
#include "util/Constants.h"

#include <memory>

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
    CHECK(reachability::blocksMovementByInstance(kWall, 0));
    CHECK(reachability::blocksMovementByInstance(kScenery, 0));
    CHECK(reachability::blocksMovementByInstance(kCritter, 0));

    // Items, misc and tiles never block (the engine's type filter) — this is the key fix.
    CHECK_FALSE(reachability::blocksMovementByInstance(kItem, 0));
    CHECK_FALSE(reachability::blocksMovementByInstance(kMisc, 0));

    // Per-instance flags override: hidden or explicitly no-block objects don't block.
    CHECK_FALSE(reachability::blocksMovementByInstance(kWall, kHidden));
    CHECK_FALSE(reachability::blocksMovementByInstance(kScenery, kNoBlock));
}

TEST_CASE("hexNeighbors yields parity-correct, symmetric neighbours", "[reachability]") {
    // An interior hex has all six neighbours.
    const std::vector<int> around = reachability::hexNeighbors(kCenter);
    CHECK(around.size() == 6);

    // Neighbour-ness is symmetric: the centre is a neighbour of each of its neighbours.
    for (const int n : around) {
        const std::vector<int> back = reachability::hexNeighbors(n);
        CHECK(std::find(back.begin(), back.end(), kCenter) != back.end());
    }

    // Off-grid positions have no neighbours.
    CHECK(reachability::hexNeighbors(-1).empty());
    CHECK(reachability::hexNeighbors(kCount).empty());
}

TEST_CASE("hexComponents: an open grid is a single walkable region", "[reachability]") {
    const std::vector<char> blocked(kCount, 0);
    std::vector<int> sizes;
    std::vector<int> samples;
    const std::vector<int> component = reachability::hexComponents(blocked, sizes, samples);

    REQUIRE(sizes.size() == 1);
    CHECK(sizes[0] == kCount);
    CHECK(component[0] == 0);
    CHECK(component[kCenter] == 0);
}

TEST_CASE("hexComponents: sealing all six neighbours isolates a hex", "[reachability]") {
    // Block the full ring around the centre — in a hex grid the six neighbours fully enclose it, so
    // the centre must become its own one-hex component (this also proves the neighbour ring is correct).
    std::vector<char> blocked(kCount, 0);
    for (const int n : reachability::hexNeighbors(kCenter)) {
        blocked[n] = 1;
    }

    std::vector<int> sizes;
    std::vector<int> samples;
    const std::vector<int> component = reachability::hexComponents(blocked, sizes, samples);

    // Two components: the big surrounding region and the enclosed centre.
    REQUIRE(sizes.size() == 2);

    const int centreId = component[kCenter];
    REQUIRE(centreId != -1);         // the centre itself is walkable...
    CHECK(sizes[centreId] == 1);     // ...but cut off, a region of one hex
    CHECK(component[0] != centreId); // a far-away hex is in the other (main) region
    CHECK(samples[centreId] == kCenter);

    // The blocked ring hexes belong to no component.
    for (const int n : reachability::hexNeighbors(kCenter)) {
        CHECK(component[n] == -1);
    }
}

TEST_CASE("entryHexes: player start counts only on its own elevation; every exit grid always counts", "[reachability]") {
    const std::vector<std::shared_ptr<MapObject>> noObjects;

    // The player start seeds the flood only on the elevation the player spawns on.
    CHECK(reachability::entryHexes(1, 555, 1, noObjects) == std::vector<int>{ 555 });
    CHECK(reachability::entryHexes(0, 555, 1, noObjects).empty());

    // An exit-grid marker contributes its hex as an entry regardless of the player's elevation (you
    // arrive there when entering from the adjacent map).
    auto exitGrid = std::make_shared<MapObject>();
    exitGrid->position = 777;
    exitGrid->pro_pid = (static_cast<uint32_t>(Pro::OBJECT_TYPE::MISC) << FileFormat::TYPE_MASK_SHIFT)
        | MapObject::EXIT_GRID_PID_INDEX_FIRST;
    REQUIRE(exitGrid->isExitGridMarker());
    const std::vector<std::shared_ptr<MapObject>> objects{ exitGrid };

    // Player elsewhere: only the exit grid seeds. Player here: both the start and the exit grid.
    CHECK(reachability::entryHexes(0, 555, 1, objects) == std::vector<int>{ 777 });
    CHECK(reachability::entryHexes(1, 555, 1, objects) == std::vector<int>{ 555, 777 });
}

TEST_CASE("unreachableWalkableHexes: shades the walkable hexes cut off from every entry", "[reachability]") {
    // Seal the ring around the centre so it is a walkable one-hex pocket disconnected from the rest.
    std::vector<char> blocked(kCount, 0);
    for (const int n : reachability::hexNeighbors(kCenter)) {
        blocked[n] = 1;
    }
    std::vector<int> sizes;
    std::vector<int> samples;
    const std::vector<int> component = reachability::hexComponents(blocked, sizes, samples);

    // Enter from a far corner (in the big region), not the pocket.
    const std::vector<int> seeds{ 0 };
    const std::vector<char> entryReachable = reachability::seedComponentFlags(seeds, blocked, component, sizes.size());

    reachability::ElevationResult result;
    result.blocked = blocked;
    result.component = component;
    result.componentSizes = sizes;
    result.entrySeeds = seeds;
    result.entryReachable = entryReachable;

    const std::vector<int> unreachable = reachability::unreachableWalkableHexes(result);

    // Exactly the sealed pocket is unreachable-yet-walkable.
    REQUIRE(unreachable.size() == 1);
    CHECK(unreachable[0] == kCenter);
    // The seeded region is reachable, and the blocked ring hexes are impassable (not walkable), so
    // neither is shaded.
    CHECK(std::find(unreachable.begin(), unreachable.end(), 0) == unreachable.end());
    for (const int n : reachability::hexNeighbors(kCenter)) {
        CHECK(std::find(unreachable.begin(), unreachable.end(), n) == unreachable.end());
    }
}

TEST_CASE("unreachableWalkableHexes: shades nothing when the elevation has no entry point", "[reachability]") {
    // With no player start and no exit grid here, reachability is undetermined — shade nothing rather
    // than flagging the whole (unreachable-by-this-model) elevation.
    reachability::ElevationResult result;
    result.blocked.assign(kCount, 0); // everything walkable, but no entrySeeds
    result.component.assign(kCount, 0);
    result.componentSizes = { kCount };
    result.entryReachable = { 0 };
    REQUIRE_FALSE(result.hasEntryPoints());
    CHECK(reachability::unreachableWalkableHexes(result).empty());
}
