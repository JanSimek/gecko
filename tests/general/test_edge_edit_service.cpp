#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <functional>
#include <memory>

#include "editing/commands/EdgeEditService.h"
#include "editing/commands/UndoBatcher.h"
#include "editor/HexagonGrid.h"
#include "format/map/Map.h"
#include "format/map/MapEdge.h"
#include "util/UndoStack.h"

using namespace geck;

namespace {
// EdgeEditService only touches Map::edge()/setEdge(), so a bare Map (no MapFile) is enough.
struct EdgeFixture {
    UndoStack stack{ 100 };
    UndoBatcher batcher{ stack, [] { } };
    std::unique_ptr<Map> map = std::make_unique<Map>(std::filesystem::path{});
    EdgeEditService svc{ map, batcher };

    static MapEdge::Rect seed() { return MapEdge::Rect{ 100, 200, 300, 400 }; }
};
} // namespace

TEST_CASE("EdgeEditService::addZone creates the edge and is undoable", "[edge_edit]") {
    EdgeFixture f;
    CHECK_FALSE(f.svc.hasEdge());

    const int index = f.svc.addZone(0, EdgeFixture::seed());
    CHECK(index == 0);
    REQUIRE(f.svc.hasEdge());
    CHECK(f.svc.zoneCount(0) == 1);
    CHECK(f.svc.edge()->elevations[0].zones[0] == EdgeFixture::seed());

    REQUIRE(f.stack.undo());
    CHECK_FALSE(f.svc.hasEdge()); // back to no edge at all

    REQUIRE(f.stack.redo());
    REQUIRE(f.svc.hasEdge());
    CHECK(f.svc.zoneCount(0) == 1);
}

TEST_CASE("EdgeEditService::setZoneSide moves a corner and round-trips through undo/redo", "[edge_edit]") {
    EdgeFixture f;
    f.svc.addZone(1, EdgeFixture::seed());

    CHECK(f.svc.setZoneSide(1, 0, EdgeEditService::LEFT, 1234));
    CHECK(f.svc.edge()->elevations[1].zones[0].left == 1234);

    REQUIRE(f.stack.undo());
    CHECK(f.svc.edge()->elevations[1].zones[0].left == EdgeFixture::seed().left);
    REQUIRE(f.stack.redo());
    CHECK(f.svc.edge()->elevations[1].zones[0].left == 1234);
}

TEST_CASE("EdgeEditService::setZoneSide rejects bad args", "[edge_edit]") {
    EdgeFixture f;
    f.svc.addZone(0, EdgeFixture::seed());

    CHECK_FALSE(f.svc.setZoneSide(0, 0, EdgeEditService::RIGHT, -1));
    CHECK_FALSE(f.svc.setZoneSide(0, 0, EdgeEditService::RIGHT, HexagonGrid::POSITION_COUNT));
    CHECK_FALSE(f.svc.setZoneSide(0, 9, EdgeEditService::RIGHT, 10)); // no such zone
    CHECK_FALSE(f.svc.setZoneSide(9, 0, EdgeEditService::RIGHT, 10)); // no such elevation
}

TEST_CASE("EdgeEditService::deleteZone removes the right zone and is undoable", "[edge_edit]") {
    EdgeFixture f;
    f.svc.addZone(0, EdgeFixture::seed());
    f.svc.addZone(0, MapEdge::Rect{ 1, 2, 3, 4 });
    CHECK(f.svc.zoneCount(0) == 2);

    CHECK(f.svc.deleteZone(0, 0));
    CHECK(f.svc.zoneCount(0) == 1);
    CHECK(f.svc.edge()->elevations[0].zones[0] == (MapEdge::Rect{ 1, 2, 3, 4 }));

    REQUIRE(f.stack.undo());
    CHECK(f.svc.zoneCount(0) == 2);
    CHECK(f.svc.edge()->elevations[0].zones[0] == EdgeFixture::seed());
}

