#include <catch2/catch_test_macros.hpp>

#include "format/city/CityTxt.h"
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
townmap_art_idx=156
entrance_0=On,350,275,Arroyo Bridge,-1,-1,3  ; Etc.
entrance_1=On,280,175,Arroyo Village,-1,-1,0
entrance_2=Off,-1,-1,Arroyo Caves,-1,-1,0

[Area 01]                ; The Den
area_name=The Den
world_pos=308,213
start_state=Off
size=Large
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
    CHECK(arroyo->size == "medium");
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
    REQUIRE(den->entrances.size() == 1);
    CHECK(den->entrances[0].map == "Den");

    CHECK(city.find(99) == nullptr);
}

TEST_CASE("parseCityTxt tolerates empty input", "[city]") {
    CHECK(parseCityTxt(std::string{}).areas.empty());
}
