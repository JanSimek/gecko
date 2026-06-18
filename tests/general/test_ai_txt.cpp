#include <catch2/catch_test_macros.hpp>

#include "format/ai/AiPacket.h"
#include "reader/ai/AiTxtReader.h"

using namespace geck;

namespace {
// A trimmed-down ai.txt in the real format: two valid packets (with the irrelevant animation/colour
// keys the engine also stores, to prove they're ignored), a comment, and a section with no
// packet_num that must be dropped.
constexpr const char* kAiTxt = R"(; Fallout 2 AI packets
[Addict - Wimpy]
aggression=10
disposition=coward
run_away_mode=coward
packet_num=179
secondary_freq=5000
hit_head_start=1000
color=58

[Aggressive - Tough]
packet_num=12
aggression=70
disposition=aggressive
best_weapon=ranged
distance=charge
area_attack_mode=be_sure

[Broken - NoNumber]
aggression=5
disposition=none
)";
} // namespace

TEST_CASE("parseAiTxt reads behaviour fields keyed by packet_num", "[ai]") {
    const AiTxt ai = parseAiTxt(std::string{ kAiTxt });

    // Two packets define a packet_num; the third (no packet_num) is dropped.
    CHECK(ai.size() == 2);

    const AiPacket* wimpy = ai.byPacketNum(179);
    REQUIRE(wimpy != nullptr);
    CHECK(wimpy->name == "Addict - Wimpy"); // section header is the name
    CHECK(wimpy->aggression == 10);
    CHECK(wimpy->disposition == "coward");
    CHECK(wimpy->runAwayMode == "coward");
    CHECK(wimpy->secondaryFreq == 5000);

    const AiPacket* tough = ai.byPacketNum(12);
    REQUIRE(tough != nullptr);
    CHECK(tough->name == "Aggressive - Tough");
    CHECK(tough->aggression == 70);
    CHECK(tough->disposition == "aggressive");
    CHECK(tough->bestWeapon == "ranged");
    CHECK(tough->distance == "charge");
    CHECK(tough->areaAttackMode == "be_sure");

    // Unknown packet numbers resolve to nullptr (caller falls back to the raw number).
    CHECK(ai.byPacketNum(999) == nullptr);
    CHECK(ai.byPacketNum(5) == nullptr); // the no-packet_num section
}

TEST_CASE("parseAiTxt tolerates empty / section-less input", "[ai]") {
    CHECK(parseAiTxt(std::string{}).size() == 0);
    CHECK(parseAiTxt(std::string{ "aggression=10\npacket_num=1\n" }).size() == 0); // keys before any [section]
}