TEST_CASE("EdgeEditService live side drag previews without undo, commits once", "[edge_edit]") {
    EdgeFixture f;
    f.svc.addZone(0, EdgeFixture::seed()); // undo entry #1 (creates the edge)

    // Drag start: snapshot the edge, then preview several mouse-moves (no undo recorded per move).
    const std::optional<MapEdge> before = f.svc.snapshot();
    f.svc.previewZoneSide(0, 0, EdgeEditService::LEFT, 500);
    f.svc.previewZoneSide(0, 0, EdgeEditService::LEFT, 800);
    f.svc.previewZoneSide(0, 0, EdgeEditService::LEFT, 1200);
    CHECK(f.svc.edge()->elevations[0].zones[0].left == 1200);

    // Release: commit the whole gesture as a single undo entry (#2).
    CHECK(f.svc.commitEdit("Move Edge Side", before));

    REQUIRE(f.stack.undo()); // one undo reverts the entire drag, not just the last move
    CHECK(f.svc.edge()->elevations[0].zones[0].left == EdgeFixture::seed().left);
    REQUIRE(f.stack.redo());
    CHECK(f.svc.edge()->elevations[0].zones[0].left == 1200);

    // Exactly two commands were ever recorded (drag + add): the previews added none. Two undos empty
    // the stack.
    REQUIRE(f.stack.undo()); // undo the drag
    REQUIRE(f.stack.undo()); // undo the add
    CHECK_FALSE(f.svc.hasEdge());
    CHECK_FALSE(f.stack.canUndo());
}

TEST_CASE("EdgeEditService restore discards a preview without recording", "[edge_edit]") {
    EdgeFixture f;
    f.svc.addZone(0, EdgeFixture::seed()); // the only undo entry

    const std::optional<MapEdge> before = f.svc.snapshot();
    f.svc.previewZoneSide(0, 0, EdgeEditService::TOP, 777);
    CHECK(f.svc.edge()->elevations[0].zones[0].top == 777);

    f.svc.restore(before); // drag cancel
    CHECK(f.svc.edge()->elevations[0].zones[0].top == EdgeFixture::seed().top);

    // An unchanged commit (no net move) records nothing.
    CHECK_FALSE(f.svc.commitEdit("Move Edge Side", before));

    // Only the add is on the stack: neither restore nor the no-op commit recorded anything.
    REQUIRE(f.stack.undo());
    CHECK_FALSE(f.svc.hasEdge());
    CHECK_FALSE(f.stack.canUndo());
}

TEST_CASE("EdgeEditService rejects v2 square/clip edits on a v1 edge", "[edge_edit]") {
    EdgeFixture f;
    f.svc.addZone(0, EdgeFixture::seed()); // edge starts at v1
    REQUIRE(f.svc.edge()->version == 1);

    // These only serialize for v2, so they must be rejected (not silently non-persisting edits).
    CHECK_FALSE(f.svc.toggleClipSide(0, EdgeEditService::LEFT));
    CHECK_FALSE(f.svc.setSquareSide(0, EdgeEditService::LEFT, 10));
    CHECK_FALSE(f.svc.resetSquare(0));
    CHECK_FALSE(f.svc.edge()->elevations[0].clipSides.left); // v1 block untouched

    // Only the addZone is on the undo stack — the three rejected edits recorded nothing.
    REQUIRE(f.stack.undo());
    CHECK_FALSE(f.stack.canUndo());
}

TEST_CASE("EdgeEditService v2 upgrade, clip, square and reset are undoable", "[edge_edit]") {
    EdgeFixture f;
    f.svc.addZone(0, EdgeFixture::seed()); // creates the edge as v1
    CHECK(f.svc.edge()->version == 1);

    CHECK(f.svc.upgradeToVersion2());
    CHECK(f.svc.edge()->isVersion2());
    CHECK_FALSE(f.svc.upgradeToVersion2()); // already v2 -> no-op

    CHECK(f.svc.toggleClipSide(0, EdgeEditService::TOP));
    CHECK(f.svc.edge()->elevations[0].clipSides.top);

    CHECK(f.svc.setSquareSide(0, EdgeEditService::LEFT, 42));
    CHECK(f.svc.edge()->elevations[0].squareRect.left == 42);
    CHECK(f.svc.setSquareSide(0, EdgeEditService::LEFT, 9999)); // clamped to grid width - 1
    CHECK(f.svc.edge()->elevations[0].squareRect.left == MapEdge::SQUARE_GRID_WIDTH - 1);

    CHECK(f.svc.resetSquare(0));
    CHECK(f.svc.edge()->elevations[0].squareRect
        == (MapEdge::Rect{ MapEdge::SQUARE_GRID_WIDTH - 1, 0, 0, MapEdge::SQUARE_GRID_HEIGHT - 1 }));
    CHECK_FALSE(f.svc.edge()->elevations[0].clipSides.top); // reset cleared clip flags

    REQUIRE(f.stack.undo()); // undo the reset restores clip.top
    CHECK(f.svc.edge()->elevations[0].clipSides.top);
}
