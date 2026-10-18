#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "editor/HexGeometry.h"
#include "format/frm/FrmId.h"
#include "format/map/MapObject.h"
#include "format/pro/ProtoTypes.h"
#include "pattern/Pattern.h"
#include "pattern/PatternBuilder.h"
#include "pattern/PatternStamper.h"

using namespace geck;
using namespace geck::pattern;

namespace {

constexpr int ANCHOR = 80 * hexgrid::WIDTH + 80; // col 80, row 80

constexpr uint32_t sceneryPid(uint32_t index) { return ProtoId(ObjectType::Scenery, index).pid(); }
constexpr uint32_t critterFid(uint32_t frameId) { return FrmId(ObjectType::Critter, frameId).fid(); }

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

TEST_CASE("PatternBuilder::buildVariant captures offsets and engine ids verbatim", "[pattern_builder]") {
    const MapObject a = objAt(ANCHOR, sceneryPid(769), critterFid(129), 0u, 0u);
    const MapObject b = objAt(ANCHOR + 1, sceneryPid(770), critterFid(130), 2u, 0x10u);           // col +1
    const MapObject c = objAt(ANCHOR + hexgrid::WIDTH, sceneryPid(771), critterFid(131), 4u, 0u); // row +1
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
    CHECK(v.objects[0].proPid == sceneryPid(769));
    CHECK(v.objects[1].dxHex == 1);
    CHECK(v.objects[1].direction == 2u);
    CHECK(v.objects[1].flags == 0x10u);
    CHECK(v.objects[2].dyHex == 1);
    CHECK(v.objects[2].frmPid == critterFid(131));

    REQUIRE(v.floor.size() == 2);
    CHECK(v.floor[0].dxTile == 0);
    CHECK(v.floor[0].dyTile == 0);
    CHECK(v.floor[0].tileId == 271);
    CHECK(v.floor[1].dxTile == 1);
    CHECK(v.floor[1].tileId == 272);
}

TEST_CASE("PatternBuilder and PatternStamper are inverses", "[pattern_builder]") {
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
        // Same column parity as the anchor (col 80), so the stamper's parity-snap is a
        // no-op and the placement is exactly the cube translation.
        const int target = 120 * hexgrid::WIDTH + 90;
        const auto plan = PatternStamper::plan(v, target);
        REQUIRE(plan.objects.size() == 3);
        for (size_t i = 0; i < objects.size(); ++i) {
            CHECK(plan.objects[i].hex == hexgrid::translate(objects[i]->position, ANCHOR, target));
        }
    }
}
