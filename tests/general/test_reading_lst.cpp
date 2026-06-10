#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

#include "format/lst/Lst.h"
#include "reader/lst/LstReader.h"
#include "support/Fixtures.h"

using namespace geck;

namespace {
std::vector<uint8_t> bytesOf(std::string_view s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}
} // namespace

TEST_CASE("LstReader trims comments, normalizes slashes, lowercases and skips blanks", "[lst]") {
    const std::string content = "ART/items/KNIFE.FRM ; a knife\r\n" // trailing comment + CRLF
                                "art\\scenery\\TREE.frm\n"          // backslashes -> forward slashes, LF
                                "; a full-line comment\n"           // leading ';' -> whole line is a comment -> skipped
                                "   \n"                             // whitespace only -> skipped
                                "\n"                                // blank -> skipped
                                "PLAIN.frm";                        // last line, no trailing newline

    LstReader reader;
    auto lst = reader.openFile("synthetic.lst", bytesOf(content));

    REQUIRE(lst != nullptr);
    const auto& list = lst->list();
    REQUIRE(list.size() == 3);
    CHECK(list[0] == "art/items/knife.frm");
    CHECK(list[1] == "art/scenery/tree.frm");
    CHECK(list[2] == "plain.frm");
}

TEST_CASE("LstReader at() indexes entries from zero", "[lst]") {
    LstReader reader;
    auto lst = reader.openFile("x.lst", bytesOf("first.frm\nsecond.frm\n"));

    REQUIRE(lst != nullptr);
    REQUIRE(lst->list().size() == 2);
    CHECK(lst->at(0) == "first.frm");
    CHECK(lst->at(1) == "second.frm");
}

TEST_CASE("LstReader returns an empty list for an empty file", "[lst]") {
    LstReader reader;
    auto lst = reader.openFile("empty.lst", std::vector<uint8_t>{});

    REQUIRE(lst != nullptr);
    CHECK(lst->list().empty());
}

TEST_CASE("LstReader parses a real scenery.lst fixture", "[lst]") {
    LstReader reader;
    auto lst = reader.openFile(geck::test::dataPath("scenery.lst"));

    REQUIRE(lst != nullptr);
    const auto& list = lst->list();
    REQUIRE(list.size() == 7);
    CHECK(list.front() == "chr5000.frm");
    CHECK(list.back() == "tum1000.frm");
}
