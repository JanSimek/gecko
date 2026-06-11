#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "editor/HexGeometry.h"
#include "format/map/MapObject.h"
#include "pattern/Pattern.h"
#include "pattern/PatternBuilder.h"
#include "pattern/PatternStamper.h"

using namespace geck;
using namespace geck::pattern;

namespace {

constexpr int ANCHOR = 80 * hexgrid::WIDTH + 80; // col 80, row 80

MapObject objAt(int hex, uint32_t proPid, uint32_t frmPid, uint32_t direction, uint32_t flags) {
    MapObject o;
    o.position = hex;
    o.pro_pid = proPid;
    o.frm_pid = frmPid;
    o.direction = direction;
    o.flags = flags;
    return o;
}

} // namespace

TEST_CASE("PatternBuilder::buildVariant captures offsets and engine ids verbatim", "[pattern][builder]") {
    const MapObject a = objAt(ANCHOR, 33555201u, 16777345u, 0u, 0u);
    const MapObject b = objAt(ANCHOR + 1, 33555202u, 16777346u, 2u, 0x10u);           // col +1
    const MapObject c = objAt(ANCHOR + hexgrid::WIDTH, 33555203u, 16777347u, 4u, 0u); // row +1
    const std::vector<const MapObject*> objects{ &a, &b, &c };

    // Floor tile at the anchor's own tile (col 40, row 40 -> index 4040), and one shifted.
    const int anchorTile = 40 * HexagonGrid::TILE_GRID_WIDTH + 40;
    const std::vector<PatternBuilder::TileSelection> floor{
        { anchorTile, 271 },
        { anchorTile + 1, 272 }, // col +1
    };

    const PatternVariant v = PatternBuilder::buildVariant(objects, floor, {}, ANCHOR, "default");

    CHECK(v.anchorHex == ANCHOR);
    REQUIRE(v.objects.size() == 3);
    CHECK(v.objects[0].dxHex == 0);
    CHECK(v.objects[0].dyHex == 0);
    CHECK(v.objects[0].proPid == 33555201u);
    CHECK(v.objects[1].dxHex == 1);
    CHECK(v.objects[1].direction == 2u);
    CHECK(v.objects[1].flags == 0x10u);
    CHECK(v.objects[2].dyHex == 1);
    CHECK(v.objects[2].frmPid == 16777347u);

    REQUIRE(v.floor.size() == 2);
    CHECK(v.floor[0].dxTile == 0);
    CHECK(v.floor[0].dyTile == 0);
    CHECK(v.floor[0].tileId == 271);
    CHECK(v.floor[1].dxTile == 1);
    CHECK(v.floor[1].tileId == 272);
}

TEST_CASE("PatternBuilder and PatternStamper are inverses", "[pattern][builder]") {
    const MapObject a = objAt(ANCHOR, 1u, 1u, 0u, 0u);
    const MapObject b = objAt(ANCHOR + 2, 2u, 2u, 1u, 0u);
    const MapObject c = objAt(ANCHOR + 3 * hexgrid::WIDTH - 1, 3u, 3u, 5u, 0u);
    const std::vector<const MapObject*> objects{ &a, &b, &c };

    const PatternVariant v = PatternBuilder::buildVariant(objects, {}, {}, ANCHOR, "default");

    SECTION("stamping at the original anchor reproduces the original hexes") {
        const auto plan = PatternStamper::plan(v, ANCHOR);
        REQUIRE(plan.objects.size() == 3);
        CHECK(plan.objects[0].hex == a.position);
        CHECK(plan.objects[1].hex == b.position);
        CHECK(plan.objects[2].hex == c.position);
    }

    SECTION("stamping at a new anchor translates every object by the same cube delta") {
        const int target = 120 * hexgrid::WIDTH + 91; // different column parity
        const auto plan = PatternStamper::plan(v, target);
        REQUIRE(plan.objects.size() == 3);
        for (size_t i = 0; i < objects.size(); ++i) {
            CHECK(plan.objects[i].hex == hexgrid::translate(objects[i]->position, ANCHOR, target));
        }
    }
}
