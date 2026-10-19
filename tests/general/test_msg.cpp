#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "format/msg/Msg.h"
#include "reader/msg/MsgReader.h"
#include "writer/msg/MsgSerializer.h"

#include "support/Fixtures.h"

using namespace geck;
using Catch::Matchers::Equals;

namespace {
// '#' comments, an inline comment after a message, an empty audio and a non-empty audio, LF, and no
// trailing newline on the last line.
constexpr const char* kMsgLf = "#\n"
                               "# Map Names\n"
                               "{200}{}{Desert}  # DESERT1.MAP\n"
                               "{201}{}{}\n"
                               "{209}{snd}{Klamath}";
} // namespace

TEST_CASE("MsgReader reads ids, text and audio from the test.msg fixture", "[msg]") {
    MsgReader msg_reader{};
    auto msg_file = msg_reader.openFile(test::dataPath("test.msg"));

    REQUIRE_THAT(msg_file->message(6500).text, Equals("Mutant"));
    REQUIRE(msg_file->message(6500).id == 6500);
    REQUIRE_THAT(msg_file->message(6800).text, Equals("Strong Hobo"));
    REQUIRE_THAT(msg_file->message(6800).audio, Equals("filename.test"));
    REQUIRE_THAT(msg_file->message(7700).text, Equals("Sentry Bot"));
    REQUIRE_THAT(msg_file->message(7800).text, Equals("Sentry Bot Mark II"));
}

TEST_CASE("MsgReader parses id, audio and text from inline content", "[msg]") {
    MsgReader reader;
    auto msg = reader.openFile("inline.msg", test::bytesOf("{100}{snd.acm}{Hello}\n{200}{}{World}\n"));

    REQUIRE(msg != nullptr);
    CHECK(msg->message(100).id == 100);
    CHECK(msg->message(100).audio == "snd.acm");
    CHECK(msg->message(100).text == "Hello");
    CHECK(msg->message(200).audio.empty());
    CHECK(msg->message(200).text == "World");
}

// RP and vanilla dialog files wrap long lines (acvillgr.msg 161, 172, 303); the engine reads a field
// up to its '}' across line breaks and drops the breaks (message.cc _message_load_field).
TEST_CASE("MsgReader reads a message whose text runs over several lines", "[msg]") {
    MsgReader reader;
    auto msg = reader.openFile("wrapped.msg", test::bytesOf("{160}{}{One line.}\r\n"
                                                            "{161}{}{Talk to Mynoc, the guardian of the\r\n"
                                                            " caravans.}  # trailing comment\r\n"
                                                            "{162}{}{Next.}\r\n"));
    REQUIRE(msg != nullptr);
    CHECK(msg->message(160).text == "One line.");
    CHECK(msg->message(161).text == "Talk to Mynoc, the guardian of the caravans.");
    CHECK(msg->message(162).text == "Next.");
    CHECK(msg->getMessages().size() == 3);
}

TEST_CASE("MsgReader returns an empty message for an unknown id", "[msg]") {
    MsgReader reader;
    auto msg = reader.openFile("inline.msg", test::bytesOf("{1}{}{Only}\n"));

    REQUIRE(msg != nullptr);
    const auto& missing = msg->message(999);
    CHECK(missing.id == 0);
    CHECK(missing.text.empty());
    CHECK(msg->getMessages().size() == 1); // a miss does not insert an entry
}

TEST_CASE("MsgReader keeps the last entry for a duplicate id", "[msg]") {
    MsgReader reader;
    auto msg = reader.openFile("inline.msg", test::bytesOf("{5}{}{First}\n{5}{}{Second}\n"));

    REQUIRE(msg != nullptr);
    CHECK(msg->message(5).text == "Second");
}

TEST_CASE("MsgReader rejects an empty file", "[msg]") {
    MsgReader reader;
    REQUIRE_THROWS(reader.openFile("empty.msg", std::vector<uint8_t>{}));
}

TEST_CASE("Msg round-trips LF byte-for-byte (no trailing newline)", "[msg][roundtrip]") {
    const Msg msg = parseMsg("test.msg", std::string{ kMsgLf });

    REQUIRE(msg.getMessages().count(200) == 1);
    CHECK(msg.getMessages().at(200).text == "Desert");
    CHECK(msg.getMessages().at(209).audio == "snd");
    CHECK_FALSE(msg.finalNewline());

    // Byte-for-byte round trip (this also proves the '#' comments and the inline "# DESERT1.MAP" survive).
    CHECK(writer::serializeMsg(msg) == std::string{ kMsgLf });
}

TEST_CASE("Msg normalizes CRLF to LF preserving data", "[msg][roundtrip]") {
    const std::string crlf = geck::test::toCrlf(kMsgLf);
    CHECK(writer::serializeMsg(parseMsg("test.msg", crlf)) == std::string{ kMsgLf });
}

TEST_CASE("Msg::setMessageText updates (keeping audio + comment) or appends", "[msg]") {
    Msg msg = parseMsg("test.msg", std::string{ kMsgLf });

    msg.setMessageText(200, "Wasteland");   // update; keep "# DESERT1.MAP"
    msg.setMessageText(209, "New Klamath"); // update; keep audio "snd"
    msg.setMessageText(500, "Brand New");   // not present -> append

    const std::string out = writer::serializeMsg(msg);
    CHECK(out.find("{200}{}{Wasteland}  # DESERT1.MAP\n") != std::string::npos);
    CHECK(out.find("{209}{snd}{New Klamath}") != std::string::npos);
    CHECK(out.find("{500}{}{Brand New}") != std::string::npos);
    CHECK(out.find("# Map Names\n") != std::string::npos);
    CHECK(msg.message(500).text == "Brand New");
}

TEST_CASE("Msg round-trips a message whose text runs over several lines", "[msg][roundtrip]") {
    const std::string wrapped = "{160}{}{One line.}\n"
                                "{161}{}{Talk to Mynoc, the guardian of the\n"
                                " caravans.}  # trailing comment\n"
                                "{162}{}{Next.}\n";
    const Msg msg = parseMsg("wrapped.msg", wrapped);

    CHECK(msg.getMessages().at(161).text == "Talk to Mynoc, the guardian of the caravans.");
    CHECK(writer::serializeMsg(msg) == wrapped);
}
