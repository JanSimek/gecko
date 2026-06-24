#include <catch2/catch_test_macros.hpp>

#include "format/gam/Gam.h"
#include "reader/gam/GamReader.h"
#include "writer/gam/GamSerializer.h"
#include "writer/gam/GamValidator.h"

#include <string>

using namespace geck;

namespace {

// A feature-rich slice mirroring a real .gam: a leading comment + blank, a GAME_GLOBAL_VARS section, the
// MAP_GLOBAL_VARS section with an aligned `NAME := -1;`, an aligned `NAME := 0; // note` with a trailing
// inline comment, and a blank line between sections. LF line endings, with a trailing newline.
constexpr const char* kGamLf = "// Test map variables\n"
                               "\n"
                               "GAME_GLOBAL_VARS:\n"
                               "GVAR_TEST       := 5;\n"
                               "\n"
                               "MAP_GLOBAL_VARS:\n"
                               "MVAR_FIRST      := -1;\n"
                               "MVAR_SECOND     := 0;   // a note\n";

// Parse the shared sample. Shared by the round-trip and validator tests so the fixture lives in one place.
Gam sampleGam() {
    return GamReader::parse(std::string{ kGamLf });
}

} // namespace

TEST_CASE("Gam round-trips a .gam byte-for-byte", "[gam_roundtrip]") {
    const Gam gam = sampleGam();

    // The MAP_GLOBAL_VARS variables are read in file order with their values.
    const auto mvars = gam.mapGlobalVars();
    REQUIRE(mvars.size() == 2);
    CHECK(mvars[0].first == "MVAR_FIRST");
    CHECK(mvars[0].second == -1);
    CHECK(mvars[1].first == "MVAR_SECOND");
    CHECK(mvars[1].second == 0);

    // The GAME_GLOBAL_VARS dictionary is exposed too (the CLI gvar lookup uses these accessors).
    REQUIRE(gam.gvarCount() == 1);
    CHECK(gam.gvarKey(0) == "GVAR_TEST");
    CHECK(gam.gvarValue(0) == 5);
    CHECK(gam.gvarValue("GVAR_TEST") == 5);

    // An unedited document serialises identically.
    CHECK(writer::serializeGam(gam) == std::string{ kGamLf });
}

TEST_CASE("Gam normalizes CRLF to LF while preserving all data", "[gam_roundtrip]") {
    std::string crlf;
    for (const char* p = kGamLf; *p != '\0'; ++p) {
        if (*p == '\n') {
            crlf += '\r';
        }
        crlf += *p;
    }

    const Gam gam = GamReader::parse(crlf);
    CHECK(writer::serializeGam(gam) == std::string{ kGamLf });
}

TEST_CASE("Gam::setMapGlobalVar rewrites only the value substring", "[gam_roundtrip]") {
    Gam gam = sampleGam();

    // Editing the first MAP_GLOBAL_VARS value changes only its integer; name, alignment, and `:=` spacing
    // are preserved.
    REQUIRE(gam.setMapGlobalVar(0, 42));
    const std::string out = writer::serializeGam(gam);
    CHECK(out.find("MVAR_FIRST      := 42;\n") != std::string::npos); // alignment + spacing preserved
    CHECK(out.find("MVAR_FIRST      := -1;") == std::string::npos);   // old value gone

    // The second variable's trailing inline comment survives an edit, including a longer (multi-digit)
    // value that grows the substring.
    REQUIRE(gam.setMapGlobalVar(1, 1000));
    const std::string out2 = writer::serializeGam(gam);
    CHECK(out2.find("MVAR_SECOND     := 1000;   // a note\n") != std::string::npos);

    // Untouched lines are byte-identical.
    CHECK(out2.find("// Test map variables\n") != std::string::npos);
    CHECK(out2.find("GAME_GLOBAL_VARS:\n") != std::string::npos);
    CHECK(out2.find("GVAR_TEST       := 5;\n") != std::string::npos); // GVAR section not touched
    CHECK(out2.find("MAP_GLOBAL_VARS:\n") != std::string::npos);

    // An out-of-range index is reported, not applied.
    CHECK_FALSE(gam.setMapGlobalVar(2, 7));

    // A re-read of the edited bytes sees the new values.
    const Gam reread = GamReader::parse(out2);
    const auto mvars = reread.mapGlobalVars();
    REQUIRE(mvars.size() == 2);
    CHECK(mvars[0].second == 42);
    CHECK(mvars[1].second == 1000);
}

TEST_CASE("Gam::setMapGlobalVar rebuilds the line as it grows then shrinks", "[gam_roundtrip]") {
    Gam gam = sampleGam();

    // The line with a trailing comment: grow the value (1 digit -> 4 digits). Name, `:=` alignment, the
    // `;`, and the trailing `// comment` are all preserved by the parts-based rebuild.
    REQUIRE(gam.setMapGlobalVar(1, 1000));
    {
        const std::string out = writer::serializeGam(gam);
        CHECK(out.find("MVAR_SECOND     := 1000;   // a note\n") != std::string::npos);
    }

    // Now shrink it and flip the sign (4 digits -> a negative). There is no stored offset to drift, so a
    // second edit on the same line rebuilds cleanly from the same prefix/suffix.
    REQUIRE(gam.setMapGlobalVar(1, -7));
    {
        const std::string out = writer::serializeGam(gam);
        CHECK(out.find("MVAR_SECOND     := -7;   // a note\n") != std::string::npos);
        CHECK(out.find("1000") == std::string::npos); // the intermediate value is gone

        // A re-read sees the latest value, and the name/alignment survived both edits.
        const Gam reread = GamReader::parse(out);
        const auto mvars = reread.mapGlobalVars();
        REQUIRE(mvars.size() == 2);
        CHECK(mvars[1].first == "MVAR_SECOND");
        CHECK(mvars[1].second == -7);
    }
}

TEST_CASE("validateGam accepts a normal parsed and edited document", "[gam_validator]") {
    Gam gam = sampleGam();
    REQUIRE_FALSE(writer::hasErrors(writer::validateGam(gam)));

    // An edit through the supported API keeps it valid.
    REQUIRE(gam.setMapGlobalVar(0, 12345));
    CHECK_FALSE(writer::hasErrors(writer::validateGam(gam)));
}

TEST_CASE("validateGam catches a corrupted variable line", "[gam_validator]") {
    Gam gam = sampleGam();

    // Mangle a MAP_GLOBAL_VARS line's raw so it no longer serialises to the parsed (name, value): the raw
    // now carries a different value than `value`/the parsed parts. A serialise + re-read will not
    // reproduce the document's mapGlobalVars() -> the round-trip integrity check must flag an Error.
    for (GamLine& line : gam.lines) {
        if (line.kind == GamLine::Kind::Variable && line.section == GamLine::Section::MapGlobalVars
            && line.name == "MVAR_FIRST") {
            line.raw = "MVAR_FIRST      := 999;"; // raw value (999) disagrees with line.value (-1)
            break;
        }
    }

    CHECK(writer::hasErrors(writer::validateGam(gam)));
}

TEST_CASE("validateGam flags a value span that breaks the engine shape", "[gam_validator]") {
    Gam gam = sampleGam();

    // A line whose raw no longer matches `NAME := value;` (here the `;` is gone) must be an Error: the
    // engine would fail to parse it.
    for (GamLine& line : gam.lines) {
        if (line.kind == GamLine::Kind::Variable && line.section == GamLine::Section::MapGlobalVars) {
            line.raw = "MVAR_FIRST      := -1"; // dropped the trailing ';'
            break;
        }
    }

    CHECK(writer::hasErrors(writer::validateGam(gam)));
}
