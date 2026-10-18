#include <catch2/catch_test_macros.hpp>
#include <SFML/Graphics.hpp>
#include <filesystem>
#include <memory>
#include "editor/Object.h"
#include "format/frm/Frm.h"
#include "format/map/MapObject.h"
#include "support/ControllerFixture.h"
#include "support/GraphicsTestEnv.h"

using namespace geck;

namespace {
// The game's Light Source scenery draws art/scenery/block.frm: one transparent pixel, in each direction.
Frm makeArt(bool visible) {
    Frm frm("art.frm");
    Frame frame(1, 1);
    frame.data()[0] = visible ? 1 : 0;
    Direction direction;
    direction.setFrames({ frame });
    frm.setDirections({ direction, direction, direction, direction, direction, direction });
    return frm;
}

struct MarkerFixture : test::ControllerFixture {
    MarkerFixture() {
        // light.frm ships with gecko in resources/art/misc, next to the bundled scripts; the fixtures
        // hold the color.pal its texture is built with.
        resources.files().addDataPath((std::filesystem::path(GECK_SCRIPTS_DIR).parent_path() / "resources").string());
        resources.files().addDataPath(GECK_TEST_DATA_DIR);
    }

    std::shared_ptr<Object> place(const Frm& art, uint32_t lightRadius, uint32_t lightIntensity) {
        auto mapObject = std::make_shared<MapObject>();
        mapObject->position = 20100;
        mapObject->direction = 2;
        mapObject->light_radius = lightRadius;
        mapObject->light_intensity = lightIntensity;
        auto object = std::make_shared<Object>(&art);
        object->setMapObject(mapObject);
        object->setDirection(Rotation(2));
        object->setHexPosition(hexgrid.getHexByPosition(20100)->get());
        spriteLoader.appendObjectSprite(mapObject, object, objects, overlays);
        return object;
    }
};
} // namespace

TEST_CASE("A light whose art draws nothing is shown with the light marker", "[light][sprites]") {
    GECK_SKIP_IF_HEADLESS_CI(); // an Object owns an sf::Texture
    MarkerFixture fx;
    const Frm invisible = makeArt(false);

    const auto light = fx.place(invisible, 3, 65536);
    CHECK(light->getFrm() != &invisible);
    CHECK(light->width() == 32); // light.frm, which has visible pixels to click
    CHECK(light->height() == 16);
    CHECK(light->getSprite().getGlobalBounds().size == sf::Vector2f{ 32.f, 16.f });
    // Only the drawing changes: the map keeps the object's own placement.
    CHECK(light->getMapObject().direction == 2);
    CHECK(light->getMapObject().position == 20100);
}

TEST_CASE("Only invisible lights get the light marker", "[light][sprites]") {
    GECK_SKIP_IF_HEADLESS_CI(); // an Object owns an sf::Texture
    MarkerFixture fx;
    const Frm invisible = makeArt(false);
    const Frm visible = makeArt(true);

    CHECK(fx.place(invisible, 0, 0)->getFrm() == &invisible); // no light, no proto to give one
    CHECK(fx.place(visible, 3, 65536)->getFrm() == &visible); // a lamp draws its own art
}
