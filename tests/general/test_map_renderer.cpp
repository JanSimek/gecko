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

    SECTION("natural style") {
        MapRenderer::Options options;
        options.elevation = 0;
        CHECK_THROWS_AS(renderer.renderToImage(*map, options), std::runtime_error);
    }

    SECTION("schematic style fills no legend and reports the empty map") {
        MapRenderer::Options options;
        options.elevation = 0;
        options.style = MapRenderer::Style::Schematic;
        MapRenderer::Legend legend;
        CHECK_THROWS_AS(renderer.renderToImage(*map, options, &legend), std::runtime_error);
        // An empty map has no floor ids and no objects, so the legend stays empty.
        CHECK(legend.floors.empty());
        CHECK(legend.objects.empty());
    }
}
