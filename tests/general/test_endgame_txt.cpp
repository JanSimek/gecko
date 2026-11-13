#include <catch2/catch_test_macros.hpp>

#include "format/endgame/EndgameTxt.h"
#include "format/endgame/EndDeathTxt.h"
#include "reader/endgame/EndDeathTxtReader.h"
#include "reader/endgame/EndgameTxtReader.h"

using namespace geck;

namespace {
// endgame.txt rows: comma-separated, an inline '#' comment, the optional 5th 'direction' field, and a
// too-short line that must be skipped.
constexpr const char* kEndgameTxt = R"(# gvar, value, art, narrator [, direction]
408, 1, 440, nar_ar1
700, 1, 471, nar_eldr #471 - killap
40, 1, 327, nar_10, 1
bad, line
)";
} // namespace

TEST_CASE("parseEndgameTxt reads ending slides, skipping comments and short lines", "[endgame_txt]") {
    const EndgameTxt endgame = parseEndgameTxt(std::string{ kEndgameTxt });

    REQUIRE(endgame.endings.size() == 3);

    CHECK(endgame.endings[0].gvar == 408);
    CHECK(endgame.endings[0].value == 1);
    CHECK(endgame.endings[0].art == 440);
    CHECK(endgame.endings[0].narrator == "nar_ar1");
    CHECK(endgame.endings[0].direction == 1); // no 5th field -> engine default 1 (not 0)

    CHECK(endgame.endings[1].narrator == "nar_eldr"); // inline '#' comment stripped
    CHECK(endgame.endings[2].direction == 1);         // optional 5th field (panning-desert art)
}

TEST_CASE("parseEndgameTxt tolerates empty input", "[endgame_txt]") {
    CHECK(parseEndgameTxt(std::string{}).endings.empty());
}

TEST_CASE("parseEndDeathTxt reads death endings as the engine tokenises them", "[endgame_txt]") {
    // Spaces, tabs and commas all separate fields; a '#' line is a comment; a short line is skipped.
    const EndDeathTxt endDeath = parseEndDeathTxt(std::string{ "# gvar, value, known, notKnown, level, %, narrator\n"
                                                               "-1, 0, -1, 16, 0, 40, nar_dth3\n"
                                                               "491\t1 3,-1, 150, 0, nar_mo1\n"
                                                               "-1, 0, -1\n" });

    REQUIRE(endDeath.endings.size() == 2);
    CHECK(endDeath.endings[0].gvar == -1);
    CHECK(endDeath.endings[0].areaKnown == -1);
    CHECK(endDeath.endings[0].areaNotKnown == 16);
    CHECK(endDeath.endings[0].percentage == 40);
    CHECK(endDeath.endings[0].narrator == "nar_dth3");
    CHECK(endDeath.endings[1].gvar == 491);
    CHECK(endDeath.endings[1].value == 1);
    CHECK(endDeath.endings[1].areaKnown == 3);
    CHECK(endDeath.endings[1].minLevel == 150);
    CHECK(endDeath.endings[1].narrator == "nar_mo1");
}

TEST_CASE("parseEndingSubtitles keeps the text after each line's first colon", "[endgame_txt]") {
    const auto lines = parseEndingSubtitles("1:The darkness awaits.\r\n2:Peace: at last.\nno separator\n");

    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "The darkness awaits.");
    CHECK(lines[1] == "Peace: at last.");
}
