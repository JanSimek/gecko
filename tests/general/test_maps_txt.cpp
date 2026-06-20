#include <catch2/catch_test_macros.hpp>

#include "format/maps/MapsTxt.h"
#include "reader/maps/MapsTxtReader.h"

using namespace geck;

namespace {
// A trimmed-down maps.txt in the real format: two `[Map NNN]` sections with the engine's keys
// (including the irrelevant ones that must be ignored), a comment, and a non-map section that must
// be dropped. map_name is upper-cased here to prove the reader lowercases it like the engine.
constexpr const char* kMapsTxt = R"(; Fallout 2 maps
[Map 000]
lookup_name=Desert Encounter
map_name=DESERT1.MAP
music=07Desert
ambient_sfx=brmnwind:35, coyote:10
saved=No
dead_bodies_age=Yes
pipboy_active=Yes
automap=No
random_start_point_0=elevation:0, tile_num:20100

[Map 003]
lookup_name=Temple
map_name=ARTEMPLE.MAP
saved=Yes
automap=Yes

[NotAMap]
map_name=ignored.map
)";
} // namespace

TEST_CASE("parseMapsTxt reads map sections keyed by their [Map NNN] index", "[maps]") {
    const MapsTxt maps = parseMapsTxt(std::string{ kMapsTxt });

    // Two map sections; the trailing non-map section is dropped.
    CHECK(maps.maps.size() == 2);

    const MapInfo* desert = maps.find(0);
    REQUIRE(desert != nullptr);
    CHECK(desert->lookupName == "Desert Encounter");
    CHECK(desert->mapName == "desert1.map"); // lowercased like the engine
    CHECK(desert->music == "07Desert");
    CHECK(desert->saved == false);
    CHECK(desert->deadBodiesAge == true);
    CHECK(desert->pipboyActive == true);
    CHECK(desert->automap == false);
    REQUIRE(desert->ambientSfx.size() == 2);
    CHECK(desert->ambientSfx[0] == std::pair<std::string, int>{ "brmnwind", 35 });
    CHECK(desert->ambientSfx[1] == std::pair<std::string, int>{ "coyote", 10 });

    const MapInfo* temple = maps.find(3);
    REQUIRE(temple != nullptr);
    CHECK(temple->index == 3);
    CHECK(temple->mapName == "artemple.map");
    CHECK(temple->lookupName == "Temple");
    CHECK(temple->saved == true);
    CHECK(temple->automap == true);
    CHECK(temple->deadBodiesAge == false); // default when the key is absent

    // An index with no section resolves to nullptr (caller falls back to the bare number).
    CHECK(maps.find(1) == nullptr);
    CHECK(maps.find(999) == nullptr);
}

TEST_CASE("parseMapsTxt tolerates empty / section-less input", "[maps]") {
    CHECK(parseMapsTxt(std::string{}).maps.empty());
    CHECK(parseMapsTxt(std::string{ "map_name=stray.map\n" }).maps.empty()); // keys before any [section]
}
