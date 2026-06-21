#include <catch2/catch_test_macros.hpp>

#include "format/maps/MapsTxt.h"
#include "reader/maps/MapsTxtReader.h"
#include "writer/maps/MapRegistryWriter.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#ifndef GECK_TEST_TMP_DIR
#error "GECK_TEST_TMP_DIR must be defined for this test target (see tests/CMakeLists.txt)"
#endif

using namespace geck;
namespace fs = std::filesystem;

namespace {

fs::path writeTemp(const std::string& name, const std::string& content) {
    const fs::path path = fs::path{ GECK_TEST_TMP_DIR } / name;
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return path;
}

std::string readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Real maps.txt shape: CRLF, a `;` header comment, zero-padded [Map NNN], an inline-comment line, and
// keys the reader doesn't model (can_rest_here) — all of which the patch must leave byte-identical.
constexpr const char* kMapsTxt = "; a header comment\r\n"
                                 "\r\n"
                                 "[Map 000]\r\n"
                                 "lookup_name=Desert Encounter 1\r\n"
                                 "map_name=desert1\r\n"
                                 "can_rest_here=No,No,No  ; all 3 elevations\r\n"
                                 "\r\n"
                                 "[Map 003]\r\n"
                                 "lookup_name=Klamath\r\n"
                                 "map_name=klamath\r\n";

// Real map.msg shape: CRLF, `#` comments, {id}{audio}{text} with an empty and a non-empty audio field.
constexpr const char* kMapMsg = "#\r\n"
                                "# Map Names\r\n"
                                "{200}{}{Desert}\r\n"
                                "{201}{}{}\r\n"
                                "{209}{snd}{Klamath}\r\n";

} // namespace

TEST_CASE("updateLookupName patches one maps.txt field, preserving CRLF/comments/unmodeled keys", "[mapregistry]") {
    const fs::path path = writeTemp("mrw_maps.txt", kMapsTxt);

    REQUIRE(writer::updateLookupName(path, 0, "New Arroyo"));

    const std::string out = readAll(path);
    CHECK(out.find("lookup_name=New Arroyo\r\n") != std::string::npos);
    CHECK(out.find("lookup_name=Desert Encounter 1") == std::string::npos); // old value gone
    // Everything else byte-identical: header comment, inline-comment line, unmodeled keys, other section.
    CHECK(out.find("; a header comment\r\n") != std::string::npos);
    CHECK(out.find("can_rest_here=No,No,No  ; all 3 elevations\r\n") != std::string::npos);
    CHECK(out.find("map_name=desert1\r\n") != std::string::npos);
    CHECK(out.find("lookup_name=Klamath\r\n") != std::string::npos); // [Map 003] untouched

    const MapsTxt parsed = parseMapsTxt(out);
    const MapInfo* map0 = parsed.find(0);
    REQUIRE(map0 != nullptr);
    CHECK(map0->lookupName == "New Arroyo");
}

TEST_CASE("updateLookupName matches zero-padded sections and refuses missing ones", "[mapregistry]") {
    const fs::path path = writeTemp("mrw_maps2.txt", kMapsTxt);
    REQUIRE(writer::updateLookupName(path, 3, "Klamath Region")); // [Map 003] via stoi tolerance
    CHECK(readAll(path).find("lookup_name=Klamath Region\r\n") != std::string::npos);

    const fs::path missing = writeTemp("mrw_maps3.txt", kMapsTxt);
    CHECK_FALSE(writer::updateLookupName(missing, 99, "Nope")); // no [Map 099]
    CHECK(readAll(missing) == std::string{ kMapsTxt });         // left untouched (byte-for-byte)
}

TEST_CASE("updateDisplayName patches map.msg, preserves audio, appends when missing", "[mapregistry]") {
    const fs::path path = writeTemp("mrw_map.msg", kMapMsg);

    REQUIRE(writer::updateDisplayName(path, 0, 0, "Wasteland"));   // id 200
    REQUIRE(writer::updateDisplayName(path, 3, 0, "New Klamath")); // id 209 (3*3+0+200), audio kept
    REQUIRE(writer::updateDisplayName(path, 50, 0, "Brand New"));  // id 350, not present -> append

    const std::string out = readAll(path);
    CHECK(out.find("{200}{}{Wasteland}\r\n") != std::string::npos);
    CHECK(out.find("{209}{snd}{New Klamath}\r\n") != std::string::npos); // audio field preserved
    CHECK(out.find("{350}{}{Brand New}\r\n") != std::string::npos);      // appended at EOF
    CHECK(out.find("# Map Names\r\n") != std::string::npos);             // comments preserved
    CHECK(out.find("{200}{}{Desert}") == std::string::npos);             // old text gone

    CHECK_FALSE(writer::updateDisplayName(path, 0, 5, "x"));  // elevation out of range
    CHECK_FALSE(writer::updateDisplayName(path, -1, 0, "x")); // map not in the registry
}
