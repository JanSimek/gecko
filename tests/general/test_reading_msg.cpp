#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "format/msg/Msg.h"
#include "reader/msg/MsgReader.h"

using Catch::Matchers::Equals;

TEST_CASE("Parse .msg file", "[msg]") {
    geck::MsgReader msg_reader{};
    auto msg_file = msg_reader.openFile("data/test.msg");

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
