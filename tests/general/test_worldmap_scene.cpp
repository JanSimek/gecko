#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "editor/worldmap/WorldMapScene.h"
#include "format/pal/Pal.h"
#include "resource/GameResources.h"

#include "support/DataTree.h"

using namespace geck;

namespace {

// A structurally valid color.pal: 256 RGB triplets in the palette's 6-bit range, then the
// 32,768-entry RGB555 -> index conversion table. The colours themselves do not matter here; what
// matters is that PalReader accepts the file, so the scene gets as far as the art it cannot find.
std::vector<std::uint8_t> palBytes() {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(768 + Pal::NUM_CONVERSION_VALUES);
    for (int index = 0; index < 256; ++index) {
        const auto channel = static_cast<std::uint8_t>(index % 64);
        bytes.insert(bytes.end(), { channel, channel, channel });
    }
    bytes.resize(768 + Pal::NUM_CONVERSION_VALUES, 1);
    return bytes;
}

constexpr const char* kCityTxt = R"(
[Area 00]
area_name=Arroyo
world_pos=184,133
start_state=On
size=Medium
townmap_art_idx=156
entrance_0=On,350,275,Arroyo Bridge,-1,-1,3
)";

// One tile whose art_idx is far past the end of any intrface.lst — the shape a modded or partial
// data set takes.
constexpr const char* kWorldmapTxt = R"(
[Data]
terrain_types=Desert:1
terrain_short_names=DES
[Tile Data]
num_horizontal_tiles=1
[Tile 0]
art_idx=999999
0_0=Desert, No_Fill, None, None, None, Desert1
)";

// city.txt, worldmap.txt and color.pal, but an intrface.lst too short for any worldmap art index (the
// tile's 999999, the markers' 336-338), so resolve() throws on each.
void writeArtlessWorldmapData(test::DataTree& fixture) {
    fixture.write("data/city.txt", kCityTxt);
    fixture.write("data/worldmap.txt", kWorldmapTxt);
    fixture.writeBytes("color.pal", palBytes());
    fixture.write("art/intrface/intrface.lst", "iface0.frm\niface1.frm\n");
    fixture.mount();
}

} // namespace

// Both halves of an art lookup throw rather than returning null: ResourceRepository::load on a file
// that is not mounted, FrmResolver::resolve on an LST that is missing or too short. The scene has
// to absorb that, because in the editor load() runs inside a Qt slot, where an escaping exception
// is a terminate() rather than an error message.
TEST_CASE("WorldMapScene reports missing worldmap data instead of throwing", "[worldmap_scene]") {
    test::DataTree fixture{ "geck_worldmap_scene_nodata_test" };
    fixture.write("data/city.txt", kCityTxt);
    fixture.write("data/worldmap.txt", kWorldmapTxt);
    fixture.mount();

    // The text files are there, the art and the palette are not — a data folder added without
    // master.dat next to it. color.pal is what the scene cannot do without.
    std::unique_ptr<worldmap::WorldMapScene> scene;
    REQUIRE_NOTHROW(scene = worldmap::WorldMapScene::load(fixture.mount()));
    CHECK(scene == nullptr);
}

TEST_CASE("WorldMapScene draws what it can and lists the art it could not load", "[worldmap_scene]") {
    test::DataTree fixture{ "geck_worldmap_scene_noart_test" };
    writeArtlessWorldmapData(fixture);

    std::unique_ptr<worldmap::WorldMapScene> scene;
    REQUIRE_NOTHROW(scene = worldmap::WorldMapScene::load(fixture.mount()));
    REQUIRE(scene != nullptr);

    // The canvas is still the size worldmap.txt describes, just black where the tile would be.
    CHECK(scene->width() == WM_TILE_WIDTH);
    CHECK(scene->height() == WM_TILE_HEIGHT);
    CHECK(scene->pixels().size() == static_cast<std::size_t>(WM_TILE_WIDTH) * WM_TILE_HEIGHT * 4);

    // The tile and all three marker sprites are reported, so the UI can say why the map is black
    // and why clicking it does nothing.
    CHECK(scene->missingArt().size() == 4);

    REQUIRE(scene->areas().size() == 1);
    const worldmap::AreaMarker& arroyo = scene->areas().front();
    CHECK(arroyo.name == "Arroyo");
    // No circle art means no hit box at all, rather than a zero-sized one that matches its corner.
    CHECK(arroyo.width == 0);
    CHECK_FALSE(arroyo.contains(arroyo.x, arroyo.y));
    CHECK(scene->areaAt(arroyo.x, arroyo.y) == nullptr);
    CHECK(scene->markerProfile(arroyo.size).empty());
}

// The single most load-bearing fact in drawing the worldmap. city.txt's world_pos is measured from
// the engine's 640x480 interface window, but the map view starts at (WM_VIEW_X, WM_VIEW_Y) inside
// it, so a marker's real place on the worldmap is world_pos minus that. fallout2-ce applies the bias
// in both directions (wmInterfaceRefresh draws at city->x - offset into a buffer whose view begins
// at WM_VIEW_X; wmMatchWorldPosToArea hit-tests worldPos + WM_VIEW_X against city->x).
//
// The engine pins it down: a new game starts the party at the hard-coded world position (173, 122)
// (wmGenDataSetStartWorldPos), and that has to be inside Arroyo. Only the biased rectangle contains
// it, and puts it within two pixels of the circle's centre.
TEST_CASE("WorldMapScene places area markers WM_VIEW_X/Y before their world_pos", "[worldmap_scene]") {
    test::DataTree fixture{ "geck_worldmap_scene_bias_test" };
    writeArtlessWorldmapData(fixture);

    const std::unique_ptr<worldmap::WorldMapScene> scene = worldmap::WorldMapScene::load(fixture.mount());
    REQUIRE(scene != nullptr);
    REQUIRE(scene->areas().size() == 1);
    const worldmap::AreaMarker& arroyo = scene->areas().front();
    CHECK(arroyo.x == 162); // world_pos 184,133
    CHECK(arroyo.y == 112);

    constexpr int MEDIUM_CIRCLE = 25; // art/intrface/wrldspr1.frm
    constexpr int START_X = 173;
    constexpr int START_Y = 122;
    CHECK(std::abs(arroyo.x + MEDIUM_CIRCLE / 2 - START_X) <= 2);
    CHECK(std::abs(arroyo.y + MEDIUM_CIRCLE / 2 - START_Y) <= 2);
}
