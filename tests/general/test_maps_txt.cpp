#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "format/maps/MapsTxt.h"
#include "reader/maps/MapsTxtReader.h"
#include "writer/maps/MapsTxtSerializer.h"
#include "writer/maps/MapsTxtValidator.h"

#include "support/Fixtures.h"

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

// A feature-rich slice mirroring real maps.txt: a preamble comment + blank, a zero-padded [Map 000]
// with an inline comment, a multi-value key, repeated keys, and a commented-out key, then [Map 001].
// LF line endings, and NO trailing newline on the last line (as the real file ships).
constexpr const char* kMapsTxtLf = "; Map datafile\n"
                                   "\n"
                                   "[Map 000]\n"
                                   "lookup_name=Desert Encounter 1\n"
                                   "map_name=desert1\n"
                                   "saved=No  ; Random encounter maps aren't saved\n"
                                   "can_rest_here=No,No,No  ; All 3 elevations\n"
                                   "random_start_point_0=elev:0, tile_num:19086\n"
                                   "random_start_point_1=elev:0, tile_num:17302\n"
                                   ";music=07desert\n"
                                   "\n"
                                   "[Map 001]\n"
                                   "lookup_name=Klamath\n"
                                   "map_name=klamath";

} // namespace

TEST_CASE("MapsTxt resolves maps by index, filename, and lookup_name", "[maps_txt]") {
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

TEST_CASE("MapsTxt tolerates empty / section-less input", "[maps_txt]") {
    CHECK_FALSE(parseMapsTxt(std::string{}).find(0).has_value());
    const auto doc = parseMapsTxt(std::string{ "map_name=stray.map\n" }); // keys before any [section]
    CHECK(doc.sections.empty());
    CHECK_FALSE(doc.findByName("stray.map").has_value());
}

TEST_CASE("MapsTxt round-trips an LF file byte-for-byte (no trailing newline)", "[maps_txt][roundtrip]") {
    const MapsTxt doc = parseMapsTxt(std::string{ kMapsTxtLf });

    REQUIRE(doc.sections.size() == 2);
    CHECK(doc.sections[0].index == 0);
    CHECK(doc.sections[1].index == 1);
    CHECK_FALSE(doc.finalNewline); // last line has no newline

    CHECK(writer::serializeMapsTxt(doc) == std::string{ kMapsTxtLf });
}

TEST_CASE("MapsTxt normalizes CRLF to LF while preserving all data", "[maps_txt][roundtrip]") {
    const std::string crlf = geck::test::toCrlf(kMapsTxtLf);

    const MapsTxt doc = parseMapsTxt(crlf);
    // Round-trips to the LF form: data identical, line endings normalized.
    CHECK(writer::serializeMapsTxt(doc) == std::string{ kMapsTxtLf });
}

TEST_CASE("MapsTxt edits one field and leaves every other byte untouched", "[maps_txt]") {
    MapsTxt doc = parseMapsTxt(std::string{ kMapsTxtLf });

    REQUIRE(writer::findField(doc, 0, "lookup_name").value_or("") == "Desert Encounter 1");
    REQUIRE(writer::setField(doc, 0, "lookup_name", "New Arroyo"));
    CHECK(writer::findField(doc, 0, "lookup_name").value_or("") == "New Arroyo");

    // An edit to a line that carries an inline comment must keep the comment.
    REQUIRE(writer::setField(doc, 0, "saved", "Yes"));

    const std::string out = writer::serializeMapsTxt(doc);
    CHECK(out.find("lookup_name=New Arroyo\n") != std::string::npos);
    CHECK(out.find("lookup_name=Desert Encounter 1") == std::string::npos);
    CHECK(out.find("saved=Yes  ; Random encounter maps aren't saved\n") != std::string::npos); // comment kept
    CHECK(out.find(";music=07desert\n") != std::string::npos);                                 // commented-out key kept
    CHECK(out.find("random_start_point_1=elev:0, tile_num:17302\n") != std::string::npos);     // repeated key kept
    CHECK(out.find("lookup_name=Klamath") != std::string::npos);                               // other section kept
    CHECK_FALSE(out.empty());

    // Editing a missing key/section reports failure rather than inventing one.
    CHECK_FALSE(writer::setField(doc, 0, "no_such_key", "x"));
    CHECK_FALSE(writer::setField(doc, 99, "lookup_name", "x"));
}

TEST_CASE("validateMapsTxt enforces the engine's hard rules", "[maps_txt]") {
    using writer::MapsTxtIssue;

    SECTION("a gapless, well-formed registry has no errors") {
        const auto doc = parseMapsTxt(std::string{ kMapsTxtLf });
        const auto issues = writer::validateMapsTxt(doc);
        CHECK_FALSE(writer::hasErrors(issues));
    }

    SECTION("a gap in the [Map N] run is an error") {
        const auto doc = parseMapsTxt(std::string{
            "[Map 0]\nlookup_name=A\nmap_name=a\n[Map 2]\nlookup_name=C\nmap_name=c" });
        const auto issues = writer::validateMapsTxt(doc);
        CHECK(writer::hasErrors(issues)); // missing [Map 1]
    }

    SECTION("missing lookup_name / map_name is an error") {
        const auto doc = parseMapsTxt(std::string{ "[Map 0]\nmap_name=a" });
        CHECK(writer::hasErrors(writer::validateMapsTxt(doc)));
    }

    SECTION("an unknown key is reported but not an error") {
        const auto doc = parseMapsTxt(std::string{
            "[Map 0]\nlookup_name=A\nmap_name=a\nmystery_key=42" });
        const auto issues = writer::validateMapsTxt(doc);
        CHECK_FALSE(writer::hasErrors(issues));
        CHECK(std::any_of(issues.begin(), issues.end(),
            [](const MapsTxtIssue& i) { return i.severity == MapsTxtIssue::Severity::Info; }));
    }
}

TEST_CASE("addSection / removeSection edit the registry, keeping it serialisable", "[maps_txt]") {
    auto doc = parseMapsTxt(std::string{
        "[Map 0]\nlookup_name=A\nmap_name=a\n[Map 1]\nlookup_name=B\nmap_name=b" });

    REQUIRE(writer::addSection(doc, 2, "New Town", "newtown"));
    CHECK_FALSE(writer::addSection(doc, 2, "dup", "dup")); // already exists
    CHECK(writer::findField(doc, 2, "lookup_name").value_or("") == "New Town");
    CHECK(writer::findField(doc, 2, "map_name").value_or("") == "newtown");
    {
        const std::string out = writer::serializeMapsTxt(doc);
        CHECK(out.find("[Map 002]\n") != std::string::npos);
        CHECK(out.find("lookup_name=New Town\n") != std::string::npos);
        CHECK_FALSE(writer::hasErrors(writer::validateMapsTxt(doc))); // 0,1,2 still gapless
    }

    REQUIRE(writer::removeSection(doc, 2)); // removing the last keeps it gapless
    CHECK_FALSE(writer::removeSection(doc, 5));
    CHECK(doc.section(2) == nullptr);
    CHECK_FALSE(writer::hasErrors(writer::validateMapsTxt(doc)));
}
