#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

#include "format/msg/Msg.h"
#include "reader/msg/MsgReader.h"
#include "support/Fixtures.h"

using Catch::Matchers::Equals;

namespace {
std::vector<uint8_t> msgBytes(std::string_view s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}
} // namespace

TEST_CASE("Parse .msg file", "[msg]") {
    geck::MsgReader msg_reader{};
    auto msg_file = msg_reader.openFile(geck::test::dataPath("test.msg"));

    REQUIRE_THAT(msg_file->message(6500).text, Equals("Mutant"));
    REQUIRE(msg_file->message(6500).id == 6500);
    REQUIRE_THAT(msg_file->message(6800).text, Equals("Strong Hobo"));
    REQUIRE_THAT(msg_file->message(6800).audio, Equals("filename.test"));
    REQUIRE_THAT(msg_file->message(7700).text, Equals("Sentry Bot"));
    REQUIRE_THAT(msg_file->message(7800).text, Equals("Sentry Bot Mark II"));

    // FIXME: bug in CMBATAI2.MSG in messages #1382 and #32020 where we need to handle missing '}'
    // REQUIRE_THAT(msg_file->message(1382).text, Equals("My eyes, bitch!"));
    // REQUIRE_THAT(msg_file->message(32020).text, Equals("Ow, my arm!"));
}

TEST_CASE("MsgReader parses id, audio and text from inline content", "[msg]") {
    geck::MsgReader reader;
    auto msg = reader.openFile("inline.msg", msgBytes("{100}{snd.acm}{Hello}\n{200}{}{World}\n"));

    REQUIRE(msg != nullptr);
    CHECK(msg->message(100).id == 100);
    CHECK(msg->message(100).audio == "snd.acm");
    CHECK(msg->message(100).text == "Hello");
    CHECK(msg->message(200).audio.empty());
    CHECK(msg->message(200).text == "World");
}

TEST_CASE("MsgReader returns an empty message for an unknown id", "[msg]") {
    geck::MsgReader reader;
    auto msg = reader.openFile("inline.msg", msgBytes("{1}{}{Only}\n"));

    REQUIRE(msg != nullptr);
    const auto& missing = msg->message(999);
    CHECK(missing.id == 0);
    CHECK(missing.text.empty());
}

TEST_CASE("MsgReader keeps the last entry for a duplicate id", "[msg]") {
    geck::MsgReader reader;
    auto msg = reader.openFile("inline.msg", msgBytes("{5}{}{First}\n{5}{}{Second}\n"));

    REQUIRE(msg != nullptr);
    CHECK(msg->message(5).text == "Second");
}

TEST_CASE("MsgReader rejects an empty file", "[msg]") {
    geck::MsgReader reader;
    REQUIRE_THROWS(reader.openFile("empty.msg", std::vector<uint8_t>{}));
}
