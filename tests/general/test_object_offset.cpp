#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <vector>

#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "format/frm/Direction.h"
#include "format/frm/Frame.h"
#include "format/frm/Frm.h"
#include "format/map/MapObject.h"

using namespace geck;

namespace {

// A one-direction, one-frame 20x10 FRM shifted (3, -2), as a scenery piece's header gives it.
Frm makeFrm() {
    Frm frm{ "test.frm" };
    Direction direction;
    direction.setShiftX(3);
    direction.setShiftY(-2);
    direction.setFrames({ Frame{ 20, 10 } });
    frm.setDirections({ direction });
    return frm;
}

} // namespace

TEST_CASE("An object is drawn at its hex plus its own pixel offset, as the engine draws it", "[object]") {
    HexagonGrid grid;
    const Frm frm = makeFrm();
    const Hex& hex = grid.getHexByPosition(19280)->get();

    auto mapObject = std::make_shared<MapObject>();
    mapObject->x = static_cast<uint32_t>(-16);
    mapObject->y = static_cast<uint32_t>(-36);

    Object object{ &frm };
    object.setMapObject(mapObject);
    object.setHexPosition(hex);
    // engine: hex + (16, 8) + header shift + obj->x/y, then left = x - width/2, top = y - height + 1;
    // gecko's hex coordinates already hold the (16, 8)
    CHECK(object.getSprite().getPosition().x == static_cast<float>(hex.x() - 10 + 3 - 16));
    CHECK(object.getSprite().getPosition().y == static_cast<float>(hex.y() - 10 - 2 - 36));
    CHECK(mapObject->position == 19280);
}

TEST_CASE("Setting the MapObject after the hex applies its offset, and a new offset shows on refresh", "[object]") {
    HexagonGrid grid;
    const Frm frm = makeFrm();
    const Hex& hex = grid.getHexByPosition(12345)->get();

    Object object{ &frm };
    object.setHexPosition(hex);
    const auto plain = object.getSprite().getPosition();

    auto mapObject = std::make_shared<MapObject>();
    mapObject->x = 5;
    mapObject->y = static_cast<uint32_t>(-7);
    object.setMapObject(mapObject);
    CHECK(object.getSprite().getPosition().x == plain.x + 5);
    CHECK(object.getSprite().getPosition().y == plain.y - 7);

    mapObject->x = 0;
    mapObject->y = 0;
    object.refreshPosition();
    CHECK(object.getSprite().getPosition() == plain);
}
