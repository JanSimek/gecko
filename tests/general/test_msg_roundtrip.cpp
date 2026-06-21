#include <catch2/catch_test_macros.hpp>

#include "format/msg/MsgDocument.h"
#include "reader/msg/MsgDocumentReader.h"
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

TEST_CASE("MsgDocument round-trips LF byte-for-byte (no trailing newline)", "[msg_roundtrip]") {
    const MsgDocument doc = parseMsgDocument(std::string{ kMsgLf });

    REQUIRE(doc.message(200) != nullptr);
    CHECK(doc.message(200)->text == "Desert");
    CHECK(doc.message(200)->inlineComment.find("# DESERT1.MAP") != std::string::npos);
    CHECK(doc.message(209)->audio == "snd");
    CHECK_FALSE(doc.finalNewline);

    CHECK(writer::serializeMsg(doc) == std::string{ kMsgLf });
}

TEST_CASE("MsgDocument normalizes CRLF to LF preserving data", "[msg_roundtrip]") {
    std::string crlf;
    for (const char* p = kMsgLf; *p != '\0'; ++p) {
        if (*p == '\n') {
            crlf += '\r';
        }
        crlf += *p;
    }
    CHECK(writer::serializeMsg(parseMsgDocument(crlf)) == std::string{ kMsgLf });
}

TEST_CASE("setMessageText updates (keeping audio + comment) or appends", "[msg_roundtrip]") {
    MsgDocument doc = parseMsgDocument(std::string{ kMsgLf });

    writer::setMessageText(doc, 200, "Wasteland");   // update; keep "# DESERT1.MAP"
    writer::setMessageText(doc, 209, "New Klamath"); // update; keep audio "snd"
    writer::setMessageText(doc, 500, "Brand New");   // not present -> append

    const std::string out = writer::serializeMsg(doc);
    CHECK(out.find("{200}{}{Wasteland}  # DESERT1.MAP\n") != std::string::npos);
    CHECK(out.find("{209}{snd}{New Klamath}") != std::string::npos);
    CHECK(out.find("{500}{}{Brand New}") != std::string::npos);
    CHECK(out.find("# Map Names\n") != std::string::npos);
    CHECK(writer::findMessageText(doc, 200).value_or("") == "Wasteland");
}
