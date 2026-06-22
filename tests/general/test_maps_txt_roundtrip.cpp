#include <catch2/catch_test_macros.hpp>

#include "format/maps/MapsTxtDocument.h"
#include "reader/maps/MapsTxtDocumentReader.h"
#include "writer/maps/MapsTxtSerializer.h"
#include "writer/maps/MapsTxtValidator.h"

#include <algorithm>
#include <string>

using namespace geck;

namespace {

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

TEST_CASE("MapsTxtDocument round-trips an LF file byte-for-byte (no trailing newline)", "[maps_roundtrip]") {
    const MapsTxtDocument doc = parseMapsTxtDocument(std::string{ kMapsTxtLf });

    REQUIRE(doc.sections.size() == 2);
    CHECK(doc.sections[0].index == 0);
    CHECK(doc.sections[1].index == 1);
    CHECK_FALSE(doc.finalNewline); // last line has no newline

    CHECK(writer::serializeMapsTxt(doc) == std::string{ kMapsTxtLf });
}

TEST_CASE("MapsTxtDocument normalizes CRLF to LF while preserving all data", "[maps_roundtrip]") {
    std::string crlf;
    for (const char* p = kMapsTxtLf; *p != '\0'; ++p) {
        if (*p == '\n') {
            crlf += '\r';
        }
        crlf += *p;
    }

    const MapsTxtDocument doc = parseMapsTxtDocument(crlf);
    // Round-trips to the LF form: data identical, line endings normalized.
    CHECK(writer::serializeMapsTxt(doc) == std::string{ kMapsTxtLf });
}

TEST_CASE("MapsTxtDocument edits one field and leaves every other byte untouched", "[maps_roundtrip]") {
    MapsTxtDocument doc = parseMapsTxtDocument(std::string{ kMapsTxtLf });

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

TEST_CASE("validateMapsTxt enforces the engine's hard rules", "[maps_roundtrip]") {
    using writer::MapsTxtIssue;

    SECTION("a gapless, well-formed registry has no errors") {
        const auto doc = parseMapsTxtDocument(std::string{ kMapsTxtLf });
        const auto issues = writer::validateMapsTxt(doc);
        CHECK_FALSE(writer::hasErrors(issues));
    }

    SECTION("a gap in the [Map N] run is an error") {
        const auto doc = parseMapsTxtDocument(std::string{
            "[Map 0]\nlookup_name=A\nmap_name=a\n[Map 2]\nlookup_name=C\nmap_name=c" });
        const auto issues = writer::validateMapsTxt(doc);
        CHECK(writer::hasErrors(issues)); // missing [Map 1]
    }

    SECTION("missing lookup_name / map_name is an error") {
        const auto doc = parseMapsTxtDocument(std::string{ "[Map 0]\nmap_name=a" });
        CHECK(writer::hasErrors(writer::validateMapsTxt(doc)));
    }

    SECTION("an unknown key is reported but not an error") {
        const auto doc = parseMapsTxtDocument(std::string{
            "[Map 0]\nlookup_name=A\nmap_name=a\nmystery_key=42" });
        const auto issues = writer::validateMapsTxt(doc);
        CHECK_FALSE(writer::hasErrors(issues));
        CHECK(std::any_of(issues.begin(), issues.end(),
            [](const MapsTxtIssue& i) { return i.severity == MapsTxtIssue::Severity::Info; }));
    }
}

TEST_CASE("addSection / removeSection edit the registry, keeping it serialisable", "[maps_roundtrip]") {
    auto doc = parseMapsTxtDocument(std::string{
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
