#include <catch2/catch_test_macros.hpp>

#include "format/endgame/EndgameTxt.h"
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

TEST_CASE("parseEndgameTxt reads ending slides, skipping comments and short lines", "[endgame]") {
    const EndgameTxt endgame = parseEndgameTxt(std::string{ kEndgameTxt });

    REQUIRE(endgame.endings.size() == 3);

    CHECK(endgame.endings[0].gvar == 408);
    CHECK(endgame.endings[0].value == 1);
    CHECK(endgame.endings[0].art == 440);
    CHECK(endgame.endings[0].narrator == "nar_ar1");
    CHECK(endgame.endings[0].direction == 0);

    CHECK(endgame.endings[1].narrator == "nar_eldr"); // inline '#' comment stripped
    CHECK(endgame.endings[2].direction == 1);         // optional 5th field (panning-desert art)
}

TEST_CASE("parseEndgameTxt tolerates empty input", "[endgame]") {
    CHECK(parseEndgameTxt(std::string{}).endings.empty());
}
