#include <catch2/catch_test_macros.hpp>

#include "format/maps/MapsTxt.h"
#include "reader/maps/MapsTxtReader.h"

using namespace geck;

namespace {
// A trimmed-down maps.txt in the real format: two `[Map NNN]` sections with the engine's keys
// (including the unmodelled ones, which must not disturb the typed queries), a comment, and a non-map
// section that the queries must ignore. map_name is upper-cased to prove it is lowercased like the engine.
constexpr const char* kMapsTxt = R"(; Fallout 2 maps
[Map 000]
lookup_name=Desert Encounter
map_name=DESERT1.MAP
music=07Desert
ambient_sfx=brmnwind:35, coyote:10
saved=No
random_start_point_0=elevation:0, tile_num:20100

[Map 003]
lookup_name=Temple
map_name=artemple
saved=Yes
automap=Yes

[NotAMap]
map_name=ignored.map
)";
} // namespace

TEST_CASE("MapsTxt resolves maps by index, filename, and lookup_name", "[maps]") {
    const MapsTxt doc = parseMapsTxt(std::string{ kMapsTxt });

    // By [Map NNN] index, deriving the typed view (unmodelled keys like music/ambient_sfx are ignored).
    const auto desert = doc.find(0);
    REQUIRE(desert.has_value());
    CHECK(desert->index == 0);
    CHECK(desert->lookupName == "Desert Encounter");
    CHECK(desert->mapName == "desert1.map"); // DESERT1.MAP lowercased like the engine

    const auto temple = doc.find(3);
    REQUIRE(temple.has_value());
    CHECK(temple->index == 3);
    CHECK(temple->lookupName == "Temple");
    CHECK(temple->mapName == "artemple.map"); // bare "artemple" normalized to a loadable filename

    CHECK_FALSE(doc.find(1).has_value()); // a gap resolves to nothing
    CHECK_FALSE(doc.find(999).has_value());

    // By .map filename — any case, extension optional; the [NotAMap] section is ignored.
    REQUIRE(doc.findByName("ARTEMPLE.MAP").has_value());
    CHECK(doc.findByName("ARTEMPLE.MAP")->index == 3);
    CHECK(doc.findByName("desert1.map")->index == 0);
    CHECK(doc.findByName("artemple")->index == 3); // extension optional
    CHECK_FALSE(doc.findByName("nosuch.map").has_value());
    CHECK_FALSE(doc.findByName("ignored.map").has_value()); // non-map section not resolvable

    // By lookup_name, case-insensitive (the join from city.txt entrances to .map files).
    REQUIRE(doc.findByLookupName("Temple").has_value());
    CHECK(doc.findByLookupName("Temple")->mapName == "artemple.map");
    CHECK(doc.findByLookupName("temple")->index == 3); // case-insensitive
    CHECK(doc.findByLookupName("desert encounter")->index == 0);
    CHECK_FALSE(doc.findByLookupName("nope").has_value());
}

TEST_CASE("MapsTxt tolerates empty / section-less input", "[maps]") {
    CHECK_FALSE(parseMapsTxt(std::string{}).find(0).has_value());
    const auto doc = parseMapsTxt(std::string{ "map_name=stray.map\n" }); // keys before any [section]
    CHECK(doc.sections.empty());
    CHECK_FALSE(doc.findByName("stray.map").has_value());
}
