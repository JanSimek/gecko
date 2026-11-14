#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "editor/helper/ObjectOrder.h"
#include "format/map/MapObject.h"

#include "support/ControllerFixture.h"

using namespace geck;
using geck::object_order::Step;
using geck::test::ControllerFixture;

namespace {

using List = std::vector<std::shared_ptr<MapObject>>;

// Objects tagged by position so a failing CHECK prints a readable order.
List makeObjects(int count) {
    List list;
    for (int i = 0; i < count; ++i) {
        auto object = std::make_shared<MapObject>();
        object->position = i;
        list.push_back(std::move(object));
    }
    return list;
}

std::vector<int> positions(const List& list) {
    std::vector<int> result;
    for (const auto& object : list) {
        result.push_back(object->position);
    }
    return result;
}

// Overlap by position pairs, symmetric.
object_order::Overlaps overlapping(std::set<std::pair<int, int>> pairs) {
    return [pairs = std::move(pairs)](const MapObject& a, const MapObject& b) {
        return pairs.contains({ a.position, b.position }) || pairs.contains({ b.position, a.position });
    };
}

} // namespace

TEST_CASE("Bring to front and send to back keep the selection's own order", "[object_order]") {
    const auto list = makeObjects(5);
    const List selected{ list[1], list[3] };

    CHECK(positions(object_order::reordered(list, selected, Step::BringToFront)) == std::vector<int>{ 0, 2, 4, 1, 3 });
    CHECK(positions(object_order::reordered(list, selected, Step::SendToBack)) == std::vector<int>{ 1, 3, 0, 2, 4 });
}

TEST_CASE("Forward and backward step over the nearest unselected object without a predicate", "[object_order]") {
    const auto list = makeObjects(4);

    CHECK(positions(object_order::reordered(list, { list[1] }, Step::BringForward)) == std::vector<int>{ 0, 2, 1, 3 });
    CHECK(positions(object_order::reordered(list, { list[1] }, Step::SendBackward)) == std::vector<int>{ 1, 0, 2, 3 });
    // Already on top / at the bottom: nothing to hop over.
    CHECK(positions(object_order::reordered(list, { list[3] }, Step::BringForward)) == std::vector<int>{ 0, 1, 2, 3 });
    CHECK(positions(object_order::reordered(list, { list[0] }, Step::SendBackward)) == std::vector<int>{ 0, 1, 2, 3 });
}

TEST_CASE("Forward and backward hop over the next object the selection overlaps", "[object_order]") {
    const auto list = makeObjects(5);
    const auto overlaps = overlapping({ { 1, 3 } });

    // 2 doesn't overlap 1, so 1 passes it and lands just over 3.
    CHECK(positions(object_order::reordered(list, { list[1] }, Step::BringForward, overlaps)) == std::vector<int>{ 0, 2, 3, 1, 4 });
    CHECK(positions(object_order::reordered(list, { list[3] }, Step::SendBackward, overlaps)) == std::vector<int>{ 0, 3, 1, 2, 4 });
    // Nothing overlapping above: stays put.
    CHECK(positions(object_order::reordered(list, { list[3] }, Step::BringForward, overlaps)) == std::vector<int>{ 0, 1, 2, 3, 4 });
}

TEST_CASE("A stepping group never passes its own members", "[object_order]") {
    const auto list = makeObjects(4);

    // Both selected objects overlap 3, so both end up over it, still in their own order.
    const auto bothOverlap = overlapping({ { 1, 3 }, { 2, 3 }, { 1, 2 } });
    CHECK(positions(object_order::reordered(list, { list[1], list[2] }, Step::BringForward, bothOverlap)) == std::vector<int>{ 0, 3, 1, 2 });

    // Only the lower one overlaps 3, but the upper one is in the way: hopping past it would flip
    // the group, so the lower one stays.
    const auto lowerOverlaps = overlapping({ { 1, 3 } });
    CHECK(positions(object_order::reordered(list, { list[1], list[2] }, Step::BringForward, lowerOverlaps)) == std::vector<int>{ 0, 1, 2, 3 });

    CHECK(positions(object_order::reordered(list, { list[0], list[2] }, Step::BringForward)) == std::vector<int>{ 1, 0, 3, 2 });
    CHECK(positions(object_order::reordered(list, { list[1], list[3] }, Step::SendBackward)) == std::vector<int>{ 1, 0, 3, 2 });
}

TEST_CASE("An empty selection leaves the order alone", "[object_order]") {
    const auto list = makeObjects(3);
    CHECK(object_order::reordered(list, {}, Step::BringToFront) == list);
}

TEST_CASE("registerObjectReorder changes the map order with undo and redo", "[object_order][undo][controller]") {
    ControllerFixture fx;
    const auto list = makeObjects(3);
    fx.mapFile().map_objects[0] = list;

    const auto after = object_order::reordered(list, { list[0] }, Step::BringToFront);
    REQUIRE(fx.controller.registerObjectReorder(0, list, after, "Bring to Front"));
    CHECK(positions(fx.mapFile().map_objects[0]) == std::vector<int>{ 1, 2, 0 });

    REQUIRE(fx.undoStack.undo());
    CHECK(positions(fx.mapFile().map_objects[0]) == std::vector<int>{ 0, 1, 2 });

    REQUIRE(fx.undoStack.redo());
    CHECK(positions(fx.mapFile().map_objects[0]) == std::vector<int>{ 1, 2, 0 });
}

TEST_CASE("registerObjectReorder records nothing when the order is unchanged", "[object_order][undo][controller]") {
    ControllerFixture fx;
    const auto list = makeObjects(2);
    fx.mapFile().map_objects[0] = list;

    CHECK_FALSE(fx.controller.registerObjectReorder(0, list, list, "Bring to Front"));
    CHECK_FALSE(fx.undoStack.canUndo());
}
