#include <catch2/catch_test_macros.hpp>

#include "format/quests/QuestsTxt.h"
#include "reader/quests/QuestsTxtReader.h"

using namespace geck;

namespace {
// quests.txt rows: comma-separated, an inline '#' comment, a full-line comment, a space-separated row
// (the engine tokenizes on space/tab/comma), and a too-short line that must be skipped.
constexpr const char* kQuestsTxt = R"(# location  description  gvar  display  completed
1500, 100, 9, 2, 6
1500, 130, 480, 0, 1   # Retrieve the GECK for Arroyo
# a full-line comment
1501 200 100 1 2
badline
)";
} // namespace

TEST_CASE("parseQuestsTxt reads quest rows, skipping comments and short lines", "[quests]") {
    const QuestsTxt quests = parseQuestsTxt(std::string{ kQuestsTxt });

    REQUIRE(quests.quests.size() == 3);

    CHECK(quests.quests[0].location == 1500);
    CHECK(quests.quests[0].description == 100);
    CHECK(quests.quests[0].gvar == 9);
    CHECK(quests.quests[0].displayThreshold == 2);
    CHECK(quests.quests[0].completedThreshold == 6);

    CHECK(quests.quests[1].gvar == 480); // inline '#' comment stripped
    CHECK(quests.quests[1].completedThreshold == 1);

    CHECK(quests.quests[2].location == 1501); // space-separated row also parses
    CHECK(quests.quests[2].gvar == 100);
}

TEST_CASE("parseQuestsTxt tolerates empty input", "[quests]") {
    CHECK(parseQuestsTxt(std::string{}).quests.empty());
}
