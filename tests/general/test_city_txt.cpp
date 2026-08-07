#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

#include "format/city/CityTxt.h"
#include "format/worldmap/WorldmapTxt.h"
#include "reader/city/CityTxtReader.h"

using namespace geck;

namespace {
// A trimmed-down city.txt in the real format: inline ';' comments on section headers and values
// (which the reader must strip), world_pos, the yes/no start_state, and entrance lines whose map
// field is a lookup_name with a space (so it survives the comma split).
constexpr const char* kCityTxt = R"(; City datafile for worldmap
[Area 00]                ; Arroyo
area_name=Arroyo
world_pos=184,133        ; Absolute position ; SAVED
start_state=On           ; Starting state
size=Medium              ; Size of circle
townmap_art_idx=156      ; Fid num index for the townmap art
townmap_label_art_idx=370
entrance_0=On,350,275,Arroyo Bridge,-1,-1,3  ; Etc.
entrance_1=On,280,175,Arroyo Village,-1,-1,0
entrance_2=Off,-1,-1,Arroyo Caves,-1,-1,0

[Area 01]                ; The Den
area_name=The Den
world_pos=308,213
start_state=Off
lock_state=On
size=Large
townmap_art_idx=160
entrance_0=On,300,300,Den,-1,-1,2
)";
} // namespace

TEST_CASE("parseCityTxt reads worldmap areas, positions and entrances", "[city]") {
    const CityTxt city = parseCityTxt(std::string{ kCityTxt });

    CHECK(city.areas.size() == 2);

    const CityArea* arroyo = city.find(0);
    REQUIRE(arroyo != nullptr);
    CHECK(arroyo->name == "Arroyo");
    CHECK(arroyo->worldX == 184); // world_pos x, inline comment stripped
    CHECK(arroyo->worldY == 133);
    CHECK(arroyo->startOn == true);
    CHECK(arroyo->locked == false); // no lock_state key at all
    CHECK(arroyo->size == "medium");
    CHECK(cityAreaSize(arroyo->size) == CityAreaSize::Medium);
    CHECK(arroyo->townMapArtIndex == 156);
    CHECK(arroyo->townMapLabelArtIndex == 370);
    REQUIRE(arroyo->entrances.size() == 3);
    CHECK(arroyo->entrances[0].on == true);
    CHECK(arroyo->entrances[0].map == "Arroyo Bridge"); // lookup_name with a space survives the split
    CHECK(arroyo->entrances[0].orientation == 3);
    CHECK(arroyo->entrances[2].on == false);
    CHECK(arroyo->entrances[2].map == "Arroyo Caves");

    const CityArea* den = city.find(1);
    REQUIRE(den != nullptr);
    CHECK(den->name == "The Den");
    CHECK(den->worldX == 308);
    CHECK(den->size == "large");
    CHECK(den->locked == true);
    CHECK(den->townMapLabelArtIndex == -1); // key absent
    REQUIRE(den->entrances.size() == 1);
    CHECK(den->entrances[0].map == "Den");

    CHECK(city.find(99) == nullptr);
}

// The single most load-bearing fact in drawing the worldmap. city.txt's world_pos is measured from
// the engine's 640x480 interface window, but the map view starts at (WM_VIEW_X, WM_VIEW_Y) inside
// it, so a marker's real place on the worldmap is world_pos minus that. fallout2-ce applies the bias
// in both directions (wmInterfaceRefresh draws at city->x - offset into a buffer whose view begins
// at WM_VIEW_X; wmMatchWorldPosToArea hit-tests worldPos + WM_VIEW_X against city->x).
//
// The engine pins it down: a new game starts the party at the hard-coded world position (173, 122)
// (wmGenDataSetStartWorldPos), and that has to be inside Arroyo. Only the biased rectangle contains
// it — and puts it within two pixels of the circle's centre.
TEST_CASE("worldmap markers sit WM_VIEW_X/Y before their world_pos", "[city][worldmap]") {
    const CityTxt city = parseCityTxt(std::string{ kCityTxt });
    const CityArea* arroyo = city.find(0);
    REQUIRE(arroyo != nullptr);

    constexpr int MEDIUM_CIRCLE = 25; // art/intrface/wrldspr1.frm
    constexpr int START_X = 173;
    constexpr int START_Y = 122;

    const int left = arroyo->worldX - WM_VIEW_X;
    const int top = arroyo->worldY - WM_VIEW_Y;
    CHECK(left == 162);
    CHECK(top == 112);

    CHECK(START_X >= left);
    CHECK(START_X <= left + MEDIUM_CIRCLE);
    CHECK(START_Y >= top);
    CHECK(START_Y <= top + MEDIUM_CIRCLE);

    // Within a couple of pixels of dead centre, which is what makes the bias more than a coincidence.
    CHECK(std::abs(left + MEDIUM_CIRCLE / 2 - START_X) <= 2);
    CHECK(std::abs(top + MEDIUM_CIRCLE / 2 - START_Y) <= 2);

    // Without the bias the party would start outside Arroyo's circle entirely.
    CHECK(START_X < arroyo->worldX);
}

TEST_CASE("parseCityTxt tolerates empty input", "[city]") {
    CHECK(parseCityTxt(std::string{}).areas.empty());
}
