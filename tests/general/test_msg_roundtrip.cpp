#include <catch2/catch_test_macros.hpp>

#include "format/msg/Msg.h"
#include "reader/msg/MsgReader.h"
#include "writer/msg/MsgSerializer.h"

#include <string>

using namespace geck;

namespace {
// '#' comments, an inline comment after a message, an empty audio and a non-empty audio, LF, and no
// trailing newline on the last line.
constexpr const char* kMsgLf = "#\n"
                               "# Map Names\n"
                               "{200}{}{Desert}  # DESERT1.MAP\n"
                               "{201}{}{}\n"
                               "{209}{snd}{Klamath}";
} // namespace

TEST_CASE("Msg round-trips LF byte-for-byte (no trailing newline)", "[msg_roundtrip]") {
    const Msg msg = parseMsg("test.msg", std::string{ kMsgLf });

    REQUIRE(msg.getMessages().count(200) == 1);
    CHECK(msg.getMessages().at(200).text == "Desert");
    CHECK(msg.getMessages().at(209).audio == "snd");
    CHECK_FALSE(msg.finalNewline());

    // Byte-for-byte round trip (this also proves the '#' comments and the inline "# DESERT1.MAP" survive).
    CHECK(writer::serializeMsg(msg) == std::string{ kMsgLf });
}

TEST_CASE("Msg normalizes CRLF to LF preserving data", "[msg_roundtrip]") {
    std::string crlf;
    for (const char* p = kMsgLf; *p != '\0'; ++p) {
        if (*p == '\n') {
            crlf += '\r';
        }
        crlf += *p;
    }
    CHECK(writer::serializeMsg(parseMsg("test.msg", crlf)) == std::string{ kMsgLf });
}

TEST_CASE("Msg::setMessageText updates (keeping audio + comment) or appends", "[msg_roundtrip]") {
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
