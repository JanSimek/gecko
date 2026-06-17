#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "format/map/Map.h"
#include "resource/GameResources.h"
#include "ui/rendering/MapRenderer.h"

using namespace geck;

// A full render needs game art and an off-screen GL context (validated against real Fallout 2 data),
// but the "nothing to draw" guard is pure: an empty map has no floor sprites and no objects, so
// renderToImage() reports that instead of writing a blank image — and it does so before touching any
// texture or GL context, so this case runs on any machine, headless or not.
TEST_CASE("MapRenderer reports an empty map instead of rendering blank", "[render]") {
    resource::GameResources resources; // no data mounted
    auto map = std::make_unique<Map>("empty.map");
    map->setMapFile(std::make_unique<Map::MapFile>(Map::createEmptyMapFile()));

    MapRenderer renderer(resources);
    MapRenderer::Options options;
    options.elevation = 0;
    CHECK_THROWS_AS(renderer.renderToImage(*map, options), std::runtime_error);
}
