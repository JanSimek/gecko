#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "editor/worldmap/WorldMapScene.h"
#include "format/pal/Pal.h"
#include "resource/GameResources.h"

namespace fs = std::filesystem;
using namespace geck;

namespace {

void writeFile(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << contents;
}

// A structurally valid color.pal: 256 RGB triplets in the palette's 6-bit range, then the
// 32,768-entry RGB555 -> index conversion table. The colours themselves do not matter here; what
// matters is that PalReader accepts the file, so the scene gets as far as the art it cannot find.
void writePal(const fs::path& path) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(768 + Pal::NUM_CONVERSION_VALUES);
    for (int index = 0; index < 256; ++index) {
        const auto channel = static_cast<std::uint8_t>(index % 64);
        bytes.insert(bytes.end(), { channel, channel, channel });
    }
    bytes.resize(768 + Pal::NUM_CONVERSION_VALUES, 1);

    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
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

// A data path holding whatever the test writes into it, mounted into its own GameResources.
struct FixtureResources {
    fs::path root;
    resource::GameResources resources;

    explicit FixtureResources(const std::string& name)
        : root(fs::temp_directory_path() / name) { // NOSONAR: throwaway test dir, not security-sensitive
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root, ec);
    }

    ~FixtureResources() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    FixtureResources(const FixtureResources&) = delete;
    FixtureResources& operator=(const FixtureResources&) = delete;

    void mount() { resources.files().addDataPath(root.string()); }
};

} // namespace

// Both halves of an art lookup throw rather than returning null: ResourceRepository::load on a file
// that is not mounted, FrmResolver::resolve on an LST that is missing or too short. The scene has
// to absorb that, because in the editor load() runs inside a Qt slot, where an escaping exception
// is a terminate() rather than an error message.
TEST_CASE("WorldMapScene reports missing worldmap data instead of throwing", "[worldmap][scene]") {
    FixtureResources fixture("geck_worldmap_scene_nodata_test");
    writeFile(fixture.root / "data/city.txt", kCityTxt);
    writeFile(fixture.root / "data/worldmap.txt", kWorldmapTxt);
    fixture.mount();

    // The text files are there, the art and the palette are not — a data folder added without
    // master.dat next to it. color.pal is what the scene cannot do without.
    std::unique_ptr<worldmap::WorldMapScene> scene;
    REQUIRE_NOTHROW(scene = worldmap::WorldMapScene::load(fixture.resources));
    CHECK(scene == nullptr);
}

TEST_CASE("WorldMapScene draws what it can and lists the art it could not load", "[worldmap][scene]") {
    FixtureResources fixture("geck_worldmap_scene_noart_test");
    writeFile(fixture.root / "data/city.txt", kCityTxt);
    writeFile(fixture.root / "data/worldmap.txt", kWorldmapTxt);
    writePal(fixture.root / "color.pal");
    // Two entries, so every worldmap art index (the tile's 999999, the markers' 336-338) is past
    // the end of the list and resolve() throws on each.
    writeFile(fixture.root / "art/intrface/intrface.lst", "iface0.frm\niface1.frm\n");
    fixture.mount();

    std::unique_ptr<worldmap::WorldMapScene> scene;
    REQUIRE_NOTHROW(scene = worldmap::WorldMapScene::load(fixture.resources));
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
